# CppSekai Web / WASM

把 [BSOD-MEMZ/CppSekai](https://github.com/BSOD-MEMZ/CppSekai)（C++ / SDL2 / OpenGL 的 Project SEKAI 风格
SUS 谱面游玩器）编译到浏览器里跑。**不是重写**，是同一份 C++ 代码经 Emscripten 编成 WebAssembly，
在 WebGL2 上下文里渲染、用 Web Audio 出声。

本移植建立在 [arcwolf1/CppSekai-droid](https://github.com/arcwolf1/CppSekai-droid)（Android 移植，
已把桌面 GL 3.3 降到 GLES2、剥离 Win32 依赖）之上，再加一层 `__EMSCRIPTEN__` 支持。

> ⚠️ **版权**：上游 AGPL-3.0。贴图 / 音效 / 曲名 / 谱面数据版权归原权利人，仅限本地个人游玩与学习，
> **禁止二次分发与商业使用**。仓库**不包含** `assets/` 与谱面，请自行准备。

## 特性

- **同一份 C++ 源码**：ImGui 界面（启动 splash、选歌、设置卡片、HUD、结算）原样画在 canvas 里
- **游戏内下载器**：浏览器直连官方 CDN 拉谱面 / 音频 / 曲绘（`SelectDownload` 分支的 web 实现，
  替代 Windows 的 chartdl.exe）
- **真持久化**：页面层 IndexedDB，启动前同步恢复到虚拟 FS（设置、账户、抽卡保底、下载的谱面都不丢）
- **性能**：`-O3`、无 Asyncify、下载器列表用 `ImGuiListClipper` 只画可见行

## 构建

需要 Emscripten（`emcc` / `em++` 在 PATH 上）。仓库根有 `build-web.sh`（原版），
另有本移植的构建脚本 `build-port.py`：

```bash
python build-port.py          # 产物 dist/cppsekai.html + .js + .wasm + .data
python inject-persistence.py  # 给 dist/cppsekai.html 注入 IndexedDB 持久化（每次构建后要跑）
bash serve.sh                 # 或者 python -m http.server 8082 --directory dist
```

浏览器打开 `http://127.0.0.1:8082/cppsekai.html`。**必须走 HTTP**，`file://` 打不开。

## 目录

| 路径 | 说明 |
|---|---|
| `main.cpp` | 主循环（web 分支用 `emscripten_set_main_loop`） |
| `game/` | 选歌 / 判定 / HUD / 结算 / 设置卡片 |
| `platform/` | 渲染器（GLES2）、音频（miniaudio）、`WebDownloader`（web 下载器） |
| `core/native/` | 谱面核心（上游代码） |
| `web/shell.html` | 页面外壳（只有一个 canvas + 启动进度条） |
| `third_party/` | ImGui、nlohmann/json、miniaudio 等 vendored 依赖 |

## 已知限制

- 多人同机联机、SMTC、任务栏进度在浏览器里不可用（no-op）
- 输入法中文在 canvas 内受限（imgui_impl_sdl2 的浏览器 IME 问题）
- 需要支持 WebGL2 的浏览器（Chrome 56+ / Firefox 51+ / Safari 15+）

## 致谢

- [BSOD-MEMZ/CppSekai](https://github.com/BSOD-MEMZ/CppSekai) —— 上游
- [arcwolf1/CppSekai-droid](https://github.com/arcwolf1/CppSekai-droid) —— Android 移植（本移植的直接基础）
- [watagashi-uni/sekai-mmw-preview-web](https://github.com/watagashi-uni/sekai-mmw-preview-web) —— 更早的 C++ → WASM 渲染核心

许可：**AGPL-3.0-only**（见 [LICENSE](LICENSE)）。
