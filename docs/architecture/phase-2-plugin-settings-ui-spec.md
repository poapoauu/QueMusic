# Phase 2：Plugin Settings 双层 UI 规格

> 状态：实施与全量回归完成，最终审查进行中
> 架构基线：[`QueMusic-Plugin-Architecture-Final-Plan.md`](./QueMusic-Plugin-Architecture-Final-Plan.md)
> 前置阶段：Phase 1 — Plugin UI API 1.0

## 1. 目标

将 Phase 1 发布的 `ISourceManagementUiProvider`、`PluginUiContext` 和 `QueMusic.PluginUI 1.0` 接入现有插件设置流程。普通插件继续使用 Host 渲染的 Settings Schema；需要认证或复杂管理流程的插件可显示自己的管理页。两个入口沿用当前 QueMusic 设置布局、主题和实例管理体验。

本阶段不迁移 Local Music，不迁移 Kugou/Netease 平台逻辑，不改造播放或队列，也不改变 Source SDK v2 的 IID、ABI 或类型布局。

## 2. 用户流程与界面行为

- 创建实例时，Host 按插件 manifest 与 Provider 声明判断该插件是否支持 `Create`。支持时打开通用 Custom Management 容器；不支持或没有 Provider 时使用现有 Schema 表单。
- 编辑实例时，Host 传入所选的 `sourceInstanceId` 和 `accountId`，并按 `Edit` 支持情况打开 Custom UI；无 Custom UI 时继续使用 Schema 表单。
- 同时提供 Schema 和 Custom UI 的插件可在自定义页中放置公开的标准 Schema 表单组件；普通字段与 Secret 字段仍由 Host settings bridge 保存，插件 QML 不接触 Secret 明文。
- Host 保留插件/实例列表、标题、启用/移除操作、容器边距、滚动行为与统一的加载、失败、缺失和不可用状态。插件页通过 `QueMusic.PluginUI 1.0` 组件和 `PluginTheme` 呈现。
- 页面无法创建或后端拒绝当前上下文时，容器显示通用错误状态和返回/重试入口，不显示异常、路径或 Secret 内容。

## 3. Host 装载与生命周期

1. Host 根据当前插件包、实例和 Create/Edit 模式构造身份上下文；Edit 必须具有有效实例和账号身份，Create 不继承其他实例身份。
2. Host 取得插件生命周期 lease 后，才访问 Provider、创建 backend 和 QML 页面。manifest、Provider descriptor、UI API 版本、模式支持及包内 QML 路径必须一致并通过 Phase 1 校验。
3. QML 页面只接收只读 `PluginUiContext`，不注入全局应用对象或私有 Host QML 类型。backend 的所有权必须符合 Provider 合约，且其生存期受 lease 保护。
4. 切换插件或实例、退出设置页、关闭窗口、插件卸载/重载或加载失败时，Host 依次使 context 失效、销毁 QML 页面与 backend，再释放 lease。异步回调必须忽略已失效上下文，不能访问已卸载插件对象。
5. 插件卸载或 manifest 变化时，当前管理页立即关闭/转为通用缺失状态；不得留下仍绑定到已卸载动态库的 QML 对象。

## 4. Host 服务边界

`PluginUiContext.settings` 与 `.host` 继续作为 Plugin UI API 1.0 的稳定 QObject 接缝，由 Host 提供具体对象。它们只提供完成管理流程所需的最小服务：

- 读取当前实例的非 Secret schema、公开配置值和 Secret 是否已配置的状态；
- 异步保存公开配置、保存或清除 Secret、查询 Secret 配置状态；
- 请求目录选择器并返回本地目录 URL；
- 发布面向当前 QueMusic UI 的普通成功/错误通知。

所有写入均绑定 Host 当前确认的插件/实例身份，不接受 QML 传入另一插件的可信身份。Secret 明文只在用户输入与 Host 到 secure store 的写入期间短暂存在；不得作为 schema presentation、QML property、持久化普通设置或日志内容返回。Host 不提供任意文件访问、任意窗口访问或其他插件对象访问。

实现可在 Host 内拆分 settings bridge 与 UI service QObject；不得扩展 Source SDK v2，也不得把 Host 私有实现类型加入 Plugin UI 公共 API。公开标准 Schema 表单只能依赖 `QueMusic.PluginUI 1.0` 的公开组件与 Host bridge。

## 5. 错误与状态处理

覆盖 Provider 缺失/不匹配、模式不支持、QML 解析或实例化失败、backend 创建失败、实例被禁用、插件卸载/重载、异步操作完成时上下文已失效等情况。Host 使用统一状态展示，不暴露动态库内部对象或敏感值。Schema-only 插件现有流程在 Custom UI 能力不可用时保持可用。

## 6. 主要实现触点（预估）

- `core/settings/PluginSettingsController.*`：公开所选实例的 UI descriptor/状态与受 Host 校验的 bridge 操作。
- `plugin-ui/`、`sdk/plugin-ui/v1/`：管理页 Host 容器及最小服务/标准 Schema 组件；新增内容需保持 Plugin UI API 1.0 向后兼容。
- `components/PluginSettingsPanel.qml`、`SettingsView.qml`：将现有设置入口接入通用创建/编辑页面和生命周期状态。
- `CMakeLists.txt`、插件 QML 模块与安装规则：发布新增公开 QML 类型并纳入 import 验证。
- `tests/`：Provider/Host bridge、异步 Secret、QML 容器、插件 lease/卸载和 schema-only 回归测试；增加 QR 登录流程的测试 fixture。

文件清单在实施计划阶段按代码依赖进一步收敛。

## 7. 兼容性约束

- Source SDK v2 IID 保持 `org.quemusic.MusicSourcePlugin/2.0`，ABI 保持 `2`；不修改任何 v2 接口布局。
- `ISourceManagementUiProvider` 既有 IID、descriptor 与创建方法保持不变。
- Plugin UI API 1.0 仅做向后兼容的 Host 接入和可选公开 QML 类型增补；Schema-only 插件和现有 v1.0 UI 插件继续可用。
- 插件管理 QML 仍只能 import 公开 Qt 模块与 `QueMusic.PluginUI 1.0`，并遵守包内相对资源规则。

## 8. 验收条件

1. Schema-only fixture 可创建/编辑实例，原有字段校验、Secret 隐藏、保存、取消和目录选择行为回归通过。
2. 带 Custom UI 的 QR fixture 可在 Create/Edit 模式加载，获得正确且只读的实例上下文，并能完成模拟认证及 Secret 保存/清除；QML 无法读回 Secret 明文。
3. Schema + Custom UI fixture 可在自定义页嵌入公开标准 Schema 表单，普通字段和 Secret 写入均由 Host 服务完成。
4. 非法 descriptor、模式不支持、backend/QML 创建失败均呈现通用错误状态；其他插件设置仍可操作。
5. 切换实例、关闭页面、禁用/卸载/重载插件后，context 失效、页面/backend 销毁、lease 释放顺序可由测试验证；迟到的异步回调无副作用。
6. Plugin UI 主题切换、可访问性、布局缩放和 import allowlist 检查通过；插件 QML 不依赖 QueMusic 私有模块或对象。
7. Source SDK v2 IID/ABI 检查保持不变；全量构建和 CTest 通过。
