# QueMusic Plugin UI API 1.0 Design

> 日期：2026-09-23
> 状态：已完成交互设计确认，等待规格审阅
> 架构基线：`docs/architecture/QueMusic-Plugin-Architecture-Final-Plan.md`
> 实施阶段：Gap Analysis Phase 1

## 1. 目的

QueMusic 需要允许 Source Plugin 提供登录、账号、服务器和特殊管理业务 UI，同时继续保持 QueMusic 的统一视觉和交互。Phase 1 冻结独立版本化的 Plugin UI API 1.0，建立公开 C++ 契约、QML module、Theme Token、完整 UI Kit 表面、manifest 校验和第三方构建边界。

Source SDK v2 已经是当前真实基线。本设计在现有 v2 上演进，不改变 `IMusicSourcePluginV2`、`IMusicSourceSessionV2`、Source v2 数据类型或现有 IID/ABI。

## 2. 成功条件

Phase 1 完成时：

1. 第三方 Source Plugin 能实现可选 `ISourceManagementUiProvider`，而不修改 Source SDK v2 接口。
2. 插件 QML 能只导入 `QueMusic.PluginUI 1.0` 和允许的 Qt 模块，并实例化全部公开组件。
3. 公共 QML module 不 import、链接或暴露主程序私有 `QueMusic 1.0`、`Style.qml`、`QButton`、`QCard`、`Options`、`mainMedia` 或 `MusicApi`。
4. `PluginTheme` 能接收 Host 的浅色、深色、缩放和字体更新，插件不能通过公共 API 反向修改 Theme。
5. manifest 中的 Plugin UI 版本、路径和 runtime Provider 保持一致；不一致时插件加载失败并提供稳定错误信息。
6. 活动 Plugin UI Provider 使用现有 `PluginLease` 阻止卸载，lease 释放后恢复卸载。
7. 仓库外 fixture 只依赖公开 SDK/QML module、Qt 和安装导出的 CMake 元数据即可 configure、build 和 load。
8. 现有 Source SDK v2、Navidrome、Settings Schema、PlaybackCoordinator 和 Original UI Adapter 测试无回归。

## 3. 非目标

以下工作明确留给后续阶段：

- Phase 2 才实现 Custom Management Page 的 Host 容器、导航入口、页面生命周期、卸载时关闭页面和通用错误页。
- Phase 2 才把现有 `PluginSettingsController` 与 Custom UI 实际组合；Schema 仍是当前默认设置路径。
- Phase 3 才实现 Local Source Plugin。
- Phase 1 不给 Navidrome、Kugou 或 Netease 增加平台专用管理页面。
- Plugin UI API 提供架构隔离和兼容性边界，不把 native 插件当作不受信任代码进行安全沙箱。

## 4. 总体架构

Phase 1 使用独立双模块，并保留一个 Host 私有适配层：

```text
sdk/plugin-ui/v1                         Public C++ contract
        │
        ├── quemusic_plugin_ui_sdk       Header-only/INTERFACE target
        │
        ▼
plugin-ui/qml                            Public QML implementation
        │
        ├── QueMusic.PluginUI 1.0        Versioned QML module
        └── PluginTheme                  Read-only semantic tokens
                    ▲
                    │ controlled update
core/plugin-ui                           Host-private adapter
                    ▲
                    │
components/Style.qml                     Existing private theme
```

`quemusic_plugin_ui_sdk` 只公开 Provider 和值类型，不依赖主程序。`QueMusic.PluginUI 1.0` 只依赖 Qt Quick/Controls 和公开 Plugin UI runtime 类型。`core/plugin-ui` 是唯一允许读取现有 `Style` 并更新公共 Theme 的位置。

## 5. 公共 C++ 契约

### 5.1 版本常量

公开头定义：

```cpp
#define QUEMUSIC_PLUGIN_UI_PROVIDER_V1_IID \
    "org.quemusic.SourceManagementUiProvider/1.0"
#define QUEMUSIC_PLUGIN_UI_API_V1 "1.0"
```

Plugin UI API 与 Source SDK v2 独立版本化。未来 Plugin UI 1.x 的兼容演进不能改变 Source SDK v2 的 ABI 值或 IID。

### 5.2 ManagementUiDescriptor

```cpp
struct ManagementUiDescriptor {
    QString uiApiVersion;
    QUrl componentUrl;
    bool supportsCreate = false;
    bool supportsEdit = false;
};
```

约束：

- `uiApiVersion` 在本阶段只能是 `"1.0"`。
- `componentUrl` 必须是插件包内、相对插件包根目录的本地 QML URL。
- URL 不能为空、不能是绝对路径、不能包含 `.`/`..` 路径段，不能使用远程 scheme。
- `supportsCreate` 和 `supportsEdit` 分别声明 Create/Edit 模式；两者均为 false 的 descriptor 无效。

