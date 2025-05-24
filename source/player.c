#include "player.h"
#include "track1.h"
#include "track2.h"
#include "track3.h"

#include <3ds.h>
#include <3ds/ndsp/ndsp.h>
#include <malloc.h>
#include <string.h>
#include "tremor/ivorbisfile.h"

#define AUDIO_SAMPLE_RATE  44100
#define AUDIO_CHANNELS     2
#define AUDIO_BUFFER_SIZE  (1024 * AUDIO_CHANNELS)

typedef struct {
    const unsigned char* data;
    unsigned int size;
    size_t offset;
} Track;

static Track tracks[3];

static OggVorbis_File vf;
static bool playing = false;
static bool audio_initialized = false;

static s16* audio_buffer = NULL;
static int current_track = -1;

// === OGG CALLBACKS ===

static size_t mem_read_func(void *ptr, size_t size, size_t nmemb, void *datasource) {
    Track* track = (Track*)datasource;
    size_t bytes_to_read = size * nmemb;

    if (track->offset + bytes_to_read > track->size)
        bytes_to_read = track->size - track->offset;

    memcpy(ptr, track->data + track->offset, bytes_to_read);
    track->offset += bytes_to_read;

    return bytes_to_read / size;
}

static int mem_seek_func(void *datasource, ogg_int64_t offset, int whence) {
    Track* track = (Track*)datasource;

    switch (whence) {
        case SEEK_SET:
            if ((size_t)offset > track->size) return -1;
            track->offset = offset;
            break;
        case SEEK_CUR:
            if (track->offset + offset > track->size) return -1;
            track->offset += offset;
            break;
        case SEEK_END:
        if (offset > 0 || (size_t)(-offset) > track->size) return -1;
            track->offset = track->size + offset;
            break;
        default:
            return -1;
    }

    return 0;
}

static int mem_close_func(void *datasource) {
    return 0;
}

static long mem_tell_func(void *datasource) {
    Track* track = (Track*)datasource;
    return (long)track->offset;
}

// === NDSP CALLBACK ===

static ndspWaveBuf waveBuf;

static void myNdspCallback(void* unused) {
    if (!playing || waveBuf.status != NDSP_WBUF_DONE)
        return;

    int bitstream = 0;
    long bytesRead = ov_read(&vf, (char*)audio_buffer,
                              AUDIO_BUFFER_SIZE * sizeof(s16),
                              &bitstream);

    if (bytesRead <= 0) {
        playing = false;
        return;
    }

    memset(&waveBuf, 0, sizeof(ndspWaveBuf));
    waveBuf.data_vaddr = audio_buffer;
    waveBuf.nsamples = bytesRead / sizeof(s16) / AUDIO_CHANNELS;
    waveBuf.looping = false;

    ndspChnWaveBufAdd(0, &waveBuf);
}

/* === PLAYER CONTROL ===
Function to initialize the audio player
This function should be called before any playback
It initializes the NDSP library and sets up the audio buffer
It also initializes the tracks array with the OGG data
The tracks array is initialized at runtime to avoid static initialization issues
The audio buffer is allocated with memalign to ensure proper alignment for the DSP
The NDSP channel is set to stereo PCM16 format with a sample rate of 44100 Hz
The NDSP callback is set to handle audio processing
The audio_initialized flag is used to prevent re-initialization
The playerInit function should be called once at the start of the program
*/
void playerInit(void) {
    if (audio_initialized)
        return;

    // Initialize tracks array at runtime
    tracks[0].data = track1_ogg;
    tracks[0].size = track1_ogg_len;
    tracks[0].offset = 0;
    tracks[1].data = track2_ogg;
    tracks[1].size = track2_ogg_len;
    tracks[1].offset = 0;
    tracks[2].data = track3_ogg;
    tracks[2].size = track3_ogg_len;
    tracks[2].offset = 0;

    ndspInit();
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);

    audio_buffer = (s16*)memalign(0x1000, AUDIO_BUFFER_SIZE * sizeof(s16));
    memset(audio_buffer, 0, AUDIO_BUFFER_SIZE * sizeof(s16));

    ndspChnSetInterp(0, NDSP_INTERP_POLYPHASE);
    ndspChnSetRate(0, AUDIO_SAMPLE_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    float mix[12] = {1.0f, 1.0f}; // ✅ Full volume to left and right
    ndspChnSetMix(0, mix);
    ndspSetCallback(myNdspCallback, NULL);
    audio_initialized = true;
}

void playerStop(void) {
    if (!playing) return;

    ov_clear(&vf);
    ndspChnReset(0);
    playing = false;
}
/*Function to play a track by index
This function stops any currently playing track, sets the current track index,
opens the OGG file using the ov_open_callbacks function, and initializes the wave buffer
The wave buffer is set to the audio buffer and marked as done
The NDSP channel is set to play the wave buffer
The playing flag is set to true to indicate that playback is in progress
The playerPlay function should be called with the index of the track to play
The index should be between 0 and the number of tracks - 1
The playerStop function should be called to stop playback
The playerExit function should be called to clean up resources.
*/
// Function to start playback of a track by index
// This function stops any currently playing track, sets the current track index,
// opens the OGG file using the ov_open_callbacks function, and initializes the wave buffer.
void playerPlay(int index) {
    if (playing) {
        playerStop();
    }

    current_track = index;
    tracks[index].offset = 0;

    ov_callbacks callbacks = {
        .read_func = mem_read_func,
        .seek_func = mem_seek_func,
        .close_func = mem_close_func,
        .tell_func = mem_tell_func
    };

    if (ov_open_callbacks(&tracks[index], &vf, NULL, 0, callbacks) < 0) {
        return; // Failed to open OGG
    }

    memset(&waveBuf, 0, sizeof(ndspWaveBuf));
    waveBuf.data_vaddr = audio_buffer;
    waveBuf.status = NDSP_WBUF_DONE;

    ndspChnWaveBufAdd(0, &waveBuf);
    playing = true;
}
/* Function to exit the audio player
This function stops any currently playing track, resets the NDSP channel,
frees the audio buffer, and exits the NDSP library
It also sets the audio_initialized flag to false
The playerExit function should be called at the end of the program
to clean up resources.
*/
void playerExit(void) {
    if (playing) {
        playerStop();
    }

    if (audio_initialized) {
        ndspChnReset(0);
        ndspExit();
        free(audio_buffer);
        audio_buffer = NULL;
        audio_initialized = false;
    }
}
