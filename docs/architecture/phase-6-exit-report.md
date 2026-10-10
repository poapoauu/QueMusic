# Phase 6 出口报告：原 UI 动态 Source 化

日期：2026-10-10。开发分支：`codex/plugin-ui-api-v1`。

## 结论与范围

按[最终架构方案](QueMusic-Plugin-Architecture-Final-Plan.md)的 Phase 6 出口条件，**原 UI 动态 Source 迁移的阶段代码出口已完成**：新增 Source Plugin 不需要修改 Home、Search、Playlist、Favorites、File/Library、来源选择、Queue、Player 的平台 switch/case。数据和动作使用 Registry/Scope、Host Adapter、Capability 与 PlaybackCoordinator，不用平台整数或旧路径推测插件身份。

这不等于最终架构全部完成，也不等于真实服务和实际音频已验收。酷狗、网易账号/登录业务的插件化与 Legacy 全面删除仍属于最终方案 Phase 7–9。原 Gap Analysis 的早期建议编号与最终方案不同，后续阶段以最终方案为准。

## 已完成的阶段能力

| 范围 | 当前接线与边界 |
| --- | --- |
| Home 与来源选择 | 运行时 SourceInstance 选项；推荐、私人发现、歌单与详情只使用可信 Adapter 展示身份；不支持的业务保留空态，不借用旧平台数据 |
| Search / Playlist / Favorites | 动态来源、实体投影、嵌套浏览、定向分页/重试、有效能力失效；历史只重播仍在统一队列中的可信 occurrence |
| File / Library | Local Source Plugin 目录、分区批次上下文及能力检查；旧集合只走明确迁移入口，不直接播放旧路径；不以旧名称标记当前 Source |
| Download | Source 下载会话、取消、用户选择目标文件；与旧下载记录隔离，不把本地输出路径当作新插件媒体身份 |
| Queue | 安全展示、occurrence 定位、保留实际播放项的清理；Source 停播/缺依赖仍留在 Source 模式，不回退 Legacy |
| Player / 歌词 / 桌面控制 | 共享 Coordinator 播放动作和 Host 安全展示；切歌按 occurrence；歌词、封面、收藏、下载、频谱、系统媒体控制已有接线保留 |

Source Plugin 解析媒体资源，Core 负责实际播放。SourceInstance 与 Plugin 身份保持区分；多实例/权限/失效生命周期沿用已有 v2，不重写 SDK。

### 最后收口的缺口

- 搜索加载指示改为 Host `searchLoading`，移除共享 QLoadSign 对 `MusicApi.loaded/finished` 的隐式监听；快速停止/重启加载不被旧动画隐藏。
- Source 停播后的队列弹窗包含 sticky Source 标记，避免活跃播放标记清除后误借 Legacy 队列。
- 真应用首次原生 smoke 暴露 Host ThemeBinding 无法找到私有 Style/PluginThemeAdapter，已修复 Host 模块导入；插件仍只依赖 `QueMusic.PluginUI 1.0`，没有获得 Host 私有 QML 依赖。
- 搜索历史卡片两处字体设置修正为 `Style.settings.textTip`，消除 undefined 到 int 的绑定警告。
- 新增生产页面结构回归和测试构建专用原生窗口 smoke，不另造替身应用。

## 验证证据

1. Qt 6.11.1 / macOS x86_64 Debug：现有 `build-phase3` 全目标增量构建成功；不是全新目录 clean build。
2. 最后全量 CTest：**75/75 通过**。相关套件覆盖 Adapter、生产 QML 动作、播放/队列边界、分页/权限/实例失效、Local 目录、Plugin UI Theme、安装包及 smoke 隔离守卫。自动测试和夹具不能替代真实服务验收。
3. 真应用 Cocoa/Metal 窗口：隔离空配置运行，`report.json` 为 `passed=true`、`platform=cocoa`、`errors=[]`；9 张窗口截图。经过实际窗口鼠标事件切换本地目录标签，验证原页面加载、Source idle 队列、播放器选项及亮暗主题同步。复跑产物：`/tmp/quemusic-phase6-ui.nU8e2O/`，临时证据未提交到仓库。
4. 结构回归检查 Home/Search/Playlist/Favorites/File/Download/Player/Queue/歌词 Adapter 中的具体平台分派；现有 QML 行为测试覆盖动态第三方实例、禁用/移除、无能力拒绝及无 Adapter 空态。静态扫描是防回归约束，不是任意第三方插件端到端认证。

原生 smoke 捕获 ReferenceError、TypeError、undefined 类型赋值等运行错误；不声称所有日志零警告。macOS 上 SMTC 不支持提示、现有取色信号参数弃用提示不属于本次通过条件。截图是隔离空态，不代表真实数据列表、全屏/桌面歌词或所有原生对话框完成视觉验收。

复现步骤见[原生 smoke 运行说明](phase-6-ui-smoke.md)。历史实现与测试切片见[迁移记录](phase-6-original-ui-source-migration.md)。

## API / ABI / 数据风险

- 本次仅新增 Host `OriginalUiMusicAdapter.searchLoading/searchStatusChanged` 与测试定位 objectName，Host C++/QML 必须同步发布。
- Source SDK v2、Source 插件虚接口/ABI、独立 Plugin UI API、持久化 schema 未改变。
- ThemeBinding 对 `QueMusic 1.0` 的导入仅在 Host 桥接层；不可复制到插件。插件仍复用稳定 UI Kit / Theme Token。
- Native smoke 仅 `BUILD_TESTING=ON` 编入。复制程序、重定向配置与拒绝已有 Account.ini 是测试防误用保护，不是插件安全沙箱。

## 后续任务与验收门槛

1. 最终方案 Phase 7：将酷狗数据层和 QR/账号管理 UI 移到插件；普通设置由 Settings Schema 渲染，复杂业务使用 Plugin UI API。验收包括真实认证、取消/过期/重新认证、实例隔离、能力拒绝及原 UI 无平台分支。
2. Phase 8：网易数据层、认证与管理 UI 插件化；按相同边界验收，不把专用登录界面留在主程序。
3. Phase 9：在平台迁移具备条件后删除 Legacy MusicApi / Local / 平台特殊路径，做全仓生产路径扫描、干净构建、全量回归和持久化升级检查；本报告没有授权提前删去仍在用的旧业务。
4. 持续保留集成/发布门槛：真实 Navidrome 多实例与本地媒体的实际音频、权限撤销/实例卸载、恢复队列显式重播、带数据页面、下载保存对话框、全屏/桌面歌词和跨平台打包/ABI。当前未完成，不能由 offscreen CTest 或这次空配置 Cocoa smoke 抵扣。

Phase 6 的代码出口与原生空态 smoke 已具备证据；上述真实服务/设备/发布验收仍未关闭。
