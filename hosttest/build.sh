#!/usr/bin/env bash
# Build the host harnesses:
#   hosttest/gal_host    - the Galagino core (games, menu, video, sound, ROM loader)
#   hosttest/conv_check  - dumps the converted ROM data for hosttest/conv_check.py
#   hosttest/zex_host    - the Z80 core on the ZEXDOC/ZEXALL instruction tests
#   hosttest/flash_test  - the flash cache on its own, with fake games
#
# The core in galagino/ has no Pico dependencies; this compiles it for Linux
# with ASan. Drop -fsanitize=address (and use -O2) for timing measurements:
#   OPT="-O2" hosttest/build.sh
set -euo pipefail
cd "$(dirname "$0")/.."

OPT=${OPT:--O1 -g -fsanitize=address -fno-omit-frame-pointer}
CFLAGS=(
  $OPT -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-parentheses -Wno-sign-compare -Wno-ignored-qualifiers
  -D_DEFAULT_SOURCE
  -I galagino -I third_party/miniz
  -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_DEFLATE_APIS
  -DMINIZ_NO_ZLIB_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
)

CORE=(
  galagino/z80.c
  galagino/gal_core.c
  galagino/gal_games.c
  galagino/gal_gfx.c
  galagino/gal_sound.c
  galagino/gal_menu.c
  galagino/game_pacman.c
  galagino/game_galaga.c
  galagino/game_frogger.c
  galagino/game_dkong.c
  galagino/game_digdug.c
  galagino/game_1942.c
  galagino/game_galaxian.c
  galagino/game_universal.c
  galagino/game_phoenix.c
  galagino/game_invaders.c
  galagino/game_bombjack.c
  galagino/game_pbaction.c
  galagino/game_starforce.c
  galagino/game_btime.c
  galagino/game_snk6502.c
  galagino/game_timeplt.c
  galagino/konami_snd.c
  galagino/m6809.c
  galagino/game_tutankhm.c
  galagino/game_circusc.c
  galagino/game_mappy.c
  galagino/gal_namcoio.c
  galagino/game_gaplus.c
  galagino/game_gyruss.c
  galagino/game_xevious.c
  galagino/m6502.c
  galagino/i8085.c
  galagino/phoenix.c
  galagino/phoenix_sound.c
  galagino/tms36xx.c
  galagino/mooncresta_sound.c
  galagino/gal_flash.c
  galagino/gal_hiscore.c
  galagino/i8048.c
  galagino/romload.c
  third_party/miniz/miniz.c
)

gcc "${CFLAGS[@]}" -I pico_shared/drivers/pico_hdmi hosttest/gal_host.c "${CORE[@]}" -lm -o hosttest/gal_host
gcc "${CFLAGS[@]}" -DGAL_CONV_CHECK hosttest/conv_check.c "${CORE[@]}" -lm -o hosttest/conv_check
gcc "${CFLAGS[@]}" -I hosttest hosttest/zex_host.c galagino/z80.c -o hosttest/zex_host
# gal_flash.c reads the fake game table of the test
gcc "${CFLAGS[@]}" -Dgal_games=fake_games -c galagino/gal_flash.c -o hosttest/flash_test_gal_flash.o
gcc "${CFLAGS[@]}" hosttest/flash_test.c hosttest/flash_test_gal_flash.o third_party/miniz/miniz.c -o hosttest/flash_test
rm -f hosttest/flash_test_gal_flash.o
echo "built hosttest/gal_host, conv_check, zex_host and flash_test"
