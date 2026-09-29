# Phase 3 Local Source Plugin 验收记录

日期：2026-09-29。基线：[Phase 3 规格](phase-3-local-source-plugin-spec.md)、[实施计划](../superpowers/plans/2026-09-27-phase-3-local-source-plugin.md)、[最终架构方案](QueMusic-Plugin-Architecture-Final-Plan.md)。本记录只描述当前分支 `codex/plugin-ui-api-v1` 的实测状态；不表示已合并、发布或完成整个最终架构。

## 环境与实测命令

- macOS 26.6.2 (x86_64)、Qt 6.11.1、AppleClang 21.0.0、CMake 3.30.5、Debug build key。
- `cmake --build build-phase3 -j4`：成功。
- `QT_QPA_PLATFORM=offscreen ctest --test-dir build-phase3 --output-on-failure -j4`：最终复跑 64/64 通过，0 失败、0 skip，32.66 秒。
- `git diff 56b968f -- sdk/source/v2`：无差异；可选内容通知位于独立的 `sdk/source/extensions/content-events/v1`，没有修改 v2 的结构布局或虚函数表。
- Windows、Linux、其他 Qt 版本/编译器/架构未在本机运行；不能据此宣称跨平台通过。

## Phase 3-A：插件与 Provider 闭环

| 规格 §9 范围 | 实测证据与边界 |
| --- | --- |
| 扫描/配置、路径边界 | `quemusic_local_source_scanner_test`、`quemusic_local_library_index_test`、`quemusic_local_source_test` 覆盖递归/忽略、重复、重扫、删除/移动、空库、路径规范化、链接越界与扫描后的目标变化。拒绝访问用可控 policy seam 模拟；未用 chmod 假装跨平台权限验证。 |
| 元数据与资源 | `quemusic_local_lyrics_test`、scanner/source tests 覆盖真实小型音频 fixture、标签、时长、sidecar/内嵌封面与歌词优先级；缺资源不阻断播放。 |
| Provider 与通知 | `quemusic_local_source_test`、`quemusic_page_repository_test`、`quemusic_source_registry_v2_test` 覆盖请求终态顺序、取消、generation、scope/cursor、内容事件引起的失效与刷新。`quemusic_source_content_events_external_test` 从临时安装前缀只用公开导出 target 编译并跨共享库收发通知；首次接入即通过，它是安装兼容验收，不冒充 RED→GREEN。 |
| 实例/生命周期/播放 | `quemusic_local_source_integration_test` 通过真实动态包、Registry 多实例与 PlaybackCoordinator/fake Sink 验证文件 URL、禁用/删除/重载；index tests 覆盖 worker/watcher 停止和迟到结果 fencing。 |
| 发行/兼容 | `quemusic_local_source_package_test` 从临时安装目录真实发现并加载 Local 包，拒绝 Qt major/架构/build key 不符及缺失 `source_content_events` 运行库；`quemusic_macos_local_bundle_plugin_test` 和 `_incremental_test` 验证 Local 包文件与模块/manifest 增量同步。原 Navidrome 两个 bundle 测试仍通过。编译器不匹配没有在本机生成第二套二进制，只有现有 Host 兼容校验；不能声称已实测。 |

## Phase 3-B：目录 UI 与非破坏性迁移

- `quemusic_legacy_local_folder_migration_test` 覆盖旧 `folders(type=local)` 到普通 SourceAccount 的幂等导入、持久 pending/complete、部分失败重试、删除墓碑和缺失目录。旧表、音频源文件和自建集合均未删除。
- `quemusic_directory_library_controller_test`、`quemusic_original_ui_local_directories_test`、`quemusic_local_directories_qml_test` 验证 Directory 投影不替换在线 Category、多个实例、单实例错误隔离、私有 MediaRef/路径不透出、失效 key 拒绝、返回/刷新/分页、设置导航信号、播放/enqueue 和无 Local package 时不回退旧目录路径。
- FilePage 本地目录分支现在通过 Adapter/Source Plugin；导入和管理进入通用 Settings Schema 页面。原“我的文件夹”自建集合仍由 Legacy 模型承载。离屏 QML 预览已人工检查基本布局，测试夹具缺真实图标字体；尚无真实应用的主题切换截图或完整键鼠视觉验收。现有 UI 结构、动作、播放和主题回归测试在全量 CTest 中通过。

## 未完成的最终架构出口

- 不宣称整个 FilePage 已完全由 OriginalUiMusicAdapter 驱动：自建集合及部分旧播放/队列仍在 Legacy 路径。自建集合与 MediaRef 持久化应进入 Phase 4/6，最迟 Phase 6 去掉 FilePage 直接业务模型依赖。
- URL/path/平台整数队列与历史迁移属于 Phase 4；其他 Legacy 播放删除属于 Phase 5/10。Kugou/Netease 平台 UI 与 API 清理另按 Phase 8/9/10 处理。
- 本轮仅完成 macOS Debug/x86_64 的安装和 bundle 路径验证。发行前还需真实 Windows/Linux 构建、不同 Qt/编译器构建矩阵、部署后无构建目录依赖的运行检查及完整应用视觉验收。
- 本轮为实现者自审，没有独立 reviewer；没有推送、PR、合并或发布动作。
