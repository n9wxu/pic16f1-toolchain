#!/bin/sh
# Build avrrun, which runs a sketch's .hex on a simulated ATmega328P, from
# the sources of simavr (https://github.com/buserror/simavr, GPL 3; fetched
# here, not kept in this repository). It needs the AVR headers that come
# with the Arduino IDE's "Arduino AVR Boards".
#
# Usage: tools/arduino-bench/build-avrrun.sh <work directory>
#   The program is <work directory>/avrrun.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work=$1
mkdir -p "$work"
[ -d "$work/simavr/.git" ] || git clone -q --depth 1 https://github.com/buserror/simavr.git "$work/simavr"
inc=$(ls -d "$HOME"/Library/Arduino15/packages/arduino/tools/avr-gcc/*/avr/include \
            "$HOME"/.arduino15/packages/arduino/tools/avr-gcc/*/avr/include 2>/dev/null | head -1)
[ -n "$inc" ] || { echo "build-avrrun: no AVR headers (install Arduino AVR Boards)" >&2; exit 1; }
cd "$work/simavr/simavr"
# What simavr's Makefile would generate, for the one core that is needed.
cat > sim_core_config.h <<'H'
#ifndef __SIM_CORE_CONFIG_H__
#define __SIM_CORE_CONFIG_H__
#define CONFIG_SIMAVR_VERSION "local"
#define CONFIG_MEGA328 1
#endif
H
cat > sim_core_decl.h <<'H'
#ifndef __SIM_CORE_DECL_H__
#define __SIM_CORE_DECL_H__
#include "sim_core_config.h"
extern avr_kind_t mega328;
extern avr_kind_t * avr_kind[];
#ifdef AVR_KIND_DECL
avr_kind_t * avr_kind[] = { &mega328, NULL };
#endif
#endif
H
# shellcheck disable=SC2046
cc -O2 -std=gnu99 -I. -Isim -Icores -idirafter "$inc" -o "$work/avrrun" "$here/avrrun.c" \
    $(ls sim/*.c | grep -v run_avr.c) cores/sim_mega328.c cores/sim_megax8.c
echo "$work/avrrun"
