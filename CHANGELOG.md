# pico-galagino

Arcade games for RP2350 boards with DVI/HDMI output: Pac-Man, Galaga, Donkey Kong, Frogger, Dig Dug, 1942 and more from GalaginoPlus. A port of Galagino by Till Harbaum.

# General Info

[Binaries for each configuration and PCB design are at the end of this page](#downloads___).

[See the readme for the supported boards, the game data and the controls](https://github.com/PicoPlus-devel/pico-galagino#readme)

> [!IMPORTANT]
> An **RP2350** board is required. The original RP2040 (Pico 1) is not supported.

# Changelog

## v0.2

- 41 more games, from GalaginoPlus: **Ms. Pac-Man**, **Pengo**, **Crush Roller**, **Eyes**, **Mr. TNT**, **Lizard Wizard**, **The Glob**, **Ali Baba and 40 Thieves**, **Van-Van Car**, **Donkey Kong Jr.**, **Donkey Kong 3**, **BurgerTime**, **Bump 'n' Jump**, **Vanguard**, **Fantasy**, **Nibbler**, **Galaxian**, **Moon Cresta**, **Scramble**, **Super Cobra**, **Turtles**, **Amidar**, **Anteater**, **Bagman**, **Lady Bug**, **Mr. Do!**, **Space Invaders**, **Phoenix**, **Bomb Jack**, **Pinball Action**, **Star Force**, **Time Pilot**, **Pooyan**, **Tutankham**, **Roc'n Rope**, **Circus Charlie**, **Mappy**, **The Tower of Druaga**, **Gaplus**, **Gyruss** and **Xevious**. The readme lists their ROM sets and folders.
- High scores are saved on the SD card, in `/SAVES/GALAGINO`, and are back the next time a game is played (from GalaginoPlus).
- On boards without PSRAM, the large games (1942, Bomb Jack, Pinball Action, Star Force, Donkey Kong 3, BurgerTime, Bump 'n' Jump, Tutankham, Roc'n Rope, Circus Charlie, The Tower of Druaga, Gaplus, Gyruss, Xevious) are copied into flash the first time they are started instead of at every start-up with new ROM sets: the board shows a notice, restarts once and starts the game, which takes a few seconds. The other games are read from the SD card when they start.
- In the game menu, left and right move three games at a time.
- After quitting a game, the game menu shows the game just played instead of going back to Galaga.
- Attract mode: when the game menu chooses a game at random, it now runs on like a reel to that game and blinks its logo before starting it. After three minutes the menu chooses the next game, so a board left alone shows one game after another. Any button stops this.
- Fixed: after quitting a game, the game menu no longer started a random game after 20 seconds without input.
- Fixed: with *Tate mode* on, the game menu showed a diagonal tear while it scrolled.
- pico-bootLoader's updateAll now installs the ROM sets of all these games. The board no longer creates the game folders.
- The release now includes updateAll.exe, which installs the ROM sets on the SD card from a Windows computer. The readme explains how to use it.

## v0.1.2

- Fixed: 1942 did not always keep up with 60 frames per second, which made its sound stutter. It now runs at full speed with time to spare.
- Fixed: on boards such as the Adafruit Fruit Jam, the sound of all games dropped out briefly every few seconds, over HDMI as well as on the headphone output.
- Improved: on boards with HSTX video output, such as the Adafruit Fruit Jam, the upright picture (*Tate mode* off) now shows every line of the game instead of leaving some out.

## v0.1.1

- Fixed: on boards with PSRAM, such as the Adafruit Fruit Jam, start-up stopped with an "Out of memory" error when the 1942 ROM set was on the SD card as unzipped files, the way updateAll installs it.

## v0.1

Initial release.

- Pac-Man, Galaga, Donkey Kong, Frogger, Dig Dug and 1942 with sound, chosen from a menu of the game logos at start-up.
- The game ROMs are not included. Copy the MAME zip of each game to its own folder under `/roms/arcade` on the SD card, as it is or unzipped; the readme lists the sets and the folders. The menu shows the games that are found.
- On boards without PSRAM, the ROM sets are copied into flash once, at start-up: the first start with new sets takes a few seconds longer with a dark screen. Games already copied can then be played without the SD card.
- **Tate mode** setting: the picture is turned upright for a normal screen, or shown unrotated for a monitor turned on its side, either way round. It applies to the menu as well.
- **Quit game** in the settings menu returns to the game menu.
- A and B both fire, each with its own rapid fire setting; in 1942, B is the loop.
- Settings are shared with the other arcade games of this family, in `/settings_ARC.dat`.
- Can be started from pico-bootLoader (v0.7 and later), where it appears in the Arcade category.

<a name="downloads___"></a>
## Downloads by configuration

The ROM set installer for Windows: [updateAll.exe](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/updateAll.exe)

Binaries for each configuration are listed below. Only RP2350-based boards are supported; no RP2040 / Pico 1 binaries are provided.

Wiring and assembly are the same as for the NES emulator; see the [pico-infonesPlus documentation](https://github.com/PicoPlus-devel/pico-infonesPlus#setup) for the RP2350 boards listed below.

### Standalone boards

| Board | Binary |
|:--|:--|
| Adafruit Metro RP2350 | [picoGalagino_AdafruitMetroRP2350_arm.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_AdafruitMetroRP2350_arm.uf2) |
| Adafruit Fruit Jam | [picoGalagino_AdafruitFruitJam_arm_piousb.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_AdafruitFruitJam_arm_piousb.uf2) |
| Adafruit Feather RP2350 with TLV320DAC3100 | [picoGalagino_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_AdafruitFeatherRP2350_TLV320DAC3100_arm_piousb.uf2) |
| Waveshare RP2350-PiZero | [picoGalagino_WaveShareRP2350PiZero_arm_piousb.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_WaveShareRP2350PiZero_arm_piousb.uf2) |

### Breadboard

| Board | Binary |
|:--|:--|
| Pico 2 | [picoGalagino_AdafruitDVISD_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_AdafruitDVISD_pico2_arm.uf2) |
| Pimoroni Pico Plus 2 | [picoGalagino_AdafruitDVISD_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_AdafruitDVISD_pico2_arm.uf2) |

### PicoNES PCB (PCB required)

| Board | Binary |
|:--|:--|
| Pico 2 / Pico 2 W | [picoGalagino_AdafruitDVISD_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_AdafruitDVISD_pico2_arm.uf2) |
| Pimoroni Pico Plus 2 | [picoGalagino_AdafruitDVISD_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_AdafruitDVISD_pico2_arm.uf2) |

PCB: [pico_nesPCB_v2.6.zip](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/pico_nesPCB_v2.6.zip). [Readme](https://github.com/PicoPlus-devel/pico-infonesPlus#pcb-with-raspberry-pi-pico-or-pico-2-and-pimoroni-pico-plus-2)

3D-printed case: [thingiverse.com/thing:6689537](https://www.thingiverse.com/thing:6689537). When the board is fitted on male headers, use the latest top cover; the older covers assume a Pico soldered flat and leave no room for the USB cable.

### PicoNES Mini PCB, Waveshare RP2350-Zero (PCB required)

| Board | Binary |
|:--|:--|
| Waveshare RP2350-Zero | [picoGalagino_WaveShareRP2350ZeroWithPCB_arm.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_WaveShareRP2350ZeroWithPCB_arm.uf2) |

PCB: [Gerber_PicoNES_Mini_PCB_v2.0.zip](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/Gerber_PicoNES_Mini_PCB_v2.0.zip)

3D-printed case: [thingiverse.com/thing:7041536](https://www.thingiverse.com/thing:7041536)

### PicoNES Micro PCB, Waveshare RP2350-USBA (PCB required)

[Binary](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_WaveShare2350USBA_arm_piousb.uf2)

PCB: [Gerber_PicoNES_Micro_v1.2.zip](https://github.com/PicoPlus-devel/pico-infonesPlus/releases/latest/download/Gerber_PicoNES_Micro_v1.2.zip)

[Build guide](https://www.instructables.com/PicoNES-RaspberryPi-Pico-Based-NES-Emulator/)

### Pimoroni Pico DV

| Board | Binary |
|:--|:--|
| Pico 2 / Pico 2 W | [picoGalagino_PimoroniDVI_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_PimoroniDVI_pico2_arm.uf2) |
| Pimoroni Pico Plus 2 | [picoGalagino_PimoroniDVI_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_PimoroniDVI_pico2_arm.uf2) |

### SpotPear HDMI

For more info about the SpotPear HDMI see https://spotpear.com/index/product/detail/id/1207.html and https://spotpear.com/index/study/detail/id/971.html.

| Board | Binary |
|:--|:--|
| Pico 2 / Pico 2 W | [picoGalagino_SpotpearHDMI_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_SpotpearHDMI_pico2_arm.uf2) |

### Murmulator M1

For more info about the Murmulator see https://murmulator.ru/.

| Board | Binary |
|:--|:--|
| Pico 2 / Pico 2 W | [picoGalagino_MurmulatorM1_pico2_arm.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_MurmulatorM1_pico2_arm.uf2) |

### Murmulator M2

For more info about the Murmulator see https://murmulator.ru/.

| Board | Binary |
|:--|:--|
| Murmulator M2 | [picoGalagino_MurmulatorM2_arm.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_MurmulatorM2_arm.uf2) |

### Olimex RP2040-PICO-PC

| Board | Binary |
|:--|:--|
| Olimex RP2040-PICO-PC with a Pico 2 | [picoGalagino_OlimexPicoPC_arm.uf2](https://github.com/PicoPlus-devel/pico-galagino/releases/latest/download/picoGalagino_OlimexPicoPC_arm.uf2) |
