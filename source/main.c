#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <tremor/ivorbisfile.h>
#include <tremor/ivorbiscodec.h>

#define NUM_TRACKS 3
#define DEBUG_LOG_LINES 8
#define DEBUG_LOG_LINE_LENGTH 64
#define SEEK_BAR_X 40
#define SEEK_BAR_Y 180
#define SEEK_BAR_WIDTH 320
#define SEEK_BAR_HEIGHT 10

// Track names for UI display
static const char* trackNames[NUM_TRACKS] = {
    "Track 1",
    "Track 2",
    "Track 3"
};

// Playback state
static int selectedTrack = 0;
static bool isPlaying = true;
static float trackLength = 180.0f; // mock duration in seconds
static float trackPosition = 0.0f; // current position in seconds

// Debug log buffer
static char debugLog[DEBUG_LOG_LINES][DEBUG_LOG_LINE_LENGTH];
static int debugLogIndex = 0;

// Append formatted message to debug log
static void debug_log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(debugLog[debugLogIndex], DEBUG_LOG_LINE_LENGTH, fmt, args);
    va_end(args);
    debugLogIndex = (debugLogIndex + 1) % DEBUG_LOG_LINES;
}

// Render debug log lines on bottom screen
static void render_debug_log(C2D_TextBuf buf, C2D_Text* texts) {
    for (int i = 0; i < DEBUG_LOG_LINES; ++i) {
        int idx = (debugLogIndex + i) % DEBUG_LOG_LINES;
        C2D_TextParse(&texts[i], buf, debugLog[idx]);
        C2D_TextOptimize(&texts[i]);
        C2D_DrawText(&texts[i], C2D_AtBaseline | C2D_WithColor, 8, 10 + i * 16, 1.0f, 1.0f, 1.0f, C2D_Color32(255, 255, 255, 255));
    }
}

// Draw seek bar with current playback progress
static void draw_seek_bar(float position, float length) {
    // Background bar (gray)
    C2D_DrawRectSolid(SEEK_BAR_X, SEEK_BAR_Y, 0, SEEK_BAR_WIDTH, SEEK_BAR_HEIGHT, C2D_Color32(50, 50, 50, 255));

    if (length > 0.0f) {
        float progressRatio = position / length;
        if (progressRatio > 1.0f) progressRatio = 1.0f;
        float progressWidth = SEEK_BAR_WIDTH * progressRatio;

        // Filled progress (blue)
        C2D_DrawRectSolid(SEEK_BAR_X, SEEK_BAR_Y, 0, progressWidth, SEEK_BAR_HEIGHT, C2D_Color32(0, 160, 255, 255));

        // Knob (white rectangle)
        float knobX = SEEK_BAR_X + progressWidth - 4;
        C2D_DrawRectSolid(knobX, SEEK_BAR_Y - 4, 0, 8, SEEK_BAR_HEIGHT + 8, C2D_Color32(255, 255, 255, 255));
    }
}

// Draw current track and playback status on top screen
static void draw_playback_info(C2D_TextBuf buf, C2D_Text* text) {
    char info[128];
    snprintf(info, sizeof(info),
        "%s [%s] %02d:%02d / %02d:%02d",
        trackNames[selectedTrack],
        isPlaying ? "Playing" : "Paused",
        (int)(trackPosition / 60), (int)((int)trackPosition % 60),
        (int)(trackLength / 60), (int)((int)trackLength % 60)
    );

    C2D_TextParse(text, buf, info);
    C2D_TextOptimize(text);
    C2D_DrawText(text, C2D_AtBaseline | C2D_WithColor, 8, 40, 1.0f, 1.0f, 1.0f, C2D_Color32(255, 255, 0, 255));
}
// Add missing declarations for playing and vf
static bool playing = false;

// Dummy struct for vf, replace with actual type if using libvorbisfile
// Use the actual OggVorbis_File struct from the Tremor library

static OggVorbis_File vf;

bool playerIsPlaying(void) {
    return playing;
}
int main() {
    // Initialize services and graphics
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();

    // Create render targets for top and bottom screens
    C3D_RenderTarget* topTarget = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* botTarget = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

    // Text buffers for UI and debug log
    C2D_TextBuf topTextBuf = C2D_TextBufNew(256);
    C2D_TextBuf botTextBuf = C2D_TextBufNew(1024);

    C2D_Text topText;
    C2D_Text debugTexts[DEBUG_LOG_LINES];

    // Initialize debug log with startup message
    debug_log("Application started");

    // Variables for timing playback updates
    u64 lastTick = svcGetSystemTick();
    const u64 ticksPerSecond = 268123480; // approximate ticks per second on 3DS

    // Main loop
    while (aptMainLoop()) {
        hidScanInput();
        u32 kDown = hidKeysDown();
        u32 kHeld = hidKeysHeld();

        if (kDown & KEY_START)
            break;

        // Track switching (left/right d-pad)
        if (kDown & KEY_DRIGHT) {
            selectedTrack = (selectedTrack + 1) % NUM_TRACKS;
            trackPosition = 0.0f;
            debug_log("Selected track: %s", trackNames[selectedTrack]);
        }
        if (kDown & KEY_DLEFT) {
            selectedTrack = (selectedTrack + NUM_TRACKS - 1) % NUM_TRACKS;
            trackPosition = 0.0f;
            debug_log("Selected track: %s", trackNames[selectedTrack]);
        }

        // Play/pause toggle (A button)
        if (kDown & KEY_A) {
            isPlaying = !isPlaying;
            debug_log(isPlaying ? "Playback resumed" : "Playback paused");
        }

        // Seek control (L/R held)
        if (kHeld & KEY_L) {
            trackPosition -= 1.0f / 30.0f;  // slower rewind
            if (trackPosition < 0.0f)
                trackPosition = 0.0f;
        }
        if (kHeld & KEY_R) {
            trackPosition += 1.0f / 30.0f;  // slower forward seek
            if (trackPosition > trackLength)
                trackPosition = trackLength;
        }

        // Playback simulation with timing independent from frame rate
        u64 currentTick = svcGetSystemTick();
        double deltaSeconds = (double)(currentTick - lastTick) / (double)ticksPerSecond;
        lastTick = currentTick;

        if (isPlaying) {
            trackPosition += (float)deltaSeconds;
            if (trackPosition >= trackLength) {
                trackPosition = trackLength;
                isPlaying = false;
                debug_log("Track ended");
            }
        }

        // Start drawing top screen
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        C2D_TargetClear(topTarget, C2D_Color32(0, 0, 0, 255));
        C2D_SceneBegin(topTarget);

        draw_playback_info(topTextBuf, &topText);
        draw_seek_bar(trackPosition, trackLength);

        // Start drawing bottom screen (debug log)
        C2D_TargetClear(botTarget, C2D_Color32(16, 16, 16, 255));
        C2D_SceneBegin(botTarget);
        render_debug_log(botTextBuf, debugTexts);

        // Finish frame and swap buffers
        C3D_FrameEnd(0);
        gfxSwapBuffers();
        gfxFlushBuffers();
    }

    // Cleanup resources
    C2D_TextBufDelete(topTextBuf);
    C2D_TextBufDelete(botTextBuf);
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
