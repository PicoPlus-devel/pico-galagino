/*
 * Live SDL2 front end for the host harness (gal_host --live), ported from the
 * one of pico-infonesPlus (hosttest/host_sdl.cpp).
 *
 * Shows each frame in a window, plays the audio and reads the keyboard, so a
 * game can be watched, heard and played on the desktop. gal_host.c stays in
 * charge of the emulation; this file only moves pixels, samples and key
 * state, and paces the loop to real time.
 *
 * Pacing follows the audio clock: after each frame the loop waits until no
 * more than three frames of audio are queued, so emulation runs exactly as
 * fast as the output consumes the 735 samples each frame makes. The wait
 * never exceeds one frame period, so a stalled output cannot slow the game
 * down. Without an audio device a timer on the frame period does the same
 * job.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "host_sdl.h"

static int W, H;   /* the canvas */
static int rot;    /* degrees clockwise it is shown at */
static int TW, TH; /* the texture: the canvas, turned */
static uint32_t *turned;

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioDeviceID adev;
static int have_frame;

static uint32_t frame_period_us;
static uint64_t next_tick;
static uint32_t chunk_bytes; /* size of the last queued frame of audio */
static int paused;
static int fast_forward;
static int title_dirty = 1;
static int last_frame = -1;
static char last_what[64];
static uint32_t fps_t0, fps_frames;
static double fps;

/* LeakSanitizer: the OpenGL driver SDL's renderer loads (Mesa, LLVM) and the
 * sound and desktop libraries keep allocations until exit. Not the harness's,
 * and they would make every live run exit with an error. */
const char *__lsan_default_suppressions(void)
{
    return "leak:libSDL2\nleak:libLLVM\nleak:libGL\nleak:libEGL\nleak:libgallium\nleak:libpulse\nleak:libdbus\n"
           "leak:libX11\nleak:libwayland\n";
}

int hsdl_init(int w, int h, int rotate, int scale, int audio_on, uint32_t frame_us)
{
    W = w;
    H = h;
    rot = rotate;
    TW = rot ? h : w;
    TH = rot ? w : h;
    if (rot)
        turned = malloc((size_t)w * h * sizeof(uint32_t));
    frame_period_us = frame_us ? frame_us : 16667;

    if (SDL_Init(SDL_INIT_VIDEO) != 0)
    {
        fprintf(stderr, "LIVE: SDL video init failed: %s\n", SDL_GetError());
        return 0;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* nearest neighbour */
    if (scale < 1)
        scale = 1;
    win = SDL_CreateWindow("gal_host", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, TW * scale, TH * scale,
                           SDL_WINDOW_RESIZABLE);
    if (!win)
    {
        fprintf(stderr, "LIVE: cannot open a window: %s\n", SDL_GetError());
        return 0;
    }
    /* No PRESENTVSYNC: the audio clock paces the loop, a display vsync would
     * fight it. */
    ren = SDL_CreateRenderer(win, -1, 0);
    if (!ren)
    {
        fprintf(stderr, "LIVE: cannot create a renderer: %s\n", SDL_GetError());
        return 0;
    }
    SDL_RenderSetLogicalSize(ren, TW, TH);
    SDL_RenderSetIntegerScale(ren, SDL_TRUE);
    tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, TW, TH);
    if (!tex)
    {
        fprintf(stderr, "LIVE: cannot create a texture: %s\n", SDL_GetError());
        return 0;
    }

    if (audio_on)
    {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
            fprintf(stderr, "LIVE: no audio (%s), pacing on a timer\n", SDL_GetError());
        else
        {
            SDL_AudioSpec want, have;
            memset(&want, 0, sizeof(want));
            want.freq = 44100;
            want.format = AUDIO_S16SYS;
            want.channels = 1;
            want.samples = 512;
            adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
            if (!adev)
                fprintf(stderr, "LIVE: cannot open audio output (%s), pacing on a timer\n", SDL_GetError());
            else
                SDL_PauseAudioDevice(adev, 0);
        }
    }

    printf("LIVE: video %s, audio %s, window %dx%d\n", SDL_GetCurrentVideoDriver(),
           adev ? SDL_GetCurrentAudioDriver() : "off", TW * scale, TH * scale);
    printf("LIVE: pad  arrows=joystick  Z,C=fire  X,V=fire/2nd button  A=coin (on release)  S=start\n"
           "LIVE:      5=coin  1=start  2=2 player start  A+S=back to the menu\n"
           "LIVE: keys Space=pause  N=step  Tab=fast-forward (hold)  F12=dump frame  Esc=quit\n");
    fflush(stdout);
    return 1;
}

