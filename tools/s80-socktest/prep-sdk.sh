#!/bin/bash
# prep-sdk.sh <extracted SDK Epoc32 dir, local or rsync "host:path"> <dest-dir> [extra source dirs...]
#
# Stages the Series 80 DP 2.0 SDK pieces a Linux EKA1 build needs (Epoc32/include,
# Epoc32/release/armi/urel, Epoc32/tools for reference) and makes the headers usable by GCC on Linux:
#   1. CRLF -> LF: GCC 2.9 on Linux does not treat "\<CR><LF>" as a line continuation, so every
#      multi-line macro in e32std.h breaks ("stray '\' in program").
#   2. caseinc/: a symlink farm for every #include spelling that differs from the file's case.
# The SDK itself (S80_DP_2_0_SDK.zip, InstallShield cabs -> unshield) is not part of this tree.
set -euo pipefail
SRC=${1:?usage: prep-sdk.sh <SDK Epoc32 dir> <dest-dir> [extra source dirs...]}
DEST=${2:?usage: prep-sdk.sh <SDK Epoc32 dir> <dest-dir> [extra source dirs...]}
shift 2
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$DEST/Epoc32/release/armi"
rsync -a "$SRC/include" "$DEST/Epoc32/"
rsync -a "$SRC/release/armi/urel" "$DEST/Epoc32/release/armi/"
rsync -a "$SRC/tools" "$DEST/Epoc32/"
# grep -a: some headers are Latin-1 and would be skipped as "binary"
grep -a -rlI $'\r' "$DEST/Epoc32/include" | while IFS= read -r f; do sed -i 's/\r$//' "$f"; done
mkdir -p "$DEST/caseinc"
python3 "$HERE/mkcaseinc.py" "$DEST/Epoc32/include" "$DEST/caseinc" "$HERE" "$@"
echo "SDK staged in $DEST"