### 5.3 PluginUiContextData

传给插件 backend 工厂的值类型为：

```cpp
enum class PluginUiMode {
    Create,
    Edit,
};

struct PluginUiContextData {
    QString pluginPackageId;
    QString sourceId;
    QString sourceInstanceId;
    QString accountId;
    PluginUiMode mode = PluginUiMode::Create;
};
```

Create 模式允许 `sourceInstanceId` 和 `accountId` 为空；Edit 模式要求两者均非空。Host 必须从 manifest 和 SourceRegistry 生成这些身份，不能接受插件 QML 自报身份。

### 5.4 ISourceManagementUiProvider

```cpp
class ISourceManagementUiProvider {
public:
    virtual ~ISourceManagementUiProvider() = default;
    virtual ManagementUiDescriptor managementUi() const = 0;
    virtual QObject *createManagementBackend(
        const PluginUiContextData &context,
        QObject *parent) = 0;
};

Q_DECLARE_INTERFACE(ISourceManagementUiProvider,
                    QUEMUSIC_PLUGIN_UI_PROVIDER_V1_IID)
```

Provider 是 Source Plugin QObject 可选实现的第二接口。禁止向 `IMusicSourcePluginV2` 增加 `settingsPage()`、Provider getter 或任何 UI 方法。

backend 所有权规则：

- 成功时必须返回以传入 `parent` 为 QObject parent 的对象。
- 无法为上下文创建 backend 时返回 null；不得返回无 parent 的借用对象。
- backend 是插件业务对象，不获得 `mainMedia`、`window`、`MusicApi`、`AccountManager` 或其它 Source 对象。

### 5.5 PluginUiContext QML 对象

Host 创建、插件只读的 `PluginUiContext` 暴露：

```text
pluginPackageId   string, readonly
sourceId          string, readonly
sourceInstanceId  string, readonly
accountId         string, readonly
mode              PluginUiMode, readonly
backend           QObject, readonly
settings          QObject, readonly
capabilities      QObject, readonly
host              QObject, readonly
valid             bool, readonly
```

`settings` 只包含非敏感设置值和 Secret 是否已配置的状态。Secret 明文不是普通属性。`valid` 在插件失效或页面关闭前变为 false；Phase 2 负责连接真实生命周期。

`host` 的稳定服务表面只包含：

- 请求返回上一级管理页面；
- 发送 Host 统一样式的 info/success/warning/error 通知；
- 异步保存或清除指定 Secret；
- 查询 Secret 是否已配置。

公共 Host service 不提供 Secret 明文读取。需要使用凭据的 native backend 通过 Host 管理的安全 C++ 存储路径工作，不把明文回传 QML。

## 6. Manifest 契约

有 Custom Management UI 的插件增加：

```json
{
  "pluginUiApi": "1.0",
  "ui": {
    "management": "qml/ManagementPage.qml"
  }
}
```

兼容规则：

| Manifest | Runtime Provider | 结果 |
| --- | --- | --- |
| 无 UI 声明 | 无 Provider | 有效，继续使用 Settings Schema |
| 有 UI 声明 | 有匹配 Provider | 有效 |
| 有 UI 声明 | 无 Provider | 加载失败 |
| 无 UI 声明 | 有 Provider | 加载失败，防止未审计 UI 入口 |
| 不支持的 `pluginUiApi` | 任意 | 发现失败 |
| UI 路径越界、远程或缺失 | 任意 | 发现失败 |
| descriptor 与 manifest 版本或路径不一致 | 有 Provider | 加载失败 |

现有 Navidrome manifest 没有 Custom UI 声明，因此保持有效且继续走 Settings Schema。

`PluginManifest` 增加只读字段：

- `pluginUiApiVersion()`；
- `managementUiRelativePath()`；
- `hasManagementUi()`。

`PluginManager::load()` 在完成 Source v2 cast 后执行 Provider/manifest 一致性验证。失败沿用已有 failed plugin 生命周期，不泄漏 loader 或 plugin root。

## 7. Lease 与卸载

Phase 1 不引入第二套生命周期系统。Host 按以下顺序访问 Provider：

1. `PluginManager::acquire(packageId)` 获取 `PluginLease`；
2. 从 `pluginInstance(packageId)` 执行 `qobject_cast<ISourceManagementUiProvider *>`；
3. 校验 descriptor；
4. 创建 backend；
5. 在 Provider/backend/context 的整个使用期保留 lease；
6. 销毁 context/backend 后释放 lease。