void hsdl_quit(void)
{
    if (adev)
        SDL_CloseAudioDevice(adev);
    if (tex)
        SDL_DestroyTexture(tex);
    if (ren)
        SDL_DestroyRenderer(ren);
    if (win)
        SDL_DestroyWindow(win);
    adev = 0;
    tex = NULL;
    ren = NULL;
    win = NULL;
    free(turned);
    turned = NULL;
    SDL_Quit();
}

void hsdl_redraw(void)
{
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    if (have_frame)
        SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
}

void hsdl_present(const uint32_t *px)
{
    if (rot == 90)
    {
        /* the canvas's left edge at the top */
        for (int y = 0; y < TH; y++)
            for (int x = 0; x < TW; x++)
                turned[y * TW + x] = px[(H - 1 - x) * W + y];
        px = turned;
    }
    else if (rot == 270)
    {
        /* its right edge at the top */
        for (int y = 0; y < TH; y++)
            for (int x = 0; x < TW; x++)
                turned[y * TW + x] = px[x * W + (W - 1 - y)];
        px = turned;
    }
    SDL_UpdateTexture(tex, NULL, px, TW * (int)sizeof(uint32_t));
    have_frame = 1;
    hsdl_redraw();
}

void hsdl_audio(const int16_t *buf, int n)
{
    if (!adev || paused || fast_forward || n <= 0)
        return;
    chunk_bytes = (uint32_t)n * sizeof(int16_t);
    if (SDL_GetQueuedAudioSize(adev) > 8 * chunk_bytes)
        return; /* output stalled: drop, see hsdl_pace() */
    SDL_QueueAudio(adev, buf, chunk_bytes);
}

/* Keyboard -> pad. The keys of a USB keyboard on the board
 * (pico_shared/hid_app.cpp), matched by position like the HID usage codes,
 * plus MAME's 5, 1 and 2. */
static uint32_t keyboard_pad(void)
{
    static const struct
    {
        SDL_Scancode key;
        uint32_t bit;
    } map[] = {
        {SDL_SCANCODE_Z, HSDL_A},         {SDL_SCANCODE_X, HSDL_B},       {SDL_SCANCODE_C, HSDL_X},
        {SDL_SCANCODE_V, HSDL_Y},         {SDL_SCANCODE_A, HSDL_SELECT},  {SDL_SCANCODE_S, HSDL_START},
        {SDL_SCANCODE_UP, HSDL_UP},       {SDL_SCANCODE_DOWN, HSDL_DOWN}, {SDL_SCANCODE_LEFT, HSDL_LEFT},
        {SDL_SCANCODE_RIGHT, HSDL_RIGHT}, {SDL_SCANCODE_5, HSDL_COIN},    {SDL_SCANCODE_1, HSDL_START},
        {SDL_SCANCODE_2, HSDL_START2},
    };
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    uint32_t pad = 0;
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++)
        if (ks[map[i].key])
            pad |= map[i].bit;
    return pad;
}

