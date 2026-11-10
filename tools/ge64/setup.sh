#!/usr/bin/env bash
# Mario Has a Gun 64: generate everything that comes from the original ROMs.
#
# usage: tools/ge64/setup.sh [path/to/GoldenEye_US.z64]
#
# Needs baserom.us.z64 (Super Mario 64, US) in the repo root, and a GoldenEye 007 (US) ROM, either
# passed as the argument or placed at baserom.ge.us.z64 in the repo root. Nothing from either ROM is
# stored in this repository; this script extracts and converts it locally:
#   - SM64 assets (the decomp's own extract_assets.py)
#   - the GoldenEye 007 decomp (cloned at a pinned commit) and its asset extraction
#   - 15 GoldenEye gun models   -> bin/ge64_*.c, src/game/ge64_guns/*.inc.h
#   - 52 GoldenEye sound effects -> sound/samples/sfx_custom_ge/
#   - Mario's glove for the viewmodel, the suit's shirt stud, the title screen backdrop
# Then build with: make   (add GE64_LEVEL_SELECT=1 for the debug level-select build)
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
HERE=$ROOT/tools/ge64
SM64_ROM=$ROOT/baserom.us.z64
GE_ROM=${1:-$ROOT/baserom.ge.us.z64}
export GE_DECOMP=${GE_DECOMP:-$HERE/007}
PY=${PYTHON:-python3}

SM64_SHA1=9bef1128717f958171a4afac3ed78ee2bb4e86ce
GE_SHA1=abe01e4aeb033b6c0836819f549c791b26cfde83
GE_REPO=https://github.com/n64decomp/007.git
GE_COMMIT=c4356466796c697dfd298010b9bed261f9ed8c6a

# GoldenEye gun directory (in the 007 decomp) -> identifier used by the hack
GUNS="wppk:pp7 tt33:dd44 skorpion:klobb ak47:kf7 uzi:zmg mp5k:d5k spectre:phantom m16:ar33 fnp90:rcp90
      shotgun:shotgun autoshot:autoshot sniperrifle:sniper ruger:cougar goldengun:goldengun laser:laser"

say() { printf '\n== %s\n' "$*"; }
die() { printf '\nerror: %s\n' "$*" >&2; exit 1; }

check_rom() { # path sha1 description
    [ -f "$1" ] || die "$3 not found at $1"
    local sum; sum=$(sha1sum "$1" | cut -d' ' -f1)
    [ "$sum" = "$2" ] || die "$3 at $1 has SHA-1 $sum, expected $2.
       Only the US version in .z64 (big-endian) byte order is supported."
}

say "Checking ROMs and tools"
check_rom "$SM64_ROM" $SM64_SHA1 "Super Mario 64 (US) ROM"
check_rom "$GE_ROM" $GE_SHA1 "GoldenEye 007 (US) ROM"
command -v git >/dev/null || die "git is required"
command -v sox >/dev/null || die "sox is required to resample the sound effects (apt install sox)"
$PY -c 'import PIL' 2>/dev/null || die "Python Pillow is required (pip install pillow, or apt install python3-pil)"
echo 'int main(void){return 0;}' > "$HERE/.m32test.c"
gcc -m32 "$HERE/.m32test.c" -o "$HERE/.m32test" 2>/dev/null || { rm -f "$HERE/.m32test.c"; die "a 32-bit host compiler is required for GoldenEye's texture decompressor (apt install gcc-multilib)"; }
rm -f "$HERE/.m32test.c" "$HERE/.m32test"

say "Super Mario 64 assets"
(cd "$ROOT" && $PY extract_assets.py us)

say "GoldenEye 007 decomp ($GE_DECOMP)"
if [ ! -d "$GE_DECOMP/.git" ]; then
    git clone "$GE_REPO" "$GE_DECOMP"
fi
git -C "$GE_DECOMP" checkout -q $GE_COMMIT
if [ ! -f "$GE_DECOMP/assets/obseg/ob__ob_end.seg" ]; then
    cp "$GE_ROM" "$GE_DECOMP/baserom.u.z64"
    (cd "$GE_DECOMP" && TERM=${TERM:-dumb} ./scripts/extract_baserom.u.sh)
fi
if [ ! -f "$GE_DECOMP/assets/obseg/gun/wppk/Model.c" ]; then
    make -C "$GE_DECOMP" convert_guns
fi

say "GoldenEye texture decompressor"
make -C "$HERE/getex"

say "Gun models"
mkdir -p "$ROOT/src/game/ge64_guns"
for pair in $GUNS; do
    $PY "$HERE/convert_ge_gun.py" "${pair%%:*}" "${pair##*:}" "$ROOT" | tail -1
done

say "Sound effects"
SFX_TMP=$(mktemp -d)
trap 'rm -rf "$SFX_TMP"' EXIT
$PY "$HERE/n64bank_extract.py" "$GE_DECOMP/assets/music/sfx.ctl" "$GE_DECOMP/assets/music/sfx.tbl" \
    "$HERE/ge_sfx_ids.txt" "$SFX_TMP" | tail -1
$PY "$HERE/import_ge_sfx.py" "$ROOT" "$SFX_TMP" | tail -1

say "Mario's glove, suit stud, title backdrop"
$PY "$HERE/copy_glove.py" "$ROOT"
$PY "$HERE/mario_suit.py" "$ROOT" --button-only
cp "$HERE"/art/title_screen_bg.*.rgba16.png "$ROOT/textures/title_screen_bg/"

say "Done. Build with: make -j$(nproc)    (ROM: build/us/sm64.us.z64)"