现有 `PluginManager::unload()` 在 active lease 存在时返回 Busy。Phase 1 测试该路径，不改变 `PluginLease` ABI。

## 8. QueMusic.PluginUI 1.0 QML Module

### 8.1 模块边界

模块 URI 固定为：

```qml
import QueMusic.PluginUI 1.0
```

公共模块允许依赖：

- `QtQuick`；
- `QtQuick.Controls.Basic`；
- `QtQuick.Layouts`；
- `QtQml`/`QtCore`；
- Plugin UI 自身的 C++ runtime 类型。

公共模块不得 import `QueMusic 1.0`，不得引用 `components/` 下的私有控件，也不得读取任何应用全局 context property。

### 8.2 公共组件表面

1. 容器：`PluginPage`、`PluginScrollPage`、`PluginSection`、`PluginGroup`。
2. 文本：`PluginLabel`、`PluginDescription`、`PluginSeparator`。
3. 操作：`PluginButton`、`PluginPrimaryButton`、`PluginDangerButton`、`PluginIconButton`。
4. 输入：`PluginTextField`、`PluginPasswordField`、`PluginNumberField`、`PluginUrlField`、`PluginDirectoryField`。
5. 选择：`PluginSwitch`、`PluginCheckBox`、`PluginComboBox`。
6. 状态：`PluginStatus`、`PluginBadge`、`PluginBusyIndicator`、`PluginErrorState`、`PluginEmptyState`。
7. 业务组合：`PluginAccountCard`、`PluginServerCard`、`PluginQrCode`、`PluginQrLogin`。

### 8.3 共同交互规则

- 所有交互控件支持 `enabled`、Tab/Backtab 键盘焦点、清晰的 focus ring 和可访问名称。
- hover、pressed、checked、disabled 和 busy 状态只使用 `PluginTheme` token。
- 文本使用 Theme 字体 token，并随 `scaleFactor` 更新 implicit size。
- 控件不暴露内部 background、contentItem 或私有 Item alias；公共属性只描述业务内容和状态。
- 动画限于颜色、opacity 和小幅 scale，必须在 disabled 或系统降低动效时安全降级。

### 8.4 组件特定规则

- `PluginPasswordField` 默认遮蔽文本，不提供持久化；提交后调用方负责清空。
- `PluginNumberField` 提供 `from`、`to`、`stepSize` 和数值校验。
- `PluginUrlField` 提供 URL 输入与 `acceptableInput`，不执行网络请求。
- `PluginDirectoryField` 显示路径并发出 `browseRequested()`；Phase 2 由 Host 打开平台目录选择器。
- `PluginComboBox` 接收 model、textRole 和 valueRole，不假设平台枚举。
- `PluginStatus`/`PluginBadge` 使用 neutral/success/warning/error 语义状态。
- `PluginErrorState`/`PluginEmptyState` 提供标题、说明和可选 action 信号。
- `PluginAccountCard`/`PluginServerCard` 只展示通用身份、状态和 action，不包含平台字段。
- `PluginQrCode` 接收本地/resource/image-provider URL；无效 URL 显示通用错误状态。
- `PluginQrLogin` 组合 QR、状态、刷新和取消信号；二维码生成、轮询和认证属于插件 backend。

## 9. PluginTheme

`PluginTheme` 是 `QueMusic.PluginUI 1.0` 的只读 singleton。

### 9.1 Token

颜色：

```text
background surface surfaceHover primary
textPrimary textSecondary border
success warning danger
```

尺寸与字体：

```text
spacingSmall spacingMedium spacingLarge
radiusSmall radiusMedium radiusLarge
fontCaption fontBody fontTitle
scaleFactor dark reducedMotion
```

### 9.2 更新路径

`PluginTheme` 的 QML 属性为 readonly。Host 私有 `PluginThemeAdapter` 注册在主程序私有模块中，读取 `Style` 并调用非 QML 公开的 C++ 更新入口。插件没有导入该适配器的权限。

Theme 尚未初始化时使用固定默认浅色值，保证仓库外 fixture 和设计工具能加载。Theme 更新在 GUI 线程原子应用一组 token，并只发出一次 change notification，避免控件观察到半更新状态。

## 10. Import Allowlist

打包校验器逐个解析插件 `qml/` 下的 QML import 声明。

允许：

- 第 8.1 节列出的 Qt 模块；
- 精确的 `QueMusic.PluginUI 1.x`；
- 插件包 `qml/` 根目录以内的相对组件 import。

拒绝：

- `QueMusic 1.0` 或其它 QueMusic 私有 URI；
- 逃逸插件包根目录的相对 import；
- 绝对文件 import；
- 远程 URL import；
- 未列入 allowlist 的模块。

