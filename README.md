# pico-galagino

**pico-galagino** runs six arcade games of the early 1980s on RP2350-based microcontroller boards, with video and audio over DVI/HDMI: **Pac-Man**, **Galaga**, **Donkey Kong**, **Frogger**, **Dig Dug** and **1942**. It is a port of [Galagino](https://github.com/harbaum/galagino) by Till Harbaum, an emulator of these games for the ESP32, with the Z80 CPU core of [MAME](https://www.mamedev.org/). A game is chosen from Galagino's menu of game logos at start-up.

It uses the same menu, display, audio and controller framework as this family of emulators:

- NES: [pico-infonesPlus](https://github.com/PicoPlus-devel/pico-infonesPlus)
- Super Nintendo: [pico-snesPlus](https://github.com/PicoPlus-devel/pico-snesPlus)
- Sega Master System / Game Gear: [pico-smsplus](https://github.com/PicoPlus-devel/pico-smsplus)
- Game Boy / Game Boy Color: [pico-peanutGB](https://github.com/PicoPlus-devel/pico-peanutGB)
- Sega Mega Drive / Genesis: [pico-genesisPlus](https://github.com/PicoPlus-devel/pico-genesisPlus)
- OutRun: [pico-outrun](https://github.com/PicoPlus-devel/pico-outrun)
- Phoenix: [pico-phoenix](https://github.com/PicoPlus-devel/pico-phoenix)
- Moon Cresta: [pico-mooncresta](https://github.com/PicoPlus-devel/pico-mooncresta)

pico-galagino runs standalone, or as an application of [pico-bootLoader](https://github.com/PicoPlus-devel/pico-bootLoader) (v0.7 and later), where it appears in the **Arcade** category.

**The ROM sets of the games are not included and must be supplied by the user.** They are copyright of their manufacturers and are not distributed with this project. See [Game data](#game-data).

***

## Screenshots

The games as the emulator draws them, at twice the original size, with *Tate mode* off. For the two tate orientations, see [Display and tate mode](#display-and-tate-mode).

<table>
  <tr>
    <td><img width="320" alt="The game menu: the Pac-Man, Galaga and Donkey Kong logos, Galaga selected" src="docs/screenshots/menu.png" /></td>
    <td><img width="320" alt="Pac-Man in the maze, chased by the four ghosts" src="docs/screenshots/pacman.png" /></td>
    <td><img width="320" alt="Galaga: the fighter below a formation of aliens in a starfield" src="docs/screenshots/galaga.png" /></td>
  </tr>
  <tr>
    <td align="center">The game menu</td>
    <td align="center">Pac-Man</td>
    <td align="center">Galaga</td>
  </tr>
  <tr>
    <td><img width="320" alt="Donkey Kong: the girders, Donkey Kong with his barrels, Pauline and Mario" src="docs/screenshots/dkong.png" /></td>
    <td><img width="320" alt="Frogger: the river with logs and turtles above the road with its traffic" src="docs/screenshots/frogger.png" /></td>
    <td><img width="320" alt="Dig Dug: tunnels through the layers of earth, with Pookas and Fygars" src="docs/screenshots/digdug.png" /></td>
  </tr>
  <tr>
    <td align="center">Donkey Kong</td>
    <td align="center">Frogger</td>
    <td align="center">Dig Dug</td>
  </tr>
  <tr>
    <td><img width="320" alt="1942: the player's plane on the deck of the aircraft carrier" src="docs/screenshots/1942.png" /></td>
    <td></td>
    <td></td>
  </tr>
  <tr>
    <td align="center">1942</td>
    <td></td>
    <td></td>
  </tr>
</table>

***

## Game data

Each game needs one MAME ROM set, the same set Galagino is built from. Copy each set to its folder on the SD card:

| Game | MAME set | Zip file | Folder | Files |
| --- | --- | --- | --- | --- |
| Pac-Man | `pacman` (Midway), a clone of `puckman` | `puckman.zip` (merged) or `pacman.zip` | `/roms/arcade/PACMAN` | 10 |
| Galaga | `galaga` (Namco rev. B) | `galaga.zip` | `/roms/arcade/GALAGA` | 13 |
| Donkey Kong | `dkong` (US set 1) | `dkong.zip` | `/roms/arcade/DONKEYKONG` | 15 |
| Frogger | `frogger` | `frogger.zip` | `/roms/arcade/FROGGER` | 9 |
| Dig Dug | `digdug` (rev 2) | `digdug.zip` | `/roms/arcade/DIGDUG` | 19 |
| 1942 | `1942` (Revision B) | `1942.zip` | `/roms/arcade/1942` | 25 |

A set can be copied in either form:

- **The zip file as it is.** Merged, split and non-merged sets all work. In a merged `puckman.zip` the Midway Pac-Man files are in the clone subfolders of the archive and its colour and sound PROMs carry the parent's names; they are found all the same. With a split set, copy `pacman.zip` together with `puckman.zip`.
- **The files, unzipped.** A subfolder named after the set (for example `/roms/arcade/GALAGA/galaga`), which unzipping commonly produces, is searched as well.

Files are recognised by their size and CRC32, so their names and their place in an archive do not matter, and a corrupt or different file is never loaded. The board creates the six folders on first start.

The game menu lists the games whose sets are complete. When no set is found, the board shows a screen that names the zip files and their folders. **SELECT + START** opens the settings menu from that screen; its *USB drive mode* shows the SD card on a computer, so the sets can be copied to it without removing the card. After leaving the menu, the board restarts and looks for the sets again.

Like Galagino, the emulator converts the graphics, colour and sound PROMs into its own formats; it does so on the board when a game starts, which takes a fraction of a second. Clones, bootlegs and the other games of the Galagino forks are not supported.

**Boards without PSRAM copy the sets into their flash.** At start-up, before the picture appears, the board copies every complete set it finds on the card into its flash memory, once: a set already there is not copied again. The first start with new sets therefore takes a few seconds longer, with a dark screen and a blinking LED; later starts only check the copies. The games then start from flash, and those already copied can be played without the SD card. Boards with PSRAM read the sets from the card when a game starts.

***

## Supported hardware

pico-galagino runs on every RP2350 configuration of this family, with both video back-ends. RP2040 boards are not supported.

All six games run on every board. The converted graphics of 1942 take about 230 KB, more than the internal SRAM leaves free: on boards with PSRAM (for example the Adafruit Fruit Jam) they are kept there, and on boards without PSRAM in the flash copy of the sets (see [Game data](#game-data)).

| HW_CONFIG | Hardware | Video | Binary |
| --- | --- | --- | --- |
| 1 | Pimoroni Pico DV Demo Base with a Raspberry Pi Pico 2 | PicoDVI | `picoGalagino_PimoroniDVI_pico2_arm.uf2` |
| 2 | Adafruit DVI Breakout and microSD breakout with a Raspberry Pi Pico 2, or the PicoNES PCB | HSTX | `picoGalagino_AdafruitDVISD_pico2_arm.uf2` |
| 5 | Adafruit Metro RP2350 | HSTX | `picoGalagino_AdafruitMetroRP2350_arm.uf2` |
| 6 | Waveshare RP2350-Zero with custom PCB | PicoDVI | `picoGalagino_WaveShareRP2350ZeroWithPCB_arm.uf2` |
| 7 | Waveshare RP2350-PiZero | PicoDVI | `picoGalagino_WaveShareRP2350PiZero_arm_piousb.uf2` |
| 8 | Adafruit Fruit Jam | HSTX | `picoGalagino_AdafruitFruitJam_arm_piousb.uf2` |
| 9 | Waveshare RP2350-USB-A | PicoDVI | `picoGalagino_WaveShare2350USBA_arm_piousb.uf2` |
| 10 | Spotpear HDMI with a Raspberry Pi Pico 2 | PicoDVI | `picoGalagino_SpotpearHDMI_pico2_arm.uf2` |
| 12 | Murmulator M1 with a Raspberry Pi Pico 2 | PicoDVI | `picoGalagino_MurmulatorM1_pico2_arm.uf2` |
| 13 | Murmulator M2 | HSTX | `picoGalagino_MurmulatorM2_arm.uf2` |
| 14 | Adafruit Feather RP2350 with HSTX Port and TLV320DAC3100 I2S DAC | HSTX | `picoGalagino_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2` |
| 15 | Olimex RP2040-PICO-PC with a Raspberry Pi Pico 2 | HSTX | `picoGalagino_OlimexPicoPC_arm.uf2` |

For wiring and assembly instructions, see the setup sections of the [pico-infonesPlus README](https://github.com/PicoPlus-devel/pico-infonesPlus#setup). To flash a board, hold BOOTSEL while connecting it over USB, then copy the `.uf2` file onto the USB drive that appears.

Audio is sent over HDMI. Boards with an I2S DAC (configurations 8, 13 and 14) can use it instead through the *External Audio* setting; on the Fruit Jam, plugging in headphones selects it automatically. Configuration 15 also plays the sound on its PWM audio jack.

***

## Controls

In the game menu, **up** and **down** select a game and **A** or **START** starts it. When nothing is pressed for 20 seconds, a game is chosen at random, as in Galagino; any button switches this off until the board restarts.

In a game:

| Controller | Function |
| --- | --- |
| D-pad | Joystick |
| A (or X) | Fire (Galaga, Dig Dug: pump, 1942); jump (Donkey Kong) |
| B (or Y) | The same as A; in 1942 the loop |
| SELECT | Insert a coin (on release) |
| START | 1 player start |
| START on a second USB controller | 2 player start |
| START + A | Frame rate display on/off |
| SELECT + START | Settings menu |

Pac-Man and Frogger have no button besides the joystick. Two players take turns on the same controls.

USB game controllers, NES and SNES controllers on the GPIO ports and the Wii Classic controller are supported, as in the other emulators of this family. A and B both press the fire button. Each has its own rapid fire setting, so one of them can fire single shots and the other fire automatically. The coin is inserted when SELECT is released, and only when no other button was pressed while it was held, so opening the settings menu does not insert a coin. While START is held, A switches the frame rate display (top left, in the border) on or off instead of firing.

The DIP switches have Galagino's settings: an upright cabinet, 1 coin for 1 credit, three lives (Pac-Man, Galaga, Donkey Kong, Frogger, Dig Dug) or two (1942), and the default bonus lives.

***

## Display and tate mode

All six games were made for a monitor mounted on its side. Pac-Man, Galaga and Dig Dug have a picture 224 pixels wide and 288 pixels tall; Donkey Kong, Frogger and 1942 one 224 pixels wide and 256 pixels tall. The *Tate mode* setting chooses how it is shown:

| Tate mode | Picture | For |
| --- | --- | --- |
| Off (default) | Turned upright and shown in the centre of the screen. On boards with HSTX video output, all 288 or 256 lines are scaled to the height of the screen; a screen line that falls between two lines of the picture shows a mix of both. On boards with PicoDVI output, the lines are fitted into the 240 of the screen by leaving out every sixth or every sixteenth line. | A normal monitor or TV |
| Bottom left | The original picture, unrotated and pixel exact. | A monitor turned 90 degrees clockwise, so that its bottom edge is on the left |
| Bottom right | The same picture, rotated by 180 degrees. | A monitor turned 90 degrees counter-clockwise, so that its bottom edge is on the right |

The setting applies to the game menu as well, takes effect immediately and is saved with the other settings. The settings menu itself is not rotated.

<table>
  <tr>
    <td><img width="320" alt="Tate mode Bottom left: Galaga shown unrotated, the score along the left edge" src="docs/screenshots/tate-bottom-left.png" /></td>
    <td><img width="320" alt="Tate mode Bottom right: the same picture rotated by 180 degrees, the score along the right edge" src="docs/screenshots/tate-bottom-right.png" /></td>
  </tr>
  <tr>
    <td align="center">Bottom left</td>
    <td align="center">Bottom right</td>
  </tr>
</table>

Both pictures appear upright once the monitor is turned as described in the table above.

***

## Settings menu

**SELECT + START** opens the settings menu. Besides *Tate mode*, it offers the screen mode with or without scanlines, the FPS overlay, audio on/off, rapid fire on A and on B, the external audio output, the Fruit Jam volume and VU meter, the controller test, BOOTSEL mode and USB drive mode. In a game it also offers *Reset game*, and *Quit game*, which returns to the game menu. When started from pico-bootLoader, it also offers *Return to emulator selection*.

The settings are stored on the SD card in `/settings_ARC.dat`, a file shared by all arcade games of this family. The games have no save states, and high scores are not kept after a reset or power cycle.

***

## Building from source

Requirements: the Raspberry Pi Pico SDK (`PICO_SDK_PATH`), Pico-PIO-USB (`PICO_PIO_USB_PATH`) for the configurations that use it, the `arm-none-eabi` toolchain and `picotool`.

```sh
git clone --recursive https://github.com/PicoPlus-devel/pico-galagino.git
cd pico-galagino
./bld.sh -c 8 -2        # one configuration: HW_CONFIG 8 (Fruit Jam)
./bld.sh -b -c 8 -2     # the same, built for pico-bootLoader (releases_bl/)
./buildAll.sh           # every supported configuration (releases/)
./bld.sh -h             # all options
```

Every configuration must be built with `-2` (RP2350). `EXTRA_CMAKE_ARGS=-DGALAGINO_FRAME_STATS=1 ./bld.sh -c 8 -2` builds a version that prints the time spent emulating and drawing a frame over the serial console every five seconds.

The instruction loop of the Z80 core, `galagino/z80_ops.h`, is generated from MAME's opcode list `galagino/z80.lst` and is checked in, so the build does not need Python. After a change to the list, regenerate it with `tools/z80gen.py galagino/z80.lst galagino/z80_ops.h`. The built-in screensaver image (`DefaultSS444.c`, `DefaultSS555.c`) is a picture of the game menu, made by `tools/mkdefaultss.py` from a frame of the host harness.

The pico-bootLoader menu tiles are built from the same pictures: `tools/capture_footage.sh` takes the game menu and a frame of Pac-Man, Galaga and Donkey Kong from your own ROM sets with the host harness, and `tools/mkartwork.py` lays them out in the bootloader's two themes. Afterwards, run the bootloader's `tools/png2raw.py` on the two tiles to rebuild their `.444`/`.555` caches.

```sh
hosttest/build.sh
tools/capture_footage.sh ~/roms/arcade /tmp/gal_footage
tools/mkartwork.py --footage /tmp/gal_footage --bootloader ../pico-bootLoader/emu/assets
```

### Host test harness

The emulator in `galagino/` is plain C without Pico dependencies, and also builds for Linux:

```sh
hosttest/build.sh
./hosttest/gal_host ~/roms/arcade GALAGA 2400 300 hosttest/out --coin 700 --start 800 --play 900 --wav hosttest/out/game.wav
./hosttest/gal_host ~/roms/arcade menu 120 30 hosttest/out
python3 hosttest/ppm2png.py hosttest/out/frame_01200.ppm
hosttest/conv_check.py ~/roms/arcade path/to/galagino /tmp/conv_check
./hosttest/zex_host zexdoc.com zexall.com
```

- `gal_host` takes the folder that holds the game folders, as on the SD card, and a game (its folder name) or `menu`. It loads the set through the same loader as the firmware, runs the given number of frames with scripted input, writes every Nth frame as a 320 x 240 PPM in the chosen orientation (`--tate 0|1|2`) and the sound as a WAV file. `--tall` writes instead the 640 x 480 picture that a board with HSTX video output shows with *Tate mode* off. `--no-psram` runs it as a board without PSRAM, and `--flash FILE` with a flash copy of the sets, kept in FILE between runs.
- `conv_check.py` runs the converters of a Galagino checkout (`romconv/`) on the same ROM sets and compares every table they write with the one the emulator builds on the board.
- `zex_host` runs the ZEXDOC and ZEXALL instruction exercisers on the Z80 core; the test programs are not part of this repository.

***

## Credits and licence

- [Galagino](https://github.com/harbaum/galagino): Till Harbaum. The emulation of the six games, their ROM converters, the menu and its logos, the Galaga star sets and the recorded explosion, walk, jump and stomp sounds are taken or ported from it. The Intel 8048 core of the Donkey Kong sound CPU is derived by Galagino from [asig/kosmos-cp1](https://github.com/asig/kosmos-cp1). These parts are under the GNU General Public License v3.0.
- Z80 CPU core: the MAME project, Juergen Buchmueller and Andrei I. Holub, under the BSD-3-Clause licence. See [galagino/LICENSE](galagino/LICENSE) for the origin of every file.
- miniz (inflate and CRC32): Rich Geldreich and others; see the licence text in [third_party/miniz/miniz.c](third_party/miniz/miniz.c).
- Framework, menu, display, audio and controller support: [pico_shared](https://github.com/PicoPlus-devel/pico_shared) and the sister projects listed above.

pico-galagino is distributed under the GNU General Public License v3.0; see [LICENSE](LICENSE).

Pac-Man is copyright 1980 Namco (Midway in North America), Galaga 1981 and Dig Dug 1982 Namco, Donkey Kong 1981 Nintendo, Frogger 1981 Konami (Sega/Gremlin), and 1942 1984 Capcom. This project is not affiliated with these companies, with Galagino or with the MAME project.
