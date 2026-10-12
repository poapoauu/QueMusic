# Plugin UI 1.0：离线 QR payload 控件

`PluginQrCode` 保留原 `source` 图片接口与资源限制，追加 `payload` 字符串。非空 payload 优先于 source，即使编码失败也不回退图片加载。`PluginQrLogin` 追加 `qrPayload`、`refreshEnabled`、`cancelEnabled`，旧插件默认行为不变。

payload 仅接受 1–213 个可打印 ASCII 字符，使用仓库既有纯 JS `api/qrcode.js` 的 byte-mode/M、版本 1–10 编码；不是 URL 网络请求，不执行内容或打开浏览器。Unicode/控制字符/超限/编码失败/不足一个像素单元的尺寸显示错误，不输出 payload 到日志。需要 Unicode 的业务应先规定自己的字节编码，本接口不隐式转码。酷狗把既有公开登录落地 URL 编码成 QR；凭证 token、用户 ID 和设备记录仍不属于此接口。

Canvas 按整数像素单元绘制，保留四模块 quiet zone；黑白为扫码光学要求，不跟随深浅主题反转。控件尺寸、页面和其他控件使用稳定 Theme Token。旧 `source` 的 HTTP 禁止规则不变；有 payload 时图片 source 清空，无远端图像/第三方 QR 服务。

算法在 CMake 配置时复制到独立 UI Kit 资源中并随 SDK 安装，保留原许可头；安装消费者不需要 Host 的 `api`/私有 QML/资源路径。原应用仍保留原 `api/qrcode.js` 资源路径，避免共享 QT_RESOURCE_ALIAS 破坏旧二维码 UI。

新增属性是 UI Kit 的兼容性追加，没有修改 Source SDK v2 或 C++ Provider 虚表/IID。反向兼容不代表新插件可运行于未提供这些属性的旧 UI Kit：目前酷狗包仅与同构建实验 Host 配套，仍默认关闭、无发行 install/bundle 规则；正式发布前须收敛 UI Kit 功能版本/最低运行时要求，不能仅用旧 `pluginUiApi: 1.0` 宣称支持任意历史 Host。

验收：旧 source 行为、模块独立导入、ASCII/边界/尺寸拒绝、图像 source 清空；macOS 测试使用仅链接测试程序的 Vision QR 解码器独立还原 URL 与较长 payload，不把算法自比对视为扫码证据。安装后的 Qt-only 外部进程也验证 payload 编码资源完整。酷狗页面使用受控协议/内存 SecretStore，不是实际手机扫码或服务端账号认证验收。
