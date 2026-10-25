# CppSekai Web (WASM 移植) 发行包

这是 [BSOD-MEMZ/CppSekai](https://github.com/BSOD-MEMZ/CppSekai) 的浏览器 / WebAssembly 移植，
基于 [arcwolf1/CppSekai-droid](https://github.com/arcwolf1/CppSekai-droid) 的 GLES2 改造，
再加一层 `__EMSCRIPTEN__` 支持。**不是 JS 重写**：`main.cpp` / `platform/` / `game/` /
上游的 `core/native/` 都是同一份 C++ 代码，经 Emscripten 编成 wasm。

> ⚠️ AGPL-3.0。`assets/` 里的贴图 / 音效、曲名与谱面数据版权归原权利人，
> 仅限本地个人游玩与学习，禁止二次分发与商业使用。

## 目录

```
dist/                      已编译好的网页，直接跑
  cppsekai.html            常规版入口（同目录需要 js/wasm/data）
  cppsekai.js              Emscripten glue
  cppsekai.wasm            游戏逻辑
  cppsekai.data            资源包（assets + 曲库表 + 字体）
  cppsekai-standalone.html 单文件版：wasm 与资源都内嵌，file:// 双击可用

src 就是本目录：            完整源码，可自行重建
  main.cpp                 程序入口 / 主循环（emscripten_set_main_loop）
  platform/                平台层：Renderer(GLES2)、Audio(miniaudio)、win_compat.h
  game/                    游戏逻辑与全部 ImGui 界面（原样，未改 UI）
  core/native/             上游 AGPL 谱面核心（SUS 解析 / 绘制数据 / 判定事件）
  third_party/             imgui、miniaudio、stb、nlohmann/json、DirectXMath
  assets/                  贴图 / 音效 / 字体（启动卡与游戏 UI 都用它）
  web/shell.html           页面外壳：仅 canvas + 启动遮罩
  build-web.sh             构建常规版
  build-single.sh          构建单文件版（preload + base64 内嵌 + getPreloadedPackage）
  postprocess-single.sh    单文件版 HTML 的流式拼装
  serve.sh                 本地 HTTP 服务（file:// 打不开常规版时用）
```

## 直接运行

常规版（推荐，加载快）：

```bash
cd dist
python3 -m http.server 8000
# 浏览器打开 http://127.0.0.1:8000/cppsekai.html
```

单文件版：

```bash
# 直接双击 dist/cppsekai-standalone.html（file:// 可用，36 MB，首次加载 10–30 秒）
```

## 从源码重建

需要 Emscripten（`emcc` / `em++` 在 PATH 上）：

```bash
bash build-web.sh        # -> build-web/{cppsekai.html,js,wasm,data}
bash build-single.sh     # -> build-web/cppsekai-standalone.html（复用上面的 .o）
```

产物会写到 `build-web/`，把内容拷到 `dist/` 即可分发。

## 已知限制

- 页面里唯一自绘的是**启动遮罩**（配色取自 `game/Ui.cpp`：`#77eedd` / `#00ccbb` /
  `#3c3c52` / `#7d7d94`）。`main()` 一执行它就淡出，之后全部界面是游戏的 ImGui。
- 谱面要自己放：仓库不带谱面，选歌列表为空是原版行为。
- 存档是内存的，刷新即丢（原版不写盘的行为）。
- 多人同机联机、谱面下载器（Win32 工具）、系统媒体浮层在浏览器里不可用。
