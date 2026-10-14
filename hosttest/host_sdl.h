/*
 * Live SDL2 front end for the host harness (gal_host --live, see gal_host.c).
 *
 * Free of Galagino headers: host_sdl.c only sees plain pixels, samples and a
 * pad mask, so the cabinet mapping stays in gal_host.c with the game logic.
 */
#ifndef HOST_SDL_H
#define HOST_SDL_H

#include <stdint.h>

/* Event bits returned by hsdl_poll() */
#define HSDL_EV_QUIT (1 << 0) /* Esc, window close or Ctrl-C */
#define HSDL_EV_DUMP (1 << 1) /* F12: dump the frame on screen */
#define HSDL_EV_STEP (1 << 2) /* N while paused: run one frame */

/* The pad of hsdl_poll(): the buttons a USB keyboard gives on the board
 * (pico_shared/hid_app.cpp), plus MAME's coin and 2 player start keys */
#define HSDL_A 0x001
#define HSDL_B 0x002
#define HSDL_X 0x004
#define HSDL_Y 0x008
#define HSDL_SELECT 0x010
#define HSDL_START 0x020
#define HSDL_UP 0x040
#define HSDL_DOWN 0x080
#define HSDL_LEFT 0x100
#define HSDL_RIGHT 0x200
#define HSDL_COIN 0x400
#define HSDL_START2 0x800

/* Opens a window for a w x h canvas, shown `rotate` degrees clockwise (0, 90
 * or 270) and `scale` times enlarged, and, when audio_on, a 44.1 kHz s16 mono
 * output. frame_us is the frame period used for pacing when there is no audio
 * device. Returns 0 if the window cannot be created. */
int hsdl_init(int w, int h, int rotate, int scale, int audio_on, uint32_t frame_us);
void hsdl_quit(void);

/* One frame of w x h XRGB8888 pixels, as the canvas holds it */
void hsdl_present(const uint32_t *px);
void hsdl_redraw(void);

/* Queue one frame of audio. Dropped while paused or fast-forwarding, and while
 * the output is stalled. */
void hsdl_audio(const int16_t *buf, int n);

/* Pump SDL events. Returns the pad (HSDL_A ...) and ORs HSDL_EV_* bits into
 * *events. */
uint32_t hsdl_poll(int *events);
int hsdl_paused(void);
void hsdl_idle(void); /* short sleep for the pause loop */

/* Wait until the next frame is due: on the audio queue when there is an audio
 * device, else on a timer. Returns at once while fast-forwarding. */
void hsdl_pace(void);

/* Refresh the window title (what runs, frame, measured fps, state) about once
 * a second, or at once when one of them changes. */
void hsdl_status(const char *what, int frame);

#endif
