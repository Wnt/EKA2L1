#!/bin/bash
# Build group/commdb_v7.dll, the Symbian OS 7.0s (Series 80 v2) variant of the CommDB host access point patch,
# on a Linux host.
#
# Needs:
#   TOOLCHAIN  an EKA1 GCC 2.9-psion-98r2 arm-epoc-pe toolchain: bin/ holds arm-epoc-pe-{g++,ar,ld,dlltool} and
#              petran; arm-epoc-pe/bin/ holds the unprefixed "as" that dlltool runs. The Linux build in
#              https://github.com/razvang-dev/Nokia-N-Gage-SDK-Toolchain (its toolchain/ directory) works.
#   EPOC32     the Epoc32 directory of a Symbian OS 7.0s SDK with ARMI libraries. The Series 80 DP 2.0 SDK
#              (Nokia 9300/9500) was used: its armi commdb.lib has 183 exports, as many as the 9300 ROM's
#              commdb.dll, and every ordinal this patch imports carries the same name in both.
#
# Usage: TOOLCHAIN=/path/to/toolchain EPOC32=/path/to/Epoc32 ./build_v7.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
: "${TOOLCHAIN:?set TOOLCHAIN to the arm-epoc-pe toolchain directory}"
: "${EPOC32:?set EPOC32 to the Epoc32 directory of the SDK}"
export PATH="$TOOLCHAIN/arm-epoc-pe/bin:$TOOLCHAIN/bin:$PATH"

NAME=commdb_v7
UID1=0x10000079
UID2=0x1000008d
UID3=0xee000015
DLLNAME="${NAME}[ee000015].dll"

find_dir() {    # case-insensitive lookup of one path component
    find "$1" -maxdepth 1 -iname "$2" -print -quit
}
INCLUDE="$(find_dir "$EPOC32" include)"
RELEASE="$(find_dir "$(find_dir "$(find_dir "$EPOC32" release)" armi)" urel)"
[[ -d $INCLUDE && -d $RELEASE ]] || { echo "no include/ or release/armi/urel/ under $EPOC32" >&2; exit 1; }
lib() {
    local f; f="$(find_dir "$RELEASE" "$1")"
    [[ -n $f ]] || { echo "missing $1 in $RELEASE" >&2; exit 1; }
    echo "$f"
}

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# The SDK headers have DOS line ends (a backslash before CR does not continue a macro for this cpp) and mixed-case
# names that the sources include in lower case. Give the compiler an LF copy under every spelling it may ask for.
mkdir -p "$WORK/inc"
cp -r "$INCLUDE/." "$WORK/inc/"
find "$WORK/inc" -type f -exec sed -i 's/\r$//' {} +
find "$WORK/inc" -depth -mindepth 1 | while read -r f; do
    d="$(dirname "$f")"
    b="$(basename "$f")"
    for bb in "${b,,}" "${b^^}"; do
        [[ -e "$d/$bb" ]] || ln -s "$b" "$d/$bb"
    done
done

arm-epoc-pe-g++ -c -nostdinc -march=armv4t -mthumb-interwork -O -fomit-frame-pointer -s \
    -Wall -Wno-ctor-dtor-privacy -Wno-unknown-pragmas \
    -D__SYMBIAN32__ -D__GCC32__ -D__EPOC32__ -D__MARM__ -D__MARM_ARMI__ -D__DLL__ -DNDEBUG -D_UNICODE \
    -I"$WORK/inc" "$HERE/src/$NAME.cpp" -o "$WORK/$NAME.o"
arm-epoc-pe-ar cr "$WORK/$NAME.in" "$WORK/$NAME.o"

# The EKA1 two-pass DLL link: the first pass yields a base file for the relocations, the second the final image.
DEF="$HERE/eabi/$NAME.def"
LIBS=("$(lib edll.lib)" --whole-archive "$WORK/$NAME.in" --no-whole-archive "$(lib edllstub.lib)" "$(lib egcc.lib)"
      "$(lib euser.lib)" "$(lib commdb.lib)")
arm-epoc-pe-dlltool -m arm_interwork --def "$DEF" --output-exp "$WORK/$NAME.exp" --dllname "$DLLNAME"
arm-epoc-pe-ld -s -e _E32Dll -u _E32Dll "$WORK/$NAME.exp" --dll --base-file "$WORK/$NAME.bas" \
    -o "$WORK/$NAME.pass1" "${LIBS[@]}"
arm-epoc-pe-dlltool -m arm_interwork --def "$DEF" --dllname "$DLLNAME" --base-file "$WORK/$NAME.bas" \
    --output-exp "$WORK/$NAME.exp2"
arm-epoc-pe-ld -s -e _E32Dll -u _E32Dll --dll "$WORK/$NAME.exp2" -o "$WORK/$NAME.pe" "${LIBS[@]}"
petran -uid1 $UID1 -uid2 $UID2 -uid3 $UID3 -nocall "$WORK/$NAME.pe" "$HERE/group/$NAME.dll"
echo "built $HERE/group/$NAME.dll"
