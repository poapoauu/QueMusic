# Plugin UI Backend Secrets 1.0

## 边界与兼容性

`sdk/plugin-ui/v1/IPluginUiBackendSecretsV1.h` 定义可选 C++ 服务，IID 为 `org.quemusic.PluginUI.BackendSecrets/1.0`。Backend 对 Context 的 settings 对象执行 `qobject_cast`；不支持时必须拒绝认证存储，不回退到 Host 私有类、明文文件或 QML token 属性。

该接口独立于 Source SDK v2 和既有 Plugin UI provider/SettingsBridge：不增加旧接口虚方法，不改变 SettingsSchemaV2 布局、旧 IID 或 Host 磁盘格式。Host 私有桥实现新接口，旧构造入口仍以空 backend-only 列表工作，普通 Schema 行为保持不变。SDK 的现有目录安装规则包含新头文件。

## 声明和调用

Host 以固定 package/source/account 身份、完整 Schema 和 backend-only Secret ID 列表构造存储桥。列表只允许 Schema 中的 Secret 字段；未知、重复及公共字段声明使整个桥失效。声明不是任意存储授权，也不能将未知字段动态升级为 Secret。

插件通过独立可选 `IPluginUiBackendSecretProviderV1` 声明字段 ID（IID `org.quemusic.PluginUI.BackendSecretProvider/1.0`），返回值必须在包加载期间保持不变。完整 Schema 仍由原 `IPluginSettingsProviderV2` 提供，无新 v2 字段或虚方法。普通控制器与 Management Session 在持有插件 lease 时发现声明，并共用 Host 的 `backendSecretPresentationV1` 验证/复制/投影；Host 持有的 ID/Schema 必须脱离插件静态数据，Provider 临时对象须先于 lease 释放销毁。未提供声明的旧插件保持普通 Schema 行为（包括既有空区块）。

完整 Schema 保留用于 SourceAccountStore 校验/事务；展示 Schema 删除 backend-only 字段及没有字段/动作的区块。普通 QML `secretConfigured` 对这些字段返回 false，`saveSecret`、`saveSettings`、`savePublicValues` 和 `clearSecret` 不能写入/清除它们。其他公共字段正常保存，并保留已有业务 Secret。

新接口仅供 C++，没有 Q_INVOKABLE、Q_PROPERTY 或 QML 注册：

- `backendSecretConfigured(id)` 返回元数据配置标志，不证明凭证可读、有效或账号已登录。
- `readBackendSecret(id)` 从 Host secure envelope 读取该字段；未声明、身份冲突、存储不可用、损坏或失效时返回空 optional。不能转发到 QML/日志。
- `saveBackendSecret(id, bytes)` 接受非空、最多 64 KiB、无 NUL 且可无损转换的 UTF-8 记录；复用 SourceAccountStore 的 secure 写入/轮换/回滚，不接收任意二进制。插件仍须校验自己的记录版本及身份绑定。
- `clearBackendSecret(id)` 复用命名 Secret 清除事务。

写入/清除返回请求 UUID，结果复用原 bridge 的 `operationFinished`。事务本身按现有实现同步执行，结果通知异步；失效 Context 后不再读写或投递完成通知。状态通知可能关闭 UI 并销毁服务，因此先登记完成通知，再触发 Registry/changed，且 Registry 通知后用 QPointer 检查存活；销毁后不继续访问对象。Backend 不能依靠通知授予平台权限；成功存储也不是服务端认证证明。服务不是隔离恶意 native 插件的沙箱。

## 当前接入状态与后续验收

切片 13 已接入生产 PluginManagementUiSession 与普通 PluginSettingsController 两处 Host 表单来源。普通表单、实例 configuredSecretFieldIds 展示使用投影；普通保存、连接测试/设置动作的 Secret 草稿拒绝业务字段。完整 Schema 仍交给原保存/测试/动作事务，以保留并校验既有业务凭证。非法声明阻止表单、保存/测试和 Management UI 创建，不回退为普通可输入 Secret。测试通过 native fixture 设置加载前声明，生产声明不允许动态改变。

酷狗生产 Schema 仍为空，尚未实现该声明或认证 Backend，不开放人工 token 表单；不能据此宣称生产扫码持久登录完成。后续将酷狗凭证 Schema/声明、认证 Backend、Context invalidation/lease 及存储后的 Session 绑定一起接入。真实 Keychain、服务端 token 验证和扫码另行验收。

当前夹具验收覆盖新 IID、C++ 查询且不出现在 Qt metaobject、字段/区块隐藏、四条普通写入路径拒绝、普通配置保存保留业务记录、UTF-8/大小/空/NUL 拒绝、非法声明、实例/包身份冲突、失效服务、保存/清除通知中立即销毁服务，以及酷狗私有凭证记录经服务读写/清除。

追加真实动态插件发现下的双入口验收：Create/Edit Context 自动绑定业务字段、普通字段可保存且保留原凭证、QML/普通保存/连接测试拒绝手工业务 token、未知/公共/重复声明的双入口拒绝、Context 失效后不再读取/晚写，以及服务释放后 lease 归零并允许卸载。构建/CTest 结果以 Phase 7 切片 13 记录为准；不等同于真实账户/Keychain/音频验证。
