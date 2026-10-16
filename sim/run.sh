#!/bin/sh
# End-to-end test + README screenshots: the real src/main.cpp and MeshCore, on a PC (see sim/sim.cpp).
# Needs SDL2 (brew install sdl2 / apt install libsdl2-dev) and one `pio run` first, which fetches the libraries.
set -e
cd "$(dirname "$0")/.."
L=.pio/libdeps/cardputer-adv
G=$L/M5GFX/src
M=$L/MeshCore
C=$L/Crypto
T=.pio/sim
mkdir -p $T/inc/utility $T/inc/helpers/radiolib $T/obj docs
# main.cpp's hardware headers: sim.cpp defines all of it before including main.cpp
for h in M5Cardputer.h Preferences.h TinyGPSPlus.h RTClib.h SD.h codec2.h bootloader_random.h target.h \
  utility/PI4IOE5V6408_Class.hpp helpers/ESP32Board.h helpers/radiolib/CustomSX1262Wrapper.h; do
  : >$T/inc/$h
done
echo '#include <Arduino.h>' >$T/inc/Stream.h
for f in $M/lib/ed25519/*.c; do cc -O1 -w -c $f -o $T/obj/$(basename $f .c).o; done
c++ -std=c++17 -O1 -w -DMAX_CONTACTS=64 -DMAX_GROUP_CHANNELS=8 \
  -I$T/inc -Isim -I$M/src -I$M/lib/ed25519 -I$C -I$L/base64/src -I$G -Isrc $(sdl2-config --cflags) \
  -x c++ sim/sim.cpp $(find $G -name '*.cpp' -o -name '*.c') \
  $M/src/Mesh.cpp $M/src/Dispatcher.cpp $M/src/Packet.cpp $M/src/Identity.cpp $M/src/Utils.cpp \
  $M/src/helpers/BaseChatMesh.cpp $M/src/helpers/AdvertDataHelpers.cpp $M/src/helpers/TxtDataHelpers.cpp \
  $M/src/helpers/StaticPoolPacketManager.cpp \
  $C/AES128.cpp $C/AESCommon.cpp $C/BlockCipher.cpp $C/ChaCha.cpp $C/Cipher.cpp $C/Crypto.cpp $C/Hash.cpp \
  $C/SHA256.cpp $C/SHA512.cpp $C/Ed25519.cpp $C/Curve25519.cpp $C/BigNumberUtil.cpp \
  -x none $T/obj/*.o $(sdl2-config --libs) -o $T/sim
$T/sim docs
