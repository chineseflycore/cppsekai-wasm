"""Build the community web port (web-port/CppSekai-Web) with this machine's emsdk.

Mirrors that project's build-web.sh, plus platform/WebDownloader.cpp (the in-game
downloader this fork adds) and its -sEXPORTED_FUNCTIONS needs.

Usage: python build-port.py
Out:   web-port/CppSekai-Web/dist/cppsekai.html (+ .js/.wasm/.data)
"""
import glob
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.join(ROOT, "web-port", "CppSekai-Web")
EMSDK = os.path.join(ROOT, "toolchain", "emsdk")
EMXX = os.path.join(EMSDK, "upstream", "emscripten", "em++.py")
OUT = os.path.join(PROJ, "dist")

COMMON = [
    "-std=c++20",
    "-O3",
    "-fexceptions",
    "-D_XM_NO_INTRINSICS_=1",
    "-DIMGUI_IMPL_OPENGL_ES2",
    "-DImTextureID=void*",
    "-I.",
    "-Icore/native",
    "-Icore/native/src",
    "-Icore/native/generated",
    "-Ithird_party",
    "-Ithird_party/nlohmann",
    "-Ithird_party/DirectXMath/Inc",
    "-Ithird_party/imgui",
    "-sUSE_SDL=2",
]

GAME = [
    "main.cpp",
    "platform/Audio.cpp",
    "platform/CoreApi.cpp",
    "platform/FontOutline.cpp",
    "platform/Party_android.cpp",
    "platform/Renderer.cpp",
    "platform/SystemMedia_android.cpp",
    "platform/WebDownloader.cpp",  # web-only: replaces chartdl.exe
    "game/Hud.cpp",
    "game/Intro.cpp",
    "game/Judgement.cpp",
    "game/PartyScreen.cpp",
    "game/Result.cpp",
    "game/SongSelect.cpp",
    "game/StageBackground.cpp",
    "game/TapEffect.cpp",
    "game/Ui.cpp",
]

UPSTREAM = [
    "core/native/src/mmw_preview.cpp",
    "core/native/mmw_port/Math.cpp",
    "core/native/mmw_port/MinMax.cpp",
    "core/native/mmw_port/Note.cpp",
    "core/native/mmw_port/Score.cpp",
    "core/native/mmw_port/Tempo.cpp",
    "core/native/mmw_port/Utilities.cpp",
    "core/native/mmw_port/EffectView.cpp",
    "core/native/mmw_port/Particle.cpp",
    "core/native/mmw_port/ResourceManager.cpp",
    "core/native/mmw_port/Rendering/Camera.cpp",
]

IMGUI = sorted(glob.glob("third_party/imgui/imgui*.cpp"))

LINK = [
    "-o", "dist/cppsekai.html",
    "--shell-file", "web/shell.html",
    # Emscripten 6 moved the JS libraries to src/lib/ and no longer links IDBFS
    # just because something names it, so the backend file is passed explicitly
    # (the $IDBFS symbol our shim depends on lives there).
    "--js-library", os.path.join(EMSDK, "upstream", "emscripten", "src", "lib", "libidbfs.js"),
    "--js-library", "web/idbfs-lib.js",
    "--preload-file", "assets@/assets",
    "--preload-file", "music-levels.json@/music-levels.json",
    "--preload-file", "musics.json@/musics.json",
    "--preload-file", "music-aliases.json@/music-aliases.json",
    "--preload-file", "music-vocals.json@/music-vocals.json",
    "--preload-file", "assets/cards@/assets/cards",
    # Seed charts (one song + jacket) so the built page has something to show
    # out of the box; the downloader adds more at runtime.
    "--preload-file", "charts@/seed",
    # No -sASYNCIFY: the ported main loop is already emscripten_set_main_loop
    # (see main.cpp's __EMSCRIPTEN__ branch), so Asyncify's per-call unwind
    # checks were pure overhead. Audio init had to be made non-blocking for this.
    "-sMIN_WEBGL_VERSION=2",
    "-sMAX_WEBGL_VERSION=2",
    "-sALLOW_MEMORY_GROWTH=1",
    "-sINITIAL_MEMORY=134217728",
    "-sMAXIMUM_MEMORY=1073741824",
    "-sSTACK_SIZE=1048576",
    "-sNO_EXIT_RUNTIME=1",
    "-sENVIRONMENT=web",
    "-sUSE_SDL=2",
    "-sFORCE_FILESYSTEM=1",
    "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS,callMain",
    "-sEXPORTED_FUNCTIONS=_main,_malloc,_free,_cppsekai_dl_done",
]


def env():
    if not os.path.isfile(EMXX):
        raise SystemExit(f"em++ missing at {EMXX}")
    e = os.environ.copy()
    e["EM_CONFIG"] = os.path.join(EMSDK, ".emscripten")
    pythons = glob.glob(os.path.join(EMSDK, "python", "*", "python.exe"))
    nodes = glob.glob(os.path.join(EMSDK, "node", "*", "node.exe"))
    e["PATH"] = os.pathsep.join([os.path.dirname(p) for p in (pythons[:1] + nodes[:1])] + [e.get("PATH", "")])
    tmp = os.path.join(EMSDK, "upstream", "emscripten", "cache", "build", "web-tmp")
    os.makedirs(tmp, exist_ok=True)
    for v in ("TMP", "TEMP", "TMPDIR", "EMCC_TEMP_DIR"):
        e[v] = tmp
    return e


def py():
    p = glob.glob(os.path.join(EMSDK, "python", "*", "python.exe"))
    return p[0] if p else sys.executable


def main():
    os.makedirs(OUT, exist_ok=True)
    objdir = os.path.join(PROJ, "build-web", "obj")
    os.makedirs(objdir, exist_ok=True)
    e = env()
    objs = []

    def obj_of(src):
        # glob() on Windows hands back mixed separators ("third_party/imgui\x.cpp"),
        # so both kinds have to be flattened or the -o path gains a subdirectory.
        flat = src.replace("/", "_").replace("\\", "_").replace(".cpp", ".o")
        return os.path.join(objdir, flat)

    groups = [(GAME, False), (UPSTREAM + IMGUI, True)]
    for sources, quiet in groups:
        for src in sources:
            o = obj_of(src)
            objs.append(o)
            if os.path.exists(o) and os.path.getmtime(o) > os.path.getmtime(os.path.join(PROJ, src)):
                continue
            cmd = [py(), EMXX, "-c", src] + COMMON + (["-w"] if quiet else []) + ["-o", o]
            print(f"[cc] {src}")
            r = subprocess.run(cmd, cwd=PROJ, env=e)
            if r.returncode != 0:
                print("[port] compile failed:", src)
                return r.returncode

    print("[link] dist/cppsekai.html")
    cmd = [py(), EMXX] + objs + COMMON + LINK
    r = subprocess.run(cmd, cwd=PROJ, env=e)
    if r.returncode != 0:
        print("[port] link failed")
        return r.returncode
    wasm = os.path.join(OUT, "cppsekai.wasm")
    print(f"[port] OK -> dist/cppsekai.html ({os.path.getsize(wasm)/1024/1024:.2f} MB wasm)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
