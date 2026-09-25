#!/bin/bash
# build.sh -- build SysState.exe (Symbian OS 7.0s, EKA1 ARMI EXE) on Linux; recipe as tools/s80-socktest.
# Env: TC = razvang-dev Nokia-N-Gage-SDK-Toolchain v1.0.1 root, SDK = dir with a prepared S80 DP 2.0
#      Epoc32/ and caseinc/ (tools/s80-socktest/prep-sdk.sh), OUT = output dir (default ./build).
set -euo pipefail
: "${TC:?set TC to the toolchain root}"
: "${SDK:?set SDK to the prepared S80 DP 2.0 SDK}"
SRC=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$SRC/build}
NAME=sysstate
UID3=0x0A5C7E58     # test range (0x01000000-0x0FFFFFFF)
export PATH="$TC/toolchain/arm-epoc-pe/bin:$TC/toolchain/bin:$PATH"
LIB=$SDK/Epoc32/release/armi/urel
mkdir -p "$OUT"
CXXFLAGS=(-march=armv4t -mthumb-interwork -pipe -c -nostdinc -Wall -Wno-ctor-dtor-privacy
  -Wno-unknown-pragmas -s -fomit-frame-pointer -O
  -D__SYMBIAN32__ -D__GCC32__ -D__EPOC32__ -D__MARM__ -D__MARM_ARMI__ -D__EXE__ -DNDEBUG -D_UNICODE
  -I"$SRC" -I"$SDK/Epoc32/include" -I"$SDK/caseinc")
LIBS=("$LIB/EGCC.LIB" "$LIB/EUSER.lib" "$LIB/COMMONENGINE.lib" "$LIB/WS32.lib")
LD=(arm-epoc-pe-ld -s -e _E32Startup -u _E32Startup)
arm-epoc-pe-g++ "${CXXFLAGS[@]}" "$SRC/$NAME.cpp" -o "$OUT/$NAME.o"
rm -f "$OUT/$NAME.in"; arm-epoc-pe-ar cr "$OUT/$NAME.in" "$OUT/$NAME.o"
"${LD[@]}" --base-file "$OUT/$NAME.bas" -o "$OUT/$NAME.pass1" "$LIB/EEXE.LIB" \
  --whole-archive "$OUT/$NAME.in" --no-whole-archive "${LIBS[@]}"
arm-epoc-pe-dlltool -m arm_interwork --base-file "$OUT/$NAME.bas" --output-exp "$OUT/$NAME.exp"
"${LD[@]}" "$OUT/$NAME.exp" -Map "$OUT/$NAME.map" -o "$OUT/$NAME.pe" "$LIB/EEXE.LIB" \
  --whole-archive "$OUT/$NAME.in" --no-whole-archive "${LIBS[@]}"
petran "$OUT/$NAME.pe" "$OUT/SysState.exe" -nocall -uid1 0x1000007a -uid2 0x00000000 -uid3 $UID3 \
  -heap 0x1000 0x10000 -stack 0x2000
ls -l "$OUT/SysState.exe"; od -An -tx4 -N16 "$OUT/SysState.exe"