校验失败包含文件、行号和被拒绝的 URI。校验器在 fixture 打包及 CI 中运行，Phase 2 再接入正式第三方插件安装流程。

## 11. 错误与降级

- Manifest 结构或路径错误在 discover 阶段拒绝，不加载 native library。
- Provider 缺失、意外存在或 descriptor 不一致在 load 阶段拒绝，并使用现有 failed plugin 状态。
- `createManagementBackend()` 返回 null 时由未来 Phase 2 容器显示通用错误；Phase 1 contract test 固定 null 行为。
- 公共 QML 控件的 null backend、空 model、空文本和无效图片不得访问 undefined 私有对象或导致 engine error。
- 插件缺失、instance switching、context invalidation 和异步回调抑制由 Phase 2 页面生命周期实现；Phase 1 确保 context contract 能表达 `valid=false`。

## 12. 构建、安装与仓库外验证

新增 CMake target：

```text
quemusic_plugin_ui_sdk       Public INTERFACE headers
quemusic_plugin_ui           QueMusic.PluginUI QML/runtime target
```

公开 include 根只包含 `sdk/plugin-ui/v1`，不能把 `core/`、`components/` 或应用根目录加入第三方 include path。

安装/Bundle 输出包含：

- 公开 Plugin UI headers；
- CMake target metadata；
- `QueMusic/PluginUI` 的 qmldir、QML、qmltypes 和所需 runtime；
- 插件自有 `qml/` 和 resources。

仓库外 fixture 在独立源码/构建目录中：

1. 实现 Source v2 和 `ISourceManagementUiProvider`；
2. 提供只 import `QueMusic.PluginUI 1.0` 的 `ManagementPage.qml`；
3. 使用安装导出的 targets configure/build；
4. 通过 manifest lint；
5. 被 PluginManager 加载并通过 Provider cast。

## 13. 测试矩阵

### 13.1 `quemusic_plugin_ui_contract_test`

- IID 和 API 版本常量；
- descriptor 默认值和有效组合；
- Create/Edit identity 规则；
- backend parent/null contract；
- `IMusicSourcePluginV2` IID、ABI 宏和类接口保持原值。

### 13.2 `quemusic_plugin_ui_manifest_test`

- Schema-only manifest 继续有效；
- UI 1.0 manifest 有效；
- 缺版本、未知版本、缺 management、绝对/越界/远程/缺失路径被拒绝；
- UI 声明/Provider 缺失与 Provider 未声明两种 mismatch 被拒绝；
- descriptor version/path/mode mismatch 被拒绝；
- 失败后 loader 生命周期无泄漏。

### 13.3 `quemusic_plugin_ui_qml_test`

- `import QueMusic.PluginUI 1.0` 成功；
- 全部公开组件逐一实例化；
- null backend、空 model、空状态和无效 QR URL 安全；
- Tab/Backtab 焦点顺序、disabled 输入抑制和 accessible name；
- 典型窄宽和长文本不产生 QML warning/error。

### 13.4 `quemusic_plugin_ui_theme_test`

- 默认浅色 token；
- Light/Dark 完整 token 更新；
- 运行时切换只发出一次完整更新；
- scale/font 改变更新 implicit size；
- disabled/focus/hover 状态使用语义 token；
- 插件 QML 无法给只读 token 赋值。

### 13.5 `quemusic_plugin_ui_unload_test`

- Provider cast 前取得 lease；
- lease 活动时 unload 返回 Busy；
- backend/context 销毁并释放 lease 后 unload 成功；
- Provider mismatch 加载失败不遗留 active lease。

### 13.6 Import lint 与外部 fixture

- 允许公开 Qt、PluginUI 和包内相对 import；
- 拒绝 `QueMusic 1.0`、绝对/越界/远程和未知 import；
- 错误包含文件、行号和 URI；
- 外部 fixture 只依赖公开 target 完成 configure/build/load。

### 13.7 回归

运行完整 configure、build 和 CTest。现有 36 项基线测试必须全部通过，新测试数量和名称记录在实施结果中。

## 14. 推荐实施切分

1. 先以 contract test 冻结 C++ 类型、IID 和 Source v2 不变条件。
2. 再扩展 manifest，并以 fixture 验证 Provider 一致性和 lease。
3. 建立独立 QML module、PluginTheme 和私有 Theme adapter。
4. 按容器/文本、操作、输入/选择、状态/业务组合四组完成 UI Kit，每组遵循测试先行。
5. 增加 import lint 和仓库外 fixture。
6. 完成安装/Bundle、全量回归和公开开发文档。

每一切分都必须先观察目标测试因缺失行为而失败，再写最小实现使其通过。Phase 1 不夹带 Custom Page Host、平台登录 UI 或 Local Source Plugin。
