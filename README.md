# pico-galagino

**pico-galagino** runs 47 arcade games of the late 1970s and early 1980s on RP2350-based microcontroller boards, with video and audio over DVI/HDMI, among them **Pac-Man**, **Galaga**, **Donkey Kong**, **Frogger**, **Dig Dug**, **1942**, **Space Invaders**, **Mr. Do!**, **BurgerTime**, **Time Pilot**, **Mappy** and **Xevious**; [Game data](#game-data) lists them all. It is a port of [Galagino](https://github.com/harbaum/galagino) by Till Harbaum, an emulator of arcade games for the ESP32, and of its fork [GalaginoPlus](https://github.com/VirtualClaudioBoy/GalaginoPlus), with CPU cores from [MAME](https://www.mamedev.org/) and GalaginoPlus. A game is chosen from Galagino's menu of game logos at start-up.

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

The game menu and the 47 games, in the order of [Game data](#game-data), as the emulator draws them, at twice the original size, with *Tate mode* off. For the two tate orientations, see [Display and tate mode](#display-and-tate-mode).

<table>
  <tr>
    <td><img width="320" alt="The game menu: the Frogger, Galaga and Galaxian logos, Galaga selected" src="docs/screenshots/menu.png" /></td>
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
    <td><img width="320" alt="Ms. Pac-Man in the first maze, chased by the ghosts" src="docs/screenshots/mspacman.png" /></td>
    <td><img width="320" alt="Pengo: the penguin in the maze of ice blocks, with the Sno-Bees" src="docs/screenshots/pengo.png" /></td>
  </tr>
  <tr>
    <td align="center">1942</td>
    <td align="center">Ms. Pac-Man</td>
    <td align="center">Pengo</td>
  </tr>
  <tr>
    <td><img width="320" alt="Crush Roller: the paint roller in the maze, part of it painted green" src="docs/screenshots/crush.png" /></td>
    <td><img width="320" alt="Eyes: the maze of dots, with the eyes" src="docs/screenshots/eyes.png" /></td>
    <td><img width="320" alt="Mr. TNT: the maze of paths between the stone blocks" src="docs/screenshots/mrtnt.png" /></td>
  </tr>
  <tr>
    <td align="center">Crush Roller</td>
    <td align="center">Eyes</td>
    <td align="center">Mr. TNT</td>
  </tr>
  <tr>
    <td><img width="320" alt="Lizard Wizard: the wizard on the ground beside the tower, with fireballs falling" src="docs/screenshots/lizwiz.png" /></td>
    <td><img width="320" alt="The Glob: the building with its floors and elevators" src="docs/screenshots/theglob.png" /></td>
    <td><img width="320" alt="Ali Baba and 40 Thieves: the brick maze with Ali Baba and the thieves" src="docs/screenshots/alibaba.png" /></td>
  </tr>
  <tr>
    <td align="center">Lizard Wizard</td>
    <td align="center">The Glob</td>
    <td align="center">Ali Baba and 40 Thieves</td>
  </tr>
  <tr>
    <td><img width="320" alt="Galaxian: the alien convoy above the player's ship" src="docs/screenshots/galaxian.png" /></td>
    <td><img width="320" alt="Moon Cresta: the rocket below a group of Cold Eye aliens" src="docs/screenshots/mooncrst.png" /></td>
    <td><img width="320" alt="Scramble: the ship over the mountains, with a rocket on the ground" src="docs/screenshots/scramble.png" /></td>
  </tr>
  <tr>
    <td align="center">Galaxian</td>
    <td align="center">Moon Cresta</td>
    <td align="center">Scramble</td>
  </tr>
  <tr>
    <td><img width="320" alt="Super Cobra: the helicopter between the ground and the roof of a cave" src="docs/screenshots/scobra.png" /></td>
    <td><img width="320" alt="Turtles: the maze of houses, with the turtle and the beetles" src="docs/screenshots/turtles.png" /></td>
    <td><img width="320" alt="Amidar: the grid of paths with the warriors at the top" src="docs/screenshots/amidar.png" /></td>
  </tr>
  <tr>
    <td align="center">Super Cobra</td>
    <td align="center">Turtles</td>
    <td align="center">Amidar</td>
  </tr>
  <tr>
    <td><img width="320" alt="Anteater: the anteater above the ant hill and its tunnels" src="docs/screenshots/anteater.png" /></td>
    <td><img width="320" alt="Van-Van Car: the maze of roads between the trees" src="docs/screenshots/vanvan.png" /></td>
    <td><img width="320" alt="Lady Bug: the maze with its turnstile gates" src="docs/screenshots/ladybug.png" /></td>
  </tr>
  <tr>
    <td align="center">Anteater</td>
    <td align="center">Van-Van Car</td>
    <td align="center">Lady Bug</td>
  </tr>
  <tr>
    <td><img width="320" alt="Mr. Do!: the garden with its cherries, apples and tunnels" src="docs/screenshots/mrdo.png" /></td>
    <td><img width="320" alt="Bagman: the mine with its ladders and bags of gold" src="docs/screenshots/bagman.png" /></td>
    <td><img width="320" alt="Space Invaders: the rows of invaders above the four shields and the laser base" src="docs/screenshots/invaders.png" /></td>
  </tr>
  <tr>
    <td align="center">Mr. Do!</td>
    <td align="center">Bagman</td>
    <td align="center">Space Invaders</td>
  </tr>
  <tr>
    <td><img width="320" alt="Phoenix: the first wave of birds above the player's ship" src="docs/screenshots/phoenix.png" /></td>
    <td><img width="320" alt="Bomb Jack: Jack among the bombs in front of the Sphinx and a pyramid" src="docs/screenshots/bombjack.png" /></td>
    <td><img width="320" alt="Pinball Action: the pinball table" src="docs/screenshots/pbaction.png" /></td>
  </tr>
  <tr>
    <td align="center">Phoenix</td>
    <td align="center">Bomb Jack</td>
    <td align="center">Pinball Action</td>
  </tr>
  <tr>
    <td><img width="320" alt="Star Force: the player's ship over a space fortress" src="docs/screenshots/starforc.png" /></td>
    <td><img width="320" alt="Donkey Kong Jr.: the vines and platforms, Donkey Kong in his cage at the top" src="docs/screenshots/dkongjr.png" /></td>
    <td><img width="320" alt="Donkey Kong 3: Donkey Kong between the vines of the greenhouse, with the bees" src="docs/screenshots/dkong3.png" /></td>
  </tr>
  <tr>
    <td align="center">Star Force</td>
    <td align="center">Donkey Kong Jr.</td>
    <td align="center">Donkey Kong 3</td>
  </tr>
  <tr>
    <td><img width="320" alt="BurgerTime: the girders and ladders with the parts of the burgers" src="docs/screenshots/btime.png" /></td>
    <td><img width="320" alt="Bump 'n' Jump: the road with the player's car among the other cars" src="docs/screenshots/bnj.png" /></td>
    <td><img width="320" alt="Vanguard: the ship in a cave, with enemy ships and an explosion" src="docs/screenshots/vanguard.png" /></td>
  </tr>
  <tr>
    <td align="center">BurgerTime</td>
    <td align="center">Bump 'n' Jump</td>
    <td align="center">Vanguard</td>
  </tr>
  <tr>
    <td><img width="320" alt="Fantasy: the balloon over the sea and an island" src="docs/screenshots/fantasy.png" /></td>
    <td><img width="320" alt="Nibbler: the snake in the maze" src="docs/screenshots/nibbler.png" /></td>
    <td><img width="320" alt="Time Pilot: the plane among the clouds and the biplanes of 1910" src="docs/screenshots/timeplt.png" /></td>
  </tr>
  <tr>
    <td align="center">Fantasy</td>
    <td align="center">Nibbler</td>
    <td align="center">Time Pilot</td>
  </tr>
  <tr>
    <td><img width="320" alt="Pooyan: the wolves coming down on balloons, the mother pig in the lift on the right" src="docs/screenshots/pooyan.png" /></td>
    <td><img width="320" alt="Tutankham: the explorer in the tomb, with its creatures" src="docs/screenshots/tutankhm.png" /></td>
    <td><img width="320" alt="Roc'n Rope: the cliffs with their ledges, the explorer and the dinosaurs" src="docs/screenshots/rocnrope.png" /></td>
  </tr>
  <tr>
    <td align="center">Pooyan</td>
    <td align="center">Tutankham</td>
    <td align="center">Roc'n Rope</td>
  </tr>
  <tr>
    <td><img width="320" alt="Circus Charlie: Charlie riding the lion towards a ring of fire" src="docs/screenshots/circusc.png" /></td>
    <td><img width="320" alt="Mappy: the mansion with its floors, trampolines, doors and the cats" src="docs/screenshots/mappy.png" /></td>
    <td><img width="320" alt="The Tower of Druaga: Gilgamesh in the maze of the first floor, with the slimes" src="docs/screenshots/todruaga.png" /></td>
  </tr>
  <tr>
    <td align="center">Circus Charlie</td>
    <td align="center">Mappy</td>
    <td align="center">The Tower of Druaga</td>
  </tr>
  <tr>
    <td><img width="320" alt="Gaplus: a formation of aliens above the player's ship" src="docs/screenshots/gaplus.png" /></td>
    <td><img width="320" alt="Gyruss: the ship on its circle, with a wave of enemies coming in" src="docs/screenshots/gyruss.png" /></td>
    <td><img width="320" alt="Xevious: the Solvalou over the forest, with a ground target" src="docs/screenshots/xevious.png" /></td>
  </tr>
  <tr>
    <td align="center">Gaplus</td>
    <td align="center">Gyruss</td>
    <td align="center">Xevious</td>
  </tr>
</table>

***

## GalaginoPlus

pico-galagino follows [GalaginoPlus](https://github.com/VirtualClaudioBoy/GalaginoPlus) by VirtualClaudioBoy, a fork of Galagino with 48 games, and plays 47 of them. Scrambled Egg is left out: its set is not available from the source updateAll downloads from. Phoenix and Moon Cresta use the same folders and sets as [pico-phoenix](https://github.com/PicoPlus-devel/pico-phoenix) and [pico-mooncresta](https://github.com/PicoPlus-devel/pico-mooncresta).

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
| Ms. Pac-Man | `mspacman` | `mspacman.zip` | `/roms/arcade/MSPACMAN` | 13 |
| Pengo | `pengoja` (315-5010 type, rev. C) | `pengo.zip` | `/roms/arcade/PENGO` | 14 |
| Crush Roller | `crush` (set 1) | `crush.zip` | `/roms/arcade/CRUSHROLLER` | 10 |
| Eyes | `eyes` (US, set 1) | `eyes.zip` | `/roms/arcade/EYES` | 10 |
| Mr. TNT | `mrtnt` | `mrtnt.zip` | `/roms/arcade/MRTNT` | 10 |
| Lizard Wizard | `lizwiz` | `lizwiz.zip` | `/roms/arcade/LIZARDWIZARD` | 12 |
| The Glob | `theglobp` (Pac-Man hardware), a clone of `suprglob` | `suprglob.zip` (merged) or `theglobp.zip` | `/roms/arcade/THEGLOB` | 8 |
| Ali Baba and 40 Thieves | `alibaba` | `alibaba.zip` | `/roms/arcade/ALIBABA` | 15 |
| Galaxian | `galaxian` (Namco set 1) | `galaxian.zip` | `/roms/arcade/GALAXIAN` | 8 |
| Moon Cresta | `mooncrst` (Nichibutsu) | `mooncrst.zip` | `/roms/arcade/MOONCRESTA` | 13 |
| Scramble | `scramble` | `scramble.zip` | `/roms/arcade/SCRAMBLE` | 14 |
| Super Cobra | `scobra` | `scobra.zip` | `/roms/arcade/SUPERCOBRA` | 12 |
| Turtles | `turtles` | `turtles.zip` | `/roms/arcade/TURTLES` | 10 |
| Amidar | `amidar1` (older), a clone of `amidar` | `amidar.zip` | `/roms/arcade/AMIDAR` | 9 |
| Anteater | `anteater` | `anteater.zip` | `/roms/arcade/ANTEATER` | 9 |
| Van-Van Car | `vanvan` | `vanvan.zip` | `/roms/arcade/VANVANCAR` | 9 |
| Lady Bug | `ladybug` | `ladybug.zip` | `/roms/arcade/LADYBUG` | 13 |
| Mr. Do! | `mrdo` | `mrdo.zip` | `/roms/arcade/MRDO` | 13 |
| Bagman | `bagmanm2` (on Moon Cresta hardware), a clone of `bagman` | `bagman.zip` (merged) or `bagmanm2.zip` | `/roms/arcade/BAGMAN` | 6 |
| Space Invaders | `invaders` | `invaders.zip` | `/roms/arcade/SPACEINVADERS` | 4 |
| Phoenix | `phoenix` (Amstar, set 1) | `phoenix.zip` | `/roms/arcade/PHOENIX` | 14 |
| Bomb Jack | `bombjack` | `bombjack.zip` | `/roms/arcade/BOMBJACK` | 16 |
| Pinball Action | `pbaction` | `pbaction.zip` | `/roms/arcade/PINBALLACTION` | 14 |
| Star Force | `starforc` | `starforc.zip` | `/roms/arcade/STARFORCE` | 19 |
| Donkey Kong Jr. | `dkongjrj` (Japan), a clone of `dkongjr` | `dkongjr.zip` (merged) or `dkongjrj.zip` | `/roms/arcade/DONKEYKONGJR` | 13 |
| Donkey Kong 3 | `dkong3` | `dkong3.zip` | `/roms/arcade/DONKEYKONG3` | 15 |
| BurgerTime | `btime` (Data East set 1) | `btime.zip` | `/roms/arcade/BURGERTIME` | 15 |
| Bump 'n' Jump | `bnjm` (Midway), a clone of `brubber` | `brubber.zip` (merged) | `/roms/arcade/BUMPNJUMP` | 9 |
| Vanguard | `vanguard` (SNK, set 1) | `vanguard.zip` | `/roms/arcade/VANGUARD` | 14 |
| Fantasy | `fantasyu` (US) | `fantasyu.zip` | `/roms/arcade/FANTASY` | 16 |
| Nibbler | `nibbler6` (rev. 6), a clone of `nibbler` | `nibbler.zip` (merged) | `/roms/arcade/NIBBLER` | 16 |
| Time Pilot | `timeplt` | `timeplt.zip` | `/roms/arcade/TIMEPILOT` | 11 |
| Pooyan | `pooyan` | `pooyan.zip` | `/roms/arcade/POOYAN` | 13 |
| Tutankham | `tutankhm` | `tutankhm.zip` | `/roms/arcade/TUTANKHAM` | 17 |
| Roc'n Rope | `rocnrope` | `rocnrope.zip` | `/roms/arcade/ROCNROPE` | 16 |
| Circus Charlie | `circusc` | `circusc.zip` | `/roms/arcade/CIRCUSCHARLIE` | 18 |
| Mappy | `mappy` (US) | `mappy.zip` | `/roms/arcade/MAPPY` | 11 |
| The Tower of Druaga | `todruaga` (new version) | `todruaga.zip` | `/roms/arcade/TOWEROFDRUAGA` | 10 |
| Gaplus | `galaga3` (Galaga 3), a clone of `gaplus` | `gaplus.zip` (merged) | `/roms/arcade/GAPLUS` | 19 |
| Gyruss | `gyruss` (Konami) | `gyruss.zip` | `/roms/arcade/GYRUSS` | 15 |
| Xevious | `xevious` (Namco) | `xevious.zip` | `/roms/arcade/XEVIOUS` | 26 |

A set can be copied in either form:

- **The zip file as it is.** Merged, split and non-merged sets all work. In a merged `puckman.zip` the Midway Pac-Man files are in the clone subfolders of the archive and its colour and sound PROMs carry the parent's names; they are found all the same. With a split set, copy `pacman.zip` together with `puckman.zip`.
- **The files, unzipped.** A subfolder named after the set (for example `/roms/arcade/GALAGA/galaga`), which unzipping commonly produces, is searched as well.

Files are recognised by their size and CRC32, so their names and their place in an archive do not matter, and a corrupt or different file is never loaded.

**updateAll installs the sets.** `updateAll.exe`, a program for Windows 10 and 11, is included in every release of pico-galagino, among the files at the end of the release page. It runs on a computer with the SD card inserted: it takes the MAME zips from a folder of your choice, downloads those that are missing, and writes the sets, unzipped, into the folders above. It is a single file and needs no installation. The program is not signed, so Windows may show "Windows protected your PC" the first time it is started; select **More info**, then **Run anyway**.

1. Start `updateAll.exe`. It selects a removable drive by itself; under **SD card**, check that it is the card, or choose another.
2. Under **Zip folder**, choose the folder with the MAME zips you already have, if any.
3. Untick the sets that are not wanted. updateAll also installs the sets of other applications of this family, such as pico-outrun and pico-dragonslair. The **Status** column shows which sets are already complete on the card.
4. Select **Install**.

By default, zips that are not in the zip folder are downloaded from [ArcadeROMsDB_MiSTer](https://github.com/zakk4223/ArcadeROMsDB_MiSTer), a third-party database that is not part of this project. The user is responsible for holding the rights to the ROM sets, whether they are supplied as zips or downloaded. To use only your own zips, untick **Download missing zips**.

updateAll is maintained in [pico-bootLoader](https://github.com/PicoPlus-devel/pico-bootLoader), whose SD-card archive carries it as well. The same installer is also available there as a Python script, `updateAll.py`, for Linux and macOS, and as a PowerShell script, `updateAll.ps1`. The [updateAll README](https://github.com/PicoPlus-devel/pico-bootLoader/blob/main/updateAll/README.md) describes all three and their options. The board does not create the folders itself.

The game menu lists the games whose sets are complete. When no set is found, the board shows a screen that explains where the sets go. **SELECT + START** opens the settings menu from that screen; its *USB drive mode* shows the SD card on a computer, so the sets can be copied to it without removing the card. After leaving the menu, the board restarts and looks for the sets again.

Like Galagino, the emulator converts the graphics, colour and sound PROMs into its own formats; it does so on the board when a game starts, which takes a fraction of a second. Clones and bootlegs other than the sets listed are not supported.

**Large games run from PSRAM, or from flash.** A game is read from the card each time it starts. The data of a large game (1942, Bomb Jack, Pinball Action, Star Force, Donkey Kong 3, BurgerTime, Bump 'n' Jump, Tutankham, Roc'n Rope, Circus Charlie, The Tower of Druaga, Gaplus, Gyruss, Xevious) does not fit in the internal SRAM: on boards with PSRAM it is kept there; boards without PSRAM keep it in their flash memory instead. The board copies such a set into flash the first time the game is started: it shows a notice, restarts, copies the set with a dark screen and a blinking LED, which takes a few seconds, and starts the game. Later starts use the copy, also without the SD card. Attract mode does the same when it chooses such a game, and carries on after the restart. Under pico-bootLoader, the board returns to picoGalagino after the restart. When the flash is full, the copy made longest ago makes room. The flash used is everything after the program, as large as the flash chip itself reports, not only the size the board type usually carries.

***

## Supported hardware

pico-galagino runs on every RP2350 configuration of this family, with both video back-ends. RP2040 boards are not supported.

All the games run on every board. The converted graphics of 1942 take about 230 KB, more than the internal SRAM leaves free: on boards with PSRAM (for example the Adafruit Fruit Jam) they are kept there, and on boards without PSRAM in a copy in flash, made the first time the game is started (see [Game data](#game-data)).

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

In the game menu, **up** and **down** select a game, **left** and **right** move three games at a time, and **A** or **START** starts it. After quitting a game, the menu shows the game that was just played.

**Attract mode.** When nothing is pressed for 20 seconds, a game is chosen at random, as in Galagino: the menu runs on like a reel, slows down and stops at that game, whose logo blinks before the game starts. The game runs its own demonstration for three minutes; then the menu returns and chooses another game the same way, so a board left alone shows one game after another. Pressing any button stops this: in a game, the game goes on as a normal game; in the menu, the selection stays where the reel stopped. Attract mode starts again the next time the game menu is shown.

In a game:

| Controller | Function |
| --- | --- |
| D-pad | Joystick |
| A (or X) | Fire (Galaga, Dig Dug: pump, 1942, Space Invaders, Phoenix, Star Force, Fantasy, Donkey Kong 3: spray, Time Pilot, Pooyan, Gaplus, Gyruss, Xevious: the zapper); fire in each direction in turn (Vanguard); fire left and right (Tutankham); pepper (BurgerTime); jump (Donkey Kong, Donkey Kong Jr., Bomb Jack, Bump 'n' Jump, Circus Charlie); push ice (Pengo); throw the ball (Mr. Do!); open doors (Mappy); the sword (The Tower of Druaga); the harpoon (Roc'n Rope); the game's button (Eyes, Mr. TNT, Lizard Wizard, Ali Baba, Van-Van Car, Bagman); act (The Glob); right flipper (Pinball Action) |
| B (or Y) | The same as A; in 1942 the loop; in The Glob the elevator; in Scramble and Super Cobra the bombs; in Phoenix the shield; in Pinball Action the left flipper; in Tutankham the flash bomb; in Roc'n Rope the flash; in Xevious the blaster (bombs) |
| SELECT | Insert a coin (on release) |
| START | 1 player start |
| START on a second USB controller | 2 player start |
| START + A | Frame rate display on/off |
| SELECT + START | Settings menu |

Pac-Man, Ms. Pac-Man, Crush Roller, Frogger, Lady Bug and Nibbler have no button besides the joystick. In Pinball Action, left and right work the flippers as well, down shoots the ball and up nudges the table. Two players take turns on the same controls.

Bomb Jack checks its ROMs and RAM for about 20 seconds after it starts, Xevious for about 10, as the arcade board does, and takes coins only after that. The sound effects of Vanguard, Fantasy and Nibbler (made by SN76477 chips and discrete circuits) and their speech are not emulated; their music is. Neither are the explosion sounds of Gaplus, which its arcade board plays from a recording, and of Xevious, the starfield of Tutankham and the filters of the Konami sound boards.

USB game controllers, NES and SNES controllers on the GPIO ports and the Wii Classic controller are supported, as in the other emulators of this family. A and B both press the fire button. Each has its own rapid fire setting, so one of them can fire single shots and the other fire automatically. The coin is inserted when SELECT is released, and only when no other button was pressed while it was held, so opening the settings menu does not insert a coin. While START is held, A switches the frame rate display (top left, in the border) on or off instead of firing.

The DIP switches have Galagino's settings for its six games: an upright cabinet, 1 coin for 1 credit, three lives (Pac-Man, Galaga, Donkey Kong, Frogger, Dig Dug) or two (1942), and the default bonus lives. The other games have MAME's default settings, which are much the same: 1 coin for 1 credit and three lives.

***

## Display and tate mode

All the games were made for a monitor mounted on its side. Pac-Man and the other games on its board, Galaga and Dig Dug have a picture 224 pixels wide and 288 pixels tall; Donkey Kong, Frogger, 1942 and the games of the Galaxian board (Galaxian, Moon Cresta, Scramble, Super Cobra, Turtles, Amidar, Anteater) one 224 pixels wide and 256 pixels tall. The *Tate mode* setting chooses how it is shown:

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

The settings are stored on the SD card in `/settings_ARC.dat`, a file shared by all arcade games of this family. The games have no save states.

***

## High scores

As in GalaginoPlus, the high score table of each game is kept on the SD card, in `/SAVES/GALAGINO/<folder>.HI` (for example `/SAVES/GALAGINO/GALAGA.HI`), and put back into the game the next time it is played. The tables are those of MAME's `hiscore.dat`. The board checks the table every five seconds and writes the file when it has changed, and again before the game is reset, left or the settings menu is opened. Deleting a file resets that game's table.

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

Every configuration must be built with `-2` (RP2350).

`EXTRA_CMAKE_ARGS=-DGALAGINO_ATTRACT_SECONDS=N` sets how long a game started by attract mode runs before the next one is chosen (default 180; 0 lets it run until a button is pressed, as in Galagino).

Two build options help to measure the speed of a game on the board:

- `EXTRA_CMAKE_ARGS=-DGALAGINO_FRAME_STATS=1 ./bld.sh -c 8 -2` prints over the serial console, every five seconds, the time spent emulating and drawing a frame, the time left before the display needs the next frame, the number of frames that came too late, and the time of each CPU of the games that count it. The frame rate display on the screen shows the length of a single frame, so it varies by a few frames per second even when no frame is late.
- `-DGALAGINO_AUTOSTART=GAL_GAME_<NAME>` (with the option above, for example `EXTRA_CMAKE_ARGS="-DGALAGINO_FRAME_STATS=1 -DGALAGINO_AUTOSTART=GAL_GAME_GYRUSS"`) starts that game instead of the menu, inserts a coin, starts it and plays it with fixed inputs, so that it can be measured without anyone at the board. The names are in `galagino/galagino.h`.

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

#### Live mode

When SDL2 is installed (`libsdl2-dev`), `build.sh` builds `gal_host` with a live mode. `--live` shows the game in a window, plays the sound and reads the keyboard, in real time. The game argument defaults to `menu`, which starts the game chosen, as on the board, and runs attract mode with 180 seconds per game unless `--rotate` sets another time. Without a frame count the harness runs until the window is closed; frames are written with F12, and every Nth frame when N is given. `--scale N` sets the window size (by default three times the picture, twice with `--tall`) and `--mute` turns the sound off. With `--tate 1` or `--tate 2` the window shows the picture upright; written frames keep the canvas as the board draws it. Scripted input (`--coin`, `--start`, `--keys`, `--play`) is combined with the keyboard, and `--wav` and `--video` record the session. The window title shows the game, the frame number and the measured frame rate. Under WSL2 the window and the sound go through WSLg; `SDL_VIDEODRIVER` and `SDL_AUDIODRIVER` select other drivers.

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

***

## Credits and licence

- [GalaginoPlus](https://github.com/VirtualClaudioBoy/GalaginoPlus): VirtualClaudioBoy, under the GNU General Public License v3.0. The high score support and its tables, the ROM sets and logos of its games, the recorded Space Invaders shot and hit sounds and Donkey Kong fall sound, and their emulation, with MAME's drivers as the reference for the hardware (memory maps, decryption, colours), are taken or ported from it.
- [Galagino](https://github.com/harbaum/galagino): Till Harbaum. The emulation of the six games, their ROM converters, the menu and its logos, the Galaga star sets and the recorded explosion, walk, jump and stomp sounds are taken or ported from it. The Intel 8048 core of the Donkey Kong sound CPU is derived by Galagino from [asig/kosmos-cp1](https://github.com/asig/kosmos-cp1). These parts are under the GNU General Public License v3.0.
- [pico-phoenix](https://github.com/PicoPlus-devel/pico-phoenix) and the MAME project (i8085.cpp, tms36xx.cpp, phoenix.cpp, phoenix_v.cpp, phoenix_a.cpp: Juergen Buchmueller, Roberto Fresca, Grull Osgo, Richard Davies, Derrick Renaud, K. Wilkins), under the BSD-3-Clause licence: the 8085 CPU core, which also runs Space Invaders, and the Phoenix machine, sound and melody chip.
- The 6502 CPU core of BurgerTime, Bump 'n' Jump, Vanguard, Fantasy, Nibbler and the Donkey Kong 3 sound CPUs, the 6809 CPU core of Tutankham, Roc'n Rope, Circus Charlie, Mappy, The Tower of Druaga, Gaplus and Gyruss, and the NES APU of Donkey Kong 3, are GalaginoPlus's.
- The MAME project, under the BSD-3-Clause licence, as the reference for the other boards: Data East's BurgerTime and Bump 'n' Jump with their DECO CPU-7 and C10707 decryption (David Haywood), SNK's Vanguard, Fantasy and Nibbler with their tone generator, Nintendo's Donkey Kong 3, Universal's Lady Bug and Mr. Do! (Nicola Salmoria), Nintendo's Donkey Kong Jr. (its sound CPU's connections), Midway's Space Invaders, Tehkan's Bomb Jack (Mirko Buffoni), Pinball Action and Star Force (Nicola Salmoria), Konami's Time Pilot, Pooyan, Tutankham, Roc'n Rope, Circus Charlie and Gyruss with their KONAMI-1 decryption and sound board, Namco's Mappy, The Tower of Druaga and Gaplus with their custom I/O chips and 15XX sound chip, Namco's Xevious with its background map ROMs and 06XX interface, and the SN76496 sound chip.
- [pico-mooncresta](https://github.com/PicoPlus-devel/pico-mooncresta) and the MAME project (galaxian.cpp, galaxian_v.cpp, galaxian_a.cpp: Aaron Giles, Couriersud, Stephane Humbert, Robbbert, K. Wilkins, Derrick Renaud), under the BSD-3-Clause licence: the Galaxian hardware and the discrete sound of Galaxian and Moon Cresta.
- Z80 CPU core: the MAME project, Juergen Buchmueller and Andrei I. Holub, under the BSD-3-Clause licence. See [galagino/LICENSE](galagino/LICENSE) for the origin of every file.
- miniz (inflate and CRC32): Rich Geldreich and others; see the licence text in [third_party/miniz/miniz.c](third_party/miniz/miniz.c).
- updateAll, the ROM set installer included in the releases as `updateAll.exe`: [pico-bootLoader](https://github.com/PicoPlus-devel/pico-bootLoader/tree/main/updateAll), under the GNU General Public License v3.0, built with miniz and cJSON (MIT licence). Its source is in that repository.
- Framework, menu, display, audio and controller support: [pico_shared](https://github.com/PicoPlus-devel/pico_shared) and the sister projects listed above.

pico-galagino is distributed under the GNU General Public License v3.0; see [LICENSE](LICENSE).

Pac-Man is copyright 1980 Namco (Midway in North America), Galaga 1981 and Dig Dug 1982 Namco, Donkey Kong 1981 Nintendo, Frogger 1981 Konami (Sega/Gremlin), and 1942 1984 Capcom. The other games are copyright their respective owners. This project is not affiliated with these companies, with Galagino, GalaginoPlus or the MAME project.
