#!/usr/bin/env bash
# CppSekai -> Web (Emscripten/WASM) build script.
#
# Produces build-web/cppsekai.html + .js + .wasm + .data. Serve the folder over
# HTTP (file:// does not work with WebGL/atomics) and open cppsekai.html.
#
# This script is meant to run inside the DSH/Termux sandbox where the aarch64
# Emscripten package lives under $PREFIX/opt/emscripten. On a normal machine
# just have `emcc` on PATH; the flags are the interesting part.
set -e
cd "$(dirname "$0")"

PREFIX="${PREFIX:-/data/data/com.dsharnessmobile.shell/files/usr}"
if [ -d "$PREFIX/opt/emscripten" ] && ! command -v emcc >/dev/null 2>&1; then
    export PATH="$PREFIX/opt/emscripten:$PREFIX/bin:$PATH"
fi
export TMPDIR="${TMPDIR:-/storage/emulated/0/cppsekai/.tmp}"
mkdir -p "$TMPDIR"

OUT="${OUT:-build-web}"
OBJ="$OUT/obj"
mkdir -p "$OBJ"

COMMON=(
    -std=c++20
    -O2
    -fexceptions
    -D_XM_NO_INTRINSICS_=1
    -DIMGUI_IMPL_OPENGL_ES2
    '-DImTextureID=void*'
    -I.
    -Icore/native
    -Icore/native/src
    -Icore/native/generated
    -Ithird_party
    -Ithird_party/nlohmann
    -Ithird_party/DirectXMath/Inc
    -Ithird_party/imgui
    -sUSE_SDL=2
)

GAME=(
    main.cpp
    platform/Audio.cpp
    platform/CoreApi.cpp
    platform/FontOutline.cpp
    platform/Party_android.cpp
    platform/Renderer.cpp
    platform/SystemMedia_android.cpp
    game/Hud.cpp
    game/Intro.cpp
    game/Judgement.cpp
    game/PartyScreen.cpp
    game/Result.cpp
    game/SongSelect.cpp
    game/StageBackground.cpp
    game/TapEffect.cpp
    game/Ui.cpp
)

UPSTREAM=(
    core/native/src/mmw_preview.cpp
    core/native/mmw_port/Math.cpp
    core/native/mmw_port/MinMax.cpp
    core/native/mmw_port/Note.cpp
    core/native/mmw_port/Score.cpp
    core/native/mmw_port/Tempo.cpp
    core/native/mmw_port/Utilities.cpp
    core/native/mmw_port/EffectView.cpp
    core/native/mmw_port/Particle.cpp
    core/native/mmw_port/ResourceManager.cpp
    core/native/mmw_port/Rendering/Camera.cpp
)

IMGUI=(
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_impl_sdl2.cpp
    third_party/imgui/imgui_impl_opengl3.cpp
)

obj_of() { echo "$OBJ/$(echo "$1" | tr '/' '_' | sed 's/\.cpp$/.o/')"; }

OBJS=()
for f in "${GAME[@]}"; do
    o=$(obj_of "$f")
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
        echo "[cc] $f"
        emcc -c "$f" "${COMMON[@]}" -o "$o"
    fi
    OBJS+=("$o")
done
# Upstream + imgui are third-party: silence their warnings.
for f in "${UPSTREAM[@]}" "${IMGUI[@]}"; do
    o=$(obj_of "$f")
    if [ ! -f "$o" ] || [ "$f" -nt "$o" ]; then
        echo "[cc] $f"
        emcc -c "$f" "${COMMON[@]}" -w -o "$o"
    fi
    OBJS+=("$o")
done

echo "[link] $OUT/cppsekai.html"
em++ "${OBJS[@]}" \
    "${COMMON[@]}" \
    -o "$OUT/cppsekai.html" \
    --shell-file web/shell.html \
    --preload-file assets@/assets \
    --preload-file music-levels.json@/music-levels.json \
    --preload-file musics.json@/musics.json \
    --preload-file music-aliases.json@/music-aliases.json \
    --preload-file music-vocals.json@/music-vocals.json \
    -sASYNCIFY=1 \
    -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 \
    -sALLOW_MEMORY_GROWTH=1 \
    -sINITIAL_MEMORY=134217728 \
    -sMAXIMUM_MEMORY=1073741824 \
    -sSTACK_SIZE=1048576 \
    -sNO_EXIT_RUNTIME=1 \
    -sENVIRONMENT=web \
    -sUSE_SDL=2 \
    -sFORCE_FILESYSTEM=1 \
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS,callMain \
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free \
    
echo "OK -> $OUT/cppsekai.html"
