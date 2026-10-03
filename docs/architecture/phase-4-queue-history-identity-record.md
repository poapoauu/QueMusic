# Phase 4 Queue / History Identity 验收记录

日期：2026-10-03。设计基线为[最终架构方案](QueMusic-Plugin-Architecture-Final-Plan.md)、[Phase 4 规格](../superpowers/specs/2026-09-29-phase-4-queue-history-identity-design.md)及[实施计划](../superpowers/plans/2026-09-29-phase-4-queue-history-identity.md)。本记录只描述隔离开发分支 `codex/plugin-ui-api-v1` 的阶段状态，不表示已合并、发布或完成最终架构。

## 环境与验证

- macOS 26.6.2、x86_64、Qt 6.11.1、CMake 3.30.5、Debug 构建；`cmake --build build-phase3 -j4` 成功。
- `QT_QPA_PLATFORM=offscreen ctest --test-dir build-phase3 --output-on-failure -j4`：最终复跑 69/69 通过，0 失败、0 skip，63.87 秒。新增 `quemusic_queue_history_integration_test` 通过。
- `git diff 56b968f -- sdk/source/v2`：无差异。Phase 4 的 `QueueHistoryTypes`、Codec 与 Store 是 Host 私有类型，未改 Source SDK v2 ABI。
- 本机未运行 Windows/Linux、其他 Qt/编译器/架构矩阵；没有真实应用的完整键鼠、主题与多媒体设备人工验收。

## 已验收的闭环

1. Local Plugin 的公开 Track/Directory ID 已改为按 SourceInstance 隔离的不透明 UUID。插件私有索引可保留受控 canonical path；Host 队列/历史 JSON 不保留该路径。重扫/重启稳定、删除后重建不复用、移动与跨实例不串号。旧直接路径型缓存被 `PageCache` 拒绝。
2. Host `schemaVersion: 1` Codec 只接受完整五元组、Track 类型、独立 occurrence、有限展示字段和最近播放时间；按 1000 队列项、200 历史项、16 MiB 等上限拒绝异常输入。流 URL、令牌、绝对路径和任意插件 metadata 不进入活动持久格式。
3. `PlaybackCoordinator` 导出/恢复队列时保留顺序及重复歌曲的不同 occurrence，恢复不自动播放；缺失或禁用实例仍可见，真正播放时重新检查原实例/账号、能力及插件解析。删除按 occurrence ID，不以歌曲 ID 去重。
4. `QueueHistoryStore` 以 `QSaveFile` 原子保存；未来版本原位只读，损坏/超限文件先做唯一、仅所有者可读写的备份，失败则不覆盖。显式旧 JSON 输入只导入可无歧义映射的条目，保留原始备份并提示未迁移项；没有猜测旧文件名。仅与当前 generation/occurrence 匹配的真实 `playbackStarted` 计入历史一次。写失败可由告警条重试。
5. 启动先加载 Store 再加载 QML。恢复的非空队列不接管 Legacy 正在播放的控制；原队列弹窗提供明确的“恢复队列”选择，选择后通过 Coordinator 播放，secure 删除只调用 `removeOccurrence`。原 UI 结构保留，没有新增历史页面或来源专用 Host UI。

`quemusic_queue_history_integration_test` 使用真实 Local/Navidrome 插件包、Registry、真实 Local 扫描、Coordinator/fake sink、临时 Host 文件与禁用的 Navidrome 身份夹具，验证混合来源及重复 occurrence 重启后保留、Local 来源禁用后恢复可播放、旧路径缓存与 Host 直接 locator 被拒绝。Navidrome 夹具故意禁用且不访问外网，因此本测试**不**声称已验收 Navidrome 在线流播放；该能力由既有 Navidrome 专项测试覆盖。Local 插件私有索引使用 Qt 的 test-mode AppDataLocation，非生产 AppDataLocation；Host 文件、账户文件和音频 fixture 使用临时目录。

## 风险与后续阶段

- 本机集成测试若在进程退出前卸载 Navidrome 包，会在 Qt 对象销毁阶段触发悬空回调崩溃；测试通过现有 `PluginLease::pinLoadedPackage()` 仅对 Navidrome 包维持进程期代码映射。该现象在此夹具中可复现，尚未确认真实应用是否可达；应作为插件卸载/Qt 元类型生命周期的独立复核项，不据此宣称一般卸载安全性已验收。
- 旧 QML/`MusicApi` 直播放和本地特殊分支仍存在。Phase 5 必须使全部 Source 通过 Coordinator；Phase 6 继续动态 Source UI 化与移除 Legacy fallback。当前阶段没有完成这两项。
- “我的文件夹”自建集合、其他平台专用登录/账号 UI、Kugou/Netease 插件化与完整发行矩阵仍按最终架构后续阶段实施。没有推送、PR、合并或发布动作。
