# Phase 2 Plugin Settings 双层 UI 实施记录

日期：2026-09-27。架构基线仍为最终方案；本阶段只接入 Plugin UI，不迁移音源业务。

## 交付边界

Host 提供公开 settings/host QObject 合约，具体实现位于 `core/settings/`。
设置页保持现有插件、实例和 Schema-only 流程，增加受控 Custom Management 容器。
会话校验身份、manifest、Provider、模式和包内 QML 路径，取得 lease 后创建 backend。
原生 backend 通过公开 Context 父对象取得绑定实例的保存服务；QR fixture 的 token
不经过 QML。用户输入 Secret 只在输入控件与提交期间存在，不可通过桥接读回。

关闭顺序：context/service 失效 → 清空 Loader → 页面析构栈退出 → backend 销毁 → lease 释放。
控制器提前销毁时会话暂时脱离控制器，仍持有 lease，等待页面销毁后清理。
保存支持普通字段与 Secret 原子提交；完成信号和目录结果以 request ID 隔离。
标准表单使用公开 UI Kit，支持类型化值、条件显示、凭据保存/清除及安全草稿清理。

## 测试证据

环境：macOS 26.6.2、x86_64、Qt/QtTest 6.11.1、Apple LLVM 16.0.0、CMake 3.30.5。
构建目录 `build-phase2`，Unix Makefiles，Debug，`BUILD_TESTING=ON`。

- `cmake --build build-phase2 --parallel 4`：完整应用、SDK、fixture 和测试构建通过。
- `ctest --test-dir build-phase2 --output-on-failure`：51/51，通过；首次完整收尾运行 85.82 秒。
- 聚焦 `quemusic_plugin_ui|quemusic_plugin_settings|quemusic_source_registry_v2`：18/18，通过（16.42 秒）。
- 外部插件测试实际安装 PluginSdk、独立 configure/build、扫描生成后的 QR 页面、
  运行 Qt-only 模块 probe，再由 Host 加载外部插件并完成原生认证保存和卸载。
- `git diff --check`：通过；相对 Phase 2 起点 `4b68984`，`sdk/source/v2/` 与
  `sdk/plugin-ui/v1/` 无差异。Source IID 为 `org.quemusic.MusicSourcePlugin/2.0`，
  ABI 为 `2`；Management Provider IID 与方法布局不变。

| 验收范围 | 主要测试 |
| --- | --- |
| Schema-only 原有保存、目录、校验和私密草稿行为 | PluginSettingsQml、PluginSettingsController、SourceSettingsStorage |
| Create/Edit 身份与非法 Provider/路径/模式 | PluginManagementUiSession、PluginUiManifest、PluginUiProvider |
| 原生 QR 成功、失败、取消和关闭后回调 | PluginSettingsQml、安装后 PluginUiExternal |
| Secret 不可读回、原子提交、指定字段清除和回滚 | PluginUiSettingsBridge、SourceSettingsStorage |
| 页面/backend/lease 顺序、切换、隐藏、卸载、重载、控制器销毁 | PluginSettingsQml、PluginSettingsController、PluginUiUnload |
| UI Kit 主题、可访问性、缩放、公开 imports | PluginUiTheme、PluginUiQml、PluginSettingsQml、import allowlist |
| SDK、Registry、PlaybackCoordinator、Original UI 回归 | 全量 CTest |

## 兼容风险与限制

新增 bridge 在 Phase 2 首次发布；其中 `saveSettings()` 增补不改变已发布 Source v2
或 Management Provider 布局。实际二进制兼容仍要求 Qt、编译器、平台与 manifest
build key 匹配；本次未运行 Windows/Linux，也不是这些平台的 ABI 认证。
Plugin UI API 版本保持独立的 1.0，主程序私有 QML 仍不属于插件合约。

QR 是确定性的离线 fixture，不连接真实平台，也未模拟真实服务的网络错误。
Secret store 使用测试实现；真实系统钥匙串交互不由这些测试证明。
保存目前同步执行、异步通知完成；不能据此承诺后台 I/O 或 UI 无阻塞。
原有 Qt policy、MeshGradient 输出路径及 offscreen 字体提示仍存在；
没有将本阶段测试通过表述为所有历史警告已清除。

最终审查为作者单独自审（当前无 reviewer 子代理工具），不能替代独立审查。
本阶段不自动推送、合并或创建 PR。

## 实施决策及误判代价

1. 指定 Secret 清除新增 Host 存储事务，不复用“空值保持不变”的保存语义；误判会导致凭据清除或引用回滚出错。
2. 公开 QObject 合约与 Host 私有实现分离；误判会引入 SDK 私有依赖或运行库依赖环。
3. Loader 清空后等待页面完整析构，而不假设同步销毁；误判会让插件 QML 在动态库卸载后仍执行。
4. SettingsView 已通过隐藏面板退出，保留现有导航；误判会使其他退出路径遗留会话。
5. 新增原子 `saveSettings()`，保留已有单项方法；误判会让同时要求普通字段与 Secret 的创建流程无法提交。
6. Backend 的 QObject 父对象采用已初始化的公开 Context，Provider ABI 不变；误判可能影响依赖未约定私有父对象的插件。
7. 通知渲染 Host 通用成功/失败文案；代价是暂不显示插件特有业务细节。

## 审查收尾

单独自审检查存储事务、身份绑定、Provider/包路径、请求隔离、QML/backend/lease
顺序、设置页和安装后的公开 SDK 依赖。发现目录完成处理误接到移除确认窗口，
通过 `managementDirectoryResultReturnsToOriginatingService` 先失败再修复。
修复将处理迁回 FolderDialog，保存请求所属 service，取消/关闭后拒绝迟到结果。
修复后再次完整构建通过，全量 CTest 51/51 通过（53.61 秒），`git diff --check` 通过。

延期 Minor：公开 Schema 表单的字段和错误键尚未接入插件专用翻译目录。
原生插件属于受信任代码；import 校验与只读 Context 不是恶意插件安全沙箱。
