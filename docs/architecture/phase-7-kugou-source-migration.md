# Phase 7：酷狗 Source Plugin 与 QR Management UI

设计基线：[最终架构方案](QueMusic-Plugin-Architecture-Final-Plan.md) Phase 7。日期：2026-10-11。

## 当前实际状态

Phase 6 动态 Source UI 代码出口已完成，见[出口报告](phase-6-exit-report.md)。酷狗尚未成为标准 v2 Source Plugin，不能把以下基础切片视为 Phase 7 完成。

本轮按已有代码检查：

- `api/KugouApi.*` 在 Host 内承担搜索、分类、榜单、歌手、资源和 KRC 歌词请求，使用平台整数结果协议；没有可供 v2 Session 使用的每请求身份与取消机制。
- `cpp/AccountManager.*` 在 Host 内进行签名、设备标识持久化、QR 获取/轮询、账号状态和 cookie 写入；`SettingsView.qml` 仍有酷狗专用 UI。
- 原 `getPersonalFm/getPersonalRadar` 实际取 TOP500/飙升榜。迁移时不能把公共榜单冒充私人发现算法，也不能为此虚报 Capability。
- Local/Navidrome 的 Registry、Session、Capability、Coordinator 和 Plugin UI 基线已存在，优先复用，不再建立平台编号播放链。

## 切片 1：插件私有 JSON 请求层

新增 `plugins/kugou-source/KugouApiClient.*` 和 CMake 私有静态库，**尚未链接到 Host、注册 Source、生成 manifest 或替代现有酷狗业务**。此结构为随后 Session/认证接线提供请求生命周期，避免在 Host 新增专用配置/业务 UI。

当前支持匿名歌曲搜索与资源信息请求，固定路由来自既有 KugouApi。搜索采用 HTTPS，不允许降级 HTTP；HTTPS 端点的真实服务可用性尚未验证。GET 参数由后续 Session 显式构造，传输成功只表示有效 JSON object，不等同于平台业务状态成功。

- 每请求 UUID；并发回包不以 action 名称或平台整数关联。
- 默认每 client 自建网络管理器，禁用自动 cookie 读取/保存和认证复用，不借 Host 账号。注入管理器用于无真实网络测试。
- 只接受预定义 endpoint，不接收外部任意 URL；拒绝自动重定向，15 秒传输超时，JSON 响应上限 1 MiB。
- 独立取消/全部取消、client 析构、网络管理器/Reply 消失和取消后的迟到回调保护；起始失败延迟发送，调用者可先记录 request ID，也可取消。
- HTTP 401/403/404/429/5xx 和网络失败映射到既有 SourceErrorV2；不把 URL、原始 body、headers 或 Qt errorString 复制到错误/日志。
- 兼容成对 KG_TAG 外层包装，不全局改写 JSON 内部字符串；非法 JSON、数组、超限响应拒绝。消费失败的观察者可删除 client，不继续访问已析构对象。

不改 Source SDK v2、插件 ABI、Plugin UI API 或持久化格式。此 client 是插件私有实现，不是新公共 SDK。

## 推荐的后续切片与验收

1. v2 Session/mapper：落实 SourceInstance/account/ref、搜索与可信媒体解析、实际动作能力、分页/取消/关闭；Coordinator 播放，不把解析 URL 或旧路径交给 QML。验证异步 generation、异实例/类型/hash 拒绝、业务错误、非 HTTP 媒体拒绝及权限边界，再注册真实插件包。
2. QR 认证协议：设备标识、签名、获取/轮询/超时/取消/重试、登录/退出、实例隔离和秘密存储全部插件负责。为令牌和账号建立可信绑定；不自动导入 Host Account.ini，不输出凭证日志。用协议夹具覆盖晚到回包、过期、错误和销毁，再单独进行真实扫码验收。
3. Management UI：实现独立 Plugin UI API provider/context 与 `import QueMusic.PluginUI 1.0` QML，复用稳定 Theme/UI Kit；QR 与账号 UI 仅在插件可用时出现。普通设置走 Schema，不引用 Style、AccountManager 或其他私有 Host QML。
4. Host 迁出：新插件和 UI 验收后去除酷狗登录实现/入口和 Core 直接引用；网易迁移不在此切片处理。测试禁用/删除插件后无可用酷狗来源、无酷狗登录 UI，以及无 Source 到旧平台 fallback。
5. 补齐既有目标数据能力、业务错误及真实集成测试后，按最终方案关闭 Phase 7。不能只靠“插件目录存在”、传输测试或空态截图宣称完成。

## 本轮验证

专用测试覆盖路由/请求策略、查询编码、并发身份、取消和迟到回调、异步失败、网络管理器/实例销毁、HTTP 错误脱敏、包装 JSON、响应大小和同步观察者析构。Qt 6.11.1/macOS x86_64 Debug 全目标增量构建成功；专项 CTest 通过，最后全量 **76/76 CTest 通过（66.04 秒）**。无真实账号、外部 API 或实际音频访问；真实 HTTPS 服务、QR 和标准 Source Session 尚未验收。