uint32_t hsdl_poll(int *events)
{
    SDL_Event e;
    while (SDL_PollEvent(&e))
    {
        if (e.type == SDL_QUIT)
            *events |= HSDL_EV_QUIT;
        else if (e.type == SDL_KEYDOWN)
        {
            const int repeat = e.key.repeat;
            switch (e.key.keysym.scancode)
            {
            case SDL_SCANCODE_ESCAPE:
                *events |= HSDL_EV_QUIT;
                break;
            case SDL_SCANCODE_SPACE:
                if (repeat)
                    break;
                paused = !paused;
                title_dirty = 1;
                if (paused && adev)
                    SDL_ClearQueuedAudio(adev);
                next_tick = 0;
                break;
            case SDL_SCANCODE_N: /* held: keeps stepping */
                if (paused)
                    *events |= HSDL_EV_STEP;
                break;
            case SDL_SCANCODE_F12:
                if (!repeat)
                    *events |= HSDL_EV_DUMP;
                break;
            default:
                break;
            }
        }
        else if (e.type == SDL_WINDOWEVENT)
        {
            if (e.window.event == SDL_WINDOWEVENT_EXPOSED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
                hsdl_redraw();
        }
    }
    const int ff = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_TAB] != 0;
    if (ff != fast_forward)
    {
        fast_forward = ff;
        title_dirty = 1;
        next_tick = 0;
        if (ff && adev)
            SDL_ClearQueuedAudio(adev);
    }
    return keyboard_pad();
}

int hsdl_paused(void)
{
    return paused;
}

void hsdl_idle(void)
{
    SDL_Delay(15);
}

void hsdl_pace(void)
{
    if (paused || fast_forward)
        return;
    const uint64_t freq = SDL_GetPerformanceFrequency();
    const uint64_t period = freq * frame_period_us / 1000000u;
    uint64_t now = SDL_GetPerformanceCounter();
    if (!next_tick || now > next_tick + 4 * period)
        next_tick = now; /* start, or resync after a stall */
    next_tick += period;
    if (adev)
    {
        /* Keep ~3 frames (~50 ms) queued. A healthy output drains one frame
         * of audio per frame period, and each frame that paces on it
         * re-anchors the schedule, so the audio clock rules. The WSLg
         * PulseAudio sink sometimes stalls for seconds: then the wait gives
         * up one period past the schedule, which keeps the game at full
         * speed (hsdl_audio stops queueing at 8 frames, so latency stays
         * bounded and the sound comes back within a few frames once it
         * resumes). */
        const uint32_t limit = 3 * chunk_bytes;
        const uint64_t deadline = next_tick + period;
        while (SDL_GetQueuedAudioSize(adev) > limit)
        {
            if ((now = SDL_GetPerformanceCounter()) >= deadline)
                break;
            SDL_Delay(1);
        }
        if (now < deadline)
            next_tick = now;
        return;
    }
    while ((now = SDL_GetPerformanceCounter()) < next_tick)
    {
        const uint64_t ms = (next_tick - now) * 1000u / freq;
        SDL_Delay(ms > 1 ? (uint32_t)(ms - 1) : 0);
    }
}

void hsdl_status(const char *what, int frame)
{
    const uint32_t t = SDL_GetTicks();
    if (frame != last_frame)
    {
        if (paused)
            title_dirty = 1; /* show every single step */
        last_frame = frame;
        fps_frames++;
    }
    if (strcmp(what, last_what))
    {
        snprintf(last_what, sizeof(last_what), "%s", what);
        title_dirty = 1;
    }
    if (!fps_t0)
        fps_t0 = t;
    if (t - fps_t0 >= 1000)
    {
        fps = fps_frames * 1000.0 / (double)(t - fps_t0);
        fps_frames = 0;
        fps_t0 = t;
        title_dirty = 1;
    }
    if (!title_dirty)
        return;
    title_dirty = 0;
    char buf[256];
    snprintf(buf, sizeof(buf), "gal_host | %s | frame %d | %.1f fps%s", what, frame, fps,
             paused         ? " | PAUSED (N = step)"
             : fast_forward ? " | FAST-FORWARD"
                            : "");
    SDL_SetWindowTitle(win, buf);
}
