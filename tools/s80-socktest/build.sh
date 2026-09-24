#!/bin/bash
# build.sh — build SockTest.exe, a Symbian OS 7.0s (EKA1, ARMI) executable, on Linux.
#
#   TC=<razvang-dev Nokia-N-Gage-SDK-Toolchain v1.0.1 release root> \
#   SDK=<dir holding Epoc32/ and caseinc/ made by prep-sdk.sh> ./build.sh      -> build/SockTest.exe
#
# Toolchain: GCC 2.9-psion-98r2 ("Symbian build 539"), arm-epoc-pe binutils and petran from the
# razvang-dev release tarball (prebuilt Linux x86_64); only its compiler/linker/tools are used.
# SDK: Nokia Series 80 DP 2.0 SDK (Symbian OS 7.0s) Epoc32/include + Epoc32/release/armi/urel,
# prepared by prep-sdk.sh (CRLF -> LF, case-insensitive include symlinks).
# The steps are the SDK's own makmake/cl_gcc.pm recipe for an ARMI UREL EXE: the compile flags and
# macro list, EEXE.LIB first, the two-pass ld + dlltool base-relocation link, petran.
set -euo pipefail
: "${TC:?set TC to the toolchain root (contains toolchain/bin/arm-epoc-pe-g++)}"
: "${SDK:?set SDK to the prepared SDK dir (contains Epoc32/ and caseinc/)}"
SRC=$(cd "$(dirname "$0")" && pwd)
OUT=${OUT:-$SRC/build}
NAME=socktest
UID3=0x0A5C7E57     # test-range UID (0x01000000-0x0FFFFFFF is reserved for development)
# dlltool runs an unprefixed `as`, so the target bin dir must precede /usr/bin
export PATH="$TC/toolchain/arm-epoc-pe/bin:$TC/toolchain/bin:$PATH"
LIB=$SDK/Epoc32/release/armi/urel
mkdir -p "$OUT"

CXXFLAGS=(-march=armv4t -mthumb-interwork -pipe -c -nostdinc -Wall -Wno-ctor-dtor-privacy
  -Wno-unknown-pragmas -s -fomit-frame-pointer -O
  -D__SYMBIAN32__ -D__GCC32__ -D__EPOC32__ -D__MARM__ -D__MARM_ARMI__ -D__EXE__ -DNDEBUG -D_UNICODE
  -I"$SRC" -I"$SDK/Epoc32/include" -I"$SDK/caseinc")
LIBS=("$LIB/EGCC.LIB" "$LIB/EUSER.lib" "$LIB/WS32.lib" "$LIB/GDI.lib" "$LIB/ESOCK.lib"
  "$LIB/INSOCK.lib" "$LIB/COMMDB.lib" "$LIB/HTTP.lib" "$LIB/BAFL.lib" "$LIB/INETPROTUTIL.lib")
LD=(arm-epoc-pe-ld -s -e _E32Startup -u _E32Startup)

echo "[1/4] compile"
arm-epoc-pe-g++ "${CXXFLAGS[@]}" "$SRC/$NAME.cpp" -o "$OUT/$NAME.o"
rm -f "$OUT/$NAME.in"
arm-epoc-pe-ar cr "$OUT/$NAME.in" "$OUT/$NAME.o"
echo "[2/4] link pass 1 (base file)"
"${LD[@]}" --base-file "$OUT/$NAME.bas" -o "$OUT/$NAME.pass1" "$LIB/EEXE.LIB" \
  --whole-archive "$OUT/$NAME.in" --no-whole-archive "${LIBS[@]}"
arm-epoc-pe-dlltool -m arm_interwork --base-file "$OUT/$NAME.bas" --output-exp "$OUT/$NAME.exp"
echo "[3/4] link pass 2"
"${LD[@]}" "$OUT/$NAME.exp" -Map "$OUT/$NAME.map" -o "$OUT/$NAME.pe" "$LIB/EEXE.LIB" \
  --whole-archive "$OUT/$NAME.in" --no-whole-archive "${LIBS[@]}"
echo "[4/4] petran"
petran "$OUT/$NAME.pe" "$OUT/SockTest.exe" -nocall -uid1 0x1000007a -uid2 0x00000000 -uid3 $UID3 \
  -heap 0x10000 0x800000 -stack 0x8000
ls -l "$OUT/SockTest.exe"
od -An -tx4 -N16 "$OUT/SockTest.exe"
