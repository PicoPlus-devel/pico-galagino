# Host test harness

The emulator in `galagino/` is plain C without Pico dependencies, and also builds for Linux. The commands below are run from the root of the repository:

```sh
hosttest/build.sh
./hosttest/gal_host ~/roms/arcade GALAGA 2400 300 hosttest/out --coin 700 --start 800 --play 900 --wav hosttest/out/game.wav
./hosttest/gal_host ~/roms/arcade menu 120 30 hosttest/out
./hosttest/gal_host ~/roms/arcade --live
python3 hosttest/ppm2png.py hosttest/out/frame_01200.ppm
hosttest/conv_check.py ~/roms/arcade path/to/galagino /tmp/conv_check
./hosttest/zex_host zexdoc.com zexall.com
./hosttest/flash_test
```

- `gal_host` takes the folder that holds the game folders, as on the SD card, and a game (its folder name) or `menu`. It loads the set through the same loader as the firmware, runs the given number of frames with scripted input, writes every Nth frame as a 320 x 240 PPM in the chosen orientation (`--tate 0|1|2`) and the sound as a WAV file. `--tall` writes instead the 640 x 480 picture that a board with HSTX video output shows with *Tate mode* off. `--no-psram` runs it as a board without PSRAM, and `--flash FILE` (with `--flash-kb N` for its size) with a flash cache, kept in FILE between runs, which a large game is copied into first, as the board does. `--hiscore DIR` reads and writes the high score files in DIR. In the menu, `--select GAME` selects a game at the start, `--attract-now` makes attract mode choose a game at once instead of after 20 seconds, and `--rotate N` runs attract mode as the board does, with N seconds per game; `--seed N` fixes the random choice. `--video FILE` writes every frame as raw RGB24, which ffmpeg can turn into a video, for example `ffmpeg -f rawvideo -pix_fmt rgb24 -s 640x480 -r 60 -i FILE -i game.wav out.mp4` with `--tall`.
- `flash_test` checks the flash cache on its own with made-up games: where slots go, which copy makes room, finding them again after a restart, and damaged, half-written and outdated slots.
- `conv_check.py` runs the converters of a Galagino checkout (`romconv/`) on the same ROM sets and compares every table they write with the one the emulator builds on the board.
- `zex_host` runs the ZEXDOC and ZEXALL instruction exercisers on the Z80 core; the test programs are not part of this repository.

## Live mode

When SDL2 is installed (`libsdl2-dev`), `build.sh` builds `gal_host` with a live mode. `--live` shows the game in a window, plays the sound and reads the keyboard, in real time. The game argument defaults to `menu`, which starts the game chosen, as on the board, and runs attract mode with 180 seconds per game unless `--rotate` sets another time. Without a frame count the harness runs until the window is closed; frames are written with F12, and every Nth frame when N is given. `--scale N` sets the window size (by default three times the picture, twice with `--tall`) and `--mute` turns the sound off. With `--tate 1` or `--tate 2` the window shows the picture upright; written frames keep the canvas as the board draws it. Scripted input (`--coin`, `--start`, `--keys`, `--play`) is combined with the keyboard, and `--wav` and `--video` record the session. The window title shows the game, the frame number and the measured frame rate. Under WSL2 the window and the sound go through WSLg; `SDL_VIDEODRIVER` and `SDL_AUDIODRIVER` select other drivers.

```sh
hosttest/build.sh
./hosttest/gal_host ~/roms/arcade --live           # the game menu
./hosttest/gal_host ~/roms/arcade GALAGA --live    # straight into a game
```

The keys are those of a USB keyboard on the board:

| Key | Function |
|---|---|
| Arrow keys | Joystick |
| Z, C | Fire |
| X, V | Fire, or the game's second button (the loop in 1942) |
| A | Coin, inserted when the key is released |
| S | 1 player start |
| A + S | Leave the game for the game menu (on the board: SELECT + START, then *Quit game*) |
| 5, 1, 2 | Coin, 1 player start, 2 player start, as in MAME |
| Space | Pause or resume |
| N | While paused: run one frame; held, keep stepping |
| Tab (held) | Fast-forward, without sound |
| F12 | Write the frame on screen to `<outdir>/frame_NNNNN.ppm` |
| Esc | Quit; closing the window or Ctrl-C does the same |
