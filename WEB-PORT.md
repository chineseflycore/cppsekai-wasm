# CppSekai Web —— 浏览器 / WASM 移植版

把 [BSOD-MEMZ/CppSekai](https://github.com/BSOD-MEMZ/CppSekai)（C++ / SDL2 / OpenGL 的
Project SEKAI 风格 SUS 谱面游玩器）编译到浏览器里跑。**不是重写，而是同一份 C++ 代码
经 Emscripten 编成 WebAssembly**，在 WebGL2 上下文里渲染、用 Web Audio 出声。

本移植基于 [arcwolf1/CppSekai-droid](https://github.com/arcwolf1/CppSekai-droid)
（Android 移植，已把桌面 GL 3.3 降到 GLES2、剥离了 Win32 依赖），在这里再加一层
`__EMSCRIPTEN__` 支持。

> ⚠️ **版权**：上游 AGPL-3.0。仓库里的贴图 / 音效 / 曲名 / 谱面数据版权归原权利人，
> 仅限本地个人游玩与学习，禁止二次分发与商业使用。发布时**不要**打包 `assets/`。

## 目录

| 路径 | 说明 |
|---|---|
| `build-web.sh` | 构建脚本（emcc 命令行，产物在 `build-web/`） |
| `serve.sh` | 本地起 HTTP 服务（`file://` 打不开，必须走 HTTP） |
| `web/shell.html` | 页面外壳（加载进度 / 错误提示 / 全屏 canvas） |
| `platform/win_compat.h` | Win32 兼容层（Android 与 Web 共用） |
| `platform/Renderer.cpp` | GLES2 渲染路径（`CPSEKAI_GLES`） |
| `main.cpp` | 主循环改为 `emscripten_set_main_loop`；路径 / 渲染模式 / 输入按平台分支 |

## 构建

需要 Emscripten（`emcc` / `em++` 在 PATH 上）：

```bash
bash build-web.sh        # 常规版：html + js + wasm + data 四个文件
bash build-single.sh     # 单文件版：wasm + assets 全内嵌，file:// 可直开
```

产物：

```
build-web/cppsekai.html   页面入口
build-web/cppsekai.js     Emscripten glue（~250 KB）
build-web/cppsekai.wasm   游戏逻辑（~5.9 MB）
build-web/cppsekai.data   预加载资源（~22 MB，assets + 曲库表 + 字体）
```

## 运行

```bash
bash serve.sh                 # 默认 http://127.0.0.1:8765/cppsekai.html
PORT=9000 bash serve.sh       # 换端口
```

然后浏览器打开打印出来的地址。第一次加载要下载 ~28 MB，之后走缓存。

## 移植改了什么

1. **渲染**：桌面 GL 3.3 core → **GLES2 / WebGL2**（复用 droid 的 GLSL ES 100 shader）。
2. **主循环**：`while (running) runFrame();` → `emscripten_set_main_loop_arg`（浏览器拥有帧时钟）。
3. **音频**：miniaudio 自动走 Emscripten 的 Web Audio 后端；`-sASYNCIFY=1`。
4. **文件系统**：资源经 `--preload-file` 打进 `MEMFS` 的 `/assets`；`baseDir = "/"`。
5. **字体**：Android 系统字体换成本地打包的 `assets/fonts/cjk.ttf`（思源黑体）。
6. **Win32 / Android 专属功能**：SMTC、任务栏进度、原生菜单、多窗口联机、谱面下载器
   都降级为 no-op（`SystemMedia_android.cpp` / `Party_android.cpp` 两个 stub）。
7. **输入**：SDL 的键盘 / 鼠标 / 触摸事件由 Emscripten 映射到 canvas，多指触摸可用。

## 已知限制

- **页面里没有任何自绘 UI**：HTML 只有一个 `<canvas>`，CppSekai 自己的
  ImGui 界面（启动 splash、选歌、设置卡片、HUD、结算）原样画在里面。
- **谱面仍然要自己放**：仓库不带谱面，选歌列表为空是原版行为，不是移植问题。
- 多人同机联机、谱面下载器、系统媒体浮层在浏览器里不可用（有意为之）。
- **存档是内存的**：刷新即丢（和原版不写盘的行为一致）。要做持久化得在
  C++ 侧接 `IDBFS`，不能靠 JS 壳顶替。
- 需要支持 WebGL2 的浏览器（Chrome 56+ / Firefox 51+ / Safari 15+）。
- **单文件版是完全自包含的**（`cppsekai-standalone.html`，~18.5 MB）：wasm 和
  `assets/` 都编进去了，**`file://` 双击直接打开就能跑**，不需要 HTTP 服务器、
  也不需要任何同目录文件。代价是首次解析/解码较慢（手机约 3–10 秒）。

## 参考

- 上游：https://github.com/BSOD-MEMZ/CppSekai
- Android 移植（本移植的直接基础）：https://github.com/arcwolf1/CppSekai-droid
- 更早的 Web 版谱面预览（同源的 C++ → WASM 渲染核心）：
  https://github.com/watagashi-uni/sekai-mmw-preview-web
