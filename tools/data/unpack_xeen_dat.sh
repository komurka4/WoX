#!/bin/bash
# Prepare the game's XEEN.DAT for tools/data/extract_program_data.py.
#
# CD-ROM release: XEEN.DAT (on the CD: GAME\XEEN.DAT) is compressed with PKLITE and is unpacked here.
# GOG.com release: use WORLD\XEEN.DAT from the installation folder (not the XEEN.DAT beside the .GOG images, which is
# a different build without the CD-ROM features); it is converted by tools/data/gog_xeen_dat.py (no download needed).
#
# Uses deark 1.7.3 (Jason Summers, MIT licence, https://github.com/jsummers/deark), downloaded as the release archive
# (SHA-256 checked) and built in DEPS/deark-1.7.3 the first time. deark is only a build-time tool: it is not part of
# WoX or its packages. The result must be exactly the unpacked XEEN.DAT the extractor knows (382,608 bytes, SHA-256
# 88704c22...); anything else is refused. An already unpacked XEEN.DAT is copied unchanged.
#
# Usage: tools/data/unpack_xeen_dat.sh /path/to/XEEN.DAT OUT_FILE      (DEPS: environment, default deps/)
# Then:  python3 -B tools/data/extract_program_data.py --xeen-dat OUT_FILE --out src/data --meta build/program-data-meta
# Requires curl, tar, sha256sum, make and a C compiler. The input file is only read.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
[ $# -eq 2 ] || { sed -n 2,17p "$0" | sed 's/^# \{0,1\}//'; exit 2; }
IN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
OUT=$2
DEPS=${DEPS:-$ROOT/deps}
JOBS=${JOBS:-3}
UNPACKED_SHA=88704c229ada806289dc4a513f77c88e1966fb8e3cf1d23652c748a7a7cd1687
CANONICAL_SHA=ef7db634a74059e94ec07bbf6396abcdce7fb7c36c1baa265b170ff7903c1523
GOG_WORLD_SHA=8bf78b4850d5973cbb063c7dd96b9aaef4e94791d748334187eef74acf528d27
GOG_ROOT_SHA=5762f2c7305f7739c85173bb9aad165f59ac65f1f6c72d136d6ee072548b3d20
DEARK_SHA=fddc9e548925ecda60f1e56a23a290d33bc64cfca26f78794c00ecdac9c8d30d
DEARK=$DEPS/deark-1.7.3

[ -f "$IN" ] || { echo "no such file: $IN" >&2; exit 1; }
in_sha=$(sha256sum < "$IN" | cut -d' ' -f1)
case "$in_sha" in
  "$UNPACKED_SHA"|"$CANONICAL_SHA")
    cp "$IN" "$OUT"
    echo "$IN is already the unpacked XEEN.DAT: copied to $OUT"
    exit 0 ;;
  "$GOG_WORLD_SHA")
    exec python3 -B "$ROOT/tools/data/gog_xeen_dat.py" "$IN" "$OUT" ;;
  "$GOG_ROOT_SHA")
    echo "$IN is the GOG.com release's other XEEN.DAT, a different build (without the CD-ROM features) that WoX cannot use." >&2
    echo "Use WORLD\\XEEN.DAT from the same installation folder instead." >&2
    exit 1 ;;
esac

if [ ! -x "$DEARK/deark" ]; then
  mkdir -p "$DEPS/downloads"
  archive=$DEPS/downloads/deark-1.7.3.tar.gz
  if [ ! -f "$archive" ]; then
    curl -fsSL --retry 3 -o "$archive.part" https://github.com/jsummers/deark/archive/refs/tags/v1.7.3.tar.gz
    mv "$archive.part" "$archive"
  fi
  echo "$DEARK_SHA  $archive" | sha256sum -c -
  rm -rf "$DEARK"
  tar -xzf "$archive" -C "$DEPS"
  make -C "$DEARK" -j"$JOBS" >/dev/null
fi

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
(cd "$work" && "$DEARK/deark" -m pklite -o unpacked "$IN" >/dev/null 2>&1) || true
result=$(ls "$work"/unpacked.*.exe 2>/dev/null | head -1 || true)
if [ -z "$result" ] || [ "$(sha256sum < "$result" | cut -d' ' -f1)" != "$UNPACKED_SHA" ]; then
  echo "deark could not turn $IN into the known unpacked XEEN.DAT (SHA-256 $UNPACKED_SHA)." >&2
  echo "This copy of the game may be a different release; please report it (README: Reporting problems)." >&2
  exit 1
fi
cp "$result" "$OUT"
echo "unpacked $IN -> $OUT (SHA-256 verified)"
