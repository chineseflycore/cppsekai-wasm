#!/usr/bin/env bash
# Single-file build: the WebAssembly binary is inlined into the HTML as base64,
# so a single .html file carries the whole program (the 22 MB asset pack
# cppsekai.data still sits next to it and is fetched at runtime).
#
# Run build-web.sh once first: this script reuses build-web/obj/*.o.
set -e
cd "$(dirname "$0")"

PREFIX="${PREFIX:-/data/data/com.dsharnessmobile.shell/files/usr}"
if [ -d "$PREFIX/opt/emscripten" ] && ! command -v em++ >/dev/null 2>&1; then
    export PATH="$PREFIX/opt/emscripten:$PREFIX/bin:$PATH"
fi
export TMPDIR="${TMPDIR:-/storage/emulated/0/cppsekai/.tmp}"
mkdir -p "$TMPDIR"

OUT="${OUT:-build-web}"
[ -d "$OUT/obj" ] || { echo "Run build-web.sh first (no $OUT/obj)" >&2; exit 1; }

OBJS=$(ls "$OUT"/obj/*.o | tr '\n' ' ')

em++ $OBJS \
    -std=c++20 -O2 -fexceptions \
    -D_XM_NO_INTRINSICS_=1 -DIMGUI_IMPL_OPENGL_ES2 '-DImTextureID=void*' \
    -sUSE_SDL=2 \
    -sSINGLE_FILE=1 \
    -sASYNCIFY=1 \
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sINITIAL_MEMORY=134217728 -sMAXIMUM_MEMORY=1073741824 \
    -sSTACK_SIZE=1048576 \
    -sNO_EXIT_RUNTIME=1 \
    -sENVIRONMENT=web \
    -sFORCE_FILESYSTEM=1 \
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS,callMain \
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free \
    --preload-file assets@/assets \
    --preload-file music-levels.json@/music-levels.json \
    --preload-file musics.json@/musics.json \
    --preload-file music-aliases.json@/music-aliases.json \
    --preload-file music-vocals.json@/music-vocals.json \
    -o "$OUT/cppsekai-single.js"

bash postprocess-single.sh "$OUT"
