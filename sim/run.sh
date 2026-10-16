#!/bin/sh
# Renders docs/*.png from the real firmware UI. Needs SDL2 (brew install sdl2 / apt install libsdl2-dev)
# and one `pio run` first, which fetches M5GFX.
set -e
cd "$(dirname "$0")/.."
G=.pio/libdeps/cardputer-adv/M5GFX/src
T=.pio/sim
mkdir -p $T/inc/utility docs
# main.cpp's hardware headers: sim.cpp defines everything before including it
for h in M5Cardputer.h RadioLib.h Preferences.h TinyGPSPlus.h bootloader_random.h utility/PI4IOE5V6408_Class.hpp; do
  : >$T/inc/$h
done
cc -O1 -c lib/monocypher/monocypher.c -o $T/mono.o
c++ -std=c++17 -O1 -w -I$T/inc -I$G -Isrc -Ilib/monocypher $(sdl2-config --cflags) \
  -x c++ sim/sim.cpp $(find $G -name '*.cpp' -o -name '*.c') \
  -x none $T/mono.o $(sdl2-config --libs) -o $T/sim
$T/sim docs
