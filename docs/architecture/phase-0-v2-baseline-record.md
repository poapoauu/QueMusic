# Phase 0 Source SDK v2 基线记录

> 记录日期：2026-09-22
> 设计基线：[`QueMusic-Plugin-Architecture-Final-Plan.md`](./QueMusic-Plugin-Architecture-Final-Plan.md)
> 差距分析：[`current-state-gap-analysis.md`](./current-state-gap-analysis.md)

## 1. 基线身份

| 项目 | 值 |
| --- | --- |
| 已完成 v2 基础提交 | `b94e1812aac88617da2caad4d25b25b070db75e8` |
| Phase 0 隔离分支 | `codex/plugin-architecture-phase0` |
| 隔离工作树 | `/Users/liqiang/.codex/worktrees/plugin-architecture-phase0/QueMusic` |
| 架构文档提交 | `ede258e98959a76841ed818a5420b00c22913b43` |
| 架构文档原提交 | `ba3e3307ff9459d73f0a0d4e4564fe672430887d`（从当前检出分支 cherry-pick） |

隔离分支直接基于 `b94e181`，没有从 Source SDK v1 重新实现。`b94e181..ede258e` 之间只有三份已批准的 Markdown 文档，没有生产代码变化。

## 2. 工作树隔离

以下未提交内容属于原工作树，不进入 Phase 0 隔离分支。

### 当前原始检出目录

路径：`/Users/liqiang/Documents/ChatGPT/QueMusic`

- `ThirdParty/qwindowkit`：已修改；
- `.ui-screenshots/`：未跟踪。

### 既有 unified-media-bridge 工作树

路径：`/Users/liqiang/Documents/ChatGPT/QueMusic/.worktrees/codex-unified-media-bridge`

已修改：

- `CMakeLists.txt`；
- `cpp/AccountManager.cpp`；
- `cpp/LogManager.cpp`；
- `cpp/LogManager.h`。

未跟踪：

- `.superpowers/brainstorm/`；
- `cmake/QueMusicInfo.plist.in`；
- `cmake/VerifyMacOSLocalNetworkUsage.cmake`；
- `cmake/VerifyMacOSRuntimeLibraries.cmake`；
- `cpp/AppStoragePaths.cpp`；
- `cpp/AppStoragePaths.h`；
- `tests/tst_AppStoragePaths.cpp`；
- `tests/tst_LogManager.cpp`。

这些路径没有被复制、暂存或提交到 `codex/plugin-architecture-phase0`。后续如需采用，必须单独审查并形成独立提交。

## 3. 提交拓扑

审计时分支顶部为：

```text
ede258e docs: establish plugin architecture baseline
b94e181 fix: harden original pages against partial adapters
b509c4c fix: redact authenticated playback diagnostics
0156cd4 fix: preserve original player over secure v2 playback
5f25d5d fix: finish adapter detail and pagination routing
```

该拓扑确认 Phase 0 的开发起点包含此前完成的 v2 成果，并将架构文档作为单独提交叠加在其上。

## 4. 依赖初始化

原生工作树创建只检出了 superproject，三个顶层 submodule 初始状态均以 `-` 开头，首次 CMake configure 因缺少源码失败：

- `ThirdParty/qwindowkit`；
- `ThirdParty/taglib`；
- `api/QCloudMusicApi/3rdparty/cryptopp`。

在隔离工作树执行 `git submodule update --init --recursive` 后，所有顶层与递归 submodule 均检出 superproject 固定的提交，`git submodule status --recursive` 不再包含 `-` 或 `+` 状态。随后使用 CMake `--fresh` 重新配置，不复制任何原工作树文件。

## 5. 已测工具链

| 项目 | 实测值 |
| --- | --- |
| CMake | 3.30.5 |
| CMake generator | Unix Makefiles |
| Build type | Debug |
| Qt | 6.11.1，`/Users/liqiang/Qt/6.11.1/macos` |
| C++ compiler | Apple clang 21.0.0 (`clang-2100.1.1.101`) |
| Compiler target | `x86_64-apple-darwin25.6.0` |
| Host architecture | `x86_64` |
| Xcode | 26.5 (`17F42`) |
| Git | 2.50.1 (Apple Git-155) |

`CMAKE_OSX_ARCHITECTURES` 未显式设置，当前构建随本机编译器目标使用 `x86_64`。

## 6. Fresh configure / build

配置命令：

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake \
  --fresh \
  -S . \
  -B build-phase0-clean \
  -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=/Users/liqiang/Qt/6.11.1/macos
```

全量默认目标构建命令：

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/cmake \
  --build build-phase0-clean \
  --parallel 4
```

两条命令最终均以 exit 0 完成。已验证生成：

- `build-phase0-clean/bin/QueMusic.app/Contents/MacOS/QueMusic`；
- `build-phase0-clean/plugins/navidrome/libquemusic_navidrome_source.so`；
- Source SDK v2、SourceRegistry、MusicHub、PlaybackCoordinator、Settings、Original UI Adapter 等静态库。

Configure 仍报告三个既有非阻塞警告：MeshGradient QML module 的输出目录与 target path 不一致，以及 Qt policy `QTP0001`、`QTP0004` 未设置。这些警告不在 Phase 0 修改范围内，后续构建系统清理应单独处理。

首次运行 CTest 只发现 3 项测试。调查确认提交版 CMake 将主要测试目标置于 `if(BUILD_TESTING)` 内，而 Phase 0 初始配置命令没有显式设置该变量；既有旧构建缓存则包含 `BUILD_TESTING=ON`。因此 3/3 结果不作为完整基线，最终基线必须使用上述显式开启测试的配置命令重新生成。

## 7. Fresh CTest 基线

使用 `-DBUILD_TESTING=ON` 重新执行 `--fresh` configure 和全量 build 后，CTest 注册 36 项测试。执行：

```bash
/Users/liqiang/Qt/Tools/CMake/CMake.app/Contents/bin/ctest \
  --test-dir build-phase0-clean \
  --output-on-failure
```

结果：

- 36/36 通过；
- 0 项失败；
- 总耗时 62.76 秒。

该 36 项是纯 `b94e181` 提交树的权威测试数量。既有脏工作树中的 40 项还包含四项未提交 CMake/测试工作：`AppStoragePaths`、`LogManager`、macOS local-network plist 校验和 macOS runtime-library 校验；它们没有被混入本基线。

## 8. Source SDK v2 生产边界

在以下生产范围搜索 Source SDK v1 include 路径和 `SourceManager`：

```text
CMakeLists.txt main.cpp core sdk plugins components layout pages
```

结果无匹配。生产目标没有重新引入 Source SDK v1 或 `SourceManager`。

以下必需组件均存在：

- `sdk/source/v2/IMusicSourcePluginV2.h`；
- `core/source/SourceRegistry.h`；
- `core/music/CapabilityResolver.h`；
- `core/music/MediaActionRouter.h`；
- `core/music/PlaybackCoordinator.h`；
- `core/music/OriginalUiMusicAdapter.h`；
- `core/settings/PluginSettingsController.h`；
- `plugins/navidrome-source/NavidromeSourcePlugin.h`。

## 9. 已确认的迁移期冲突

Phase 0 不修改以下生产路径，只将其固定为后续阶段的迁移输入：

| 冲突 | 代表性路径 | 后续阶段 |
| --- | --- | --- |
| Host 仍编译平台 API | `api/NeteaseCloudApi.*`、`api/KugouApi.*`、`api/MusicApiService.*`、`CMakeLists.txt` | Phase 8–10 |
| Host 仍管理平台账号/设置 UI | `cpp/AccountManager.*`、`SettingsView.qml` | Phase 2、8–10 |
| Local 仍是特殊模型 | `cpp/FolderModel.*`、`pages/FilePage.qml`、`main.cpp` | Phase 3、5 |
| QML 仍使用 Legacy Source 整数 | `components/PlayList.qml`、`layout/PlayerControl.qml` 中的 `source == -1` | Phase 4–6 |
| QML 仍可直接设置播放资源 | `main.qml` 中的 `mainMedia.source = ...` | Phase 5–6 |
| 页面仍含 `MusicApi` fallback | `HomePage.qml`、`SearchPage.qml`、`PlaylistPage.qml`、`FavouritePage.qml`、`DownloadPage.qml` 等 | Phase 6、10 |
| Legacy 队列仍存在 | `components/LegacyQueueController.qml`、`components/PlayList.qml` | Phase 4、10 |

这些结果与 Gap Analysis 一致：v2 核心可直接保留，但主 UI、Local、队列和平台业务仍处于双轨迁移状态。

## 10. Phase 0 出口结论

- 隔离分支直接继承已完成的 v2 提交，没有重做 Source SDK；
- 原始工作树和既有 v2 工作树的未提交内容均得到隔离；
- 递归 submodule 使用 superproject 固定提交；
- 从空缓存 configure、build 成功；
- 提交树完整测试 36/36 通过；
- Source SDK v1 / `SourceManager` 未进入生产目标；
- Legacy 冲突已经明确记录，未在 Phase 0 提前删除。

因此该分支可作为 Phase 1 `QueMusic.PluginUI 1.0` 的实现起点。
