# 酷狗匿名公开协议 smoke

目的：使用真实 `KugouSourceSession`/client/mapper 验证固定 HTTPS 榜单或歌手数据路径，不使用独立 HTTP 脚本的成功替代插件证据。该开发工具只在 BUILD_TESTING 下构建，不安装、不打包，不加载主程序、插件管理器、账号存储、SecretStore 或播放 Sink。

## 运行与边界

在已经完成 BUILD_TESTING 构建的仓库目录运行：

```sh
./build-phase3/bin/quemusic_kugou_public_smoke --help
./build-phase3/bin/quemusic_kugou_public_smoke --live-public
./build-phase3/bin/quemusic_kugou_public_smoke --live-public-artists
```

只有精确的单一 `--live-public`（榜单）或 `--live-public-artists`（歌手）参数会构造 Session 并访问外网；不能组合，两者无需连续执行。无参数或未知/额外参数返回 2，`--help` 返回 0。不接受 URL、域名、凭证、配置路径或环境变量覆盖。正常 CTest 仅检查 help/缺少显式启用，不注册真实网络运行。

工具使用独立匿名 `kugou/public-smoke` 实例，打开 Session，获取前两条榜单、若有则取本地快照下一页，再获取首个榜单的前两条歌曲及最多下一页。查询和身份、白名单 DTO、类型、分页结果经过插件及工具验证；没有资源解析或实际音频播放。45 秒单调/Precise 总限时，原请求层仍保留 15 秒传输限时和 1 MiB 响应上限；退出前关闭 Session，取消在途请求。

歌手模式独立请求热门歌手前两条、至多一个续页，再以首个 Artist 完整 ref 复核可选 Artist Browse v1 为 Tracks，获取该歌手歌曲及至多一个续页；不依赖榜单端点先成功。该工具直接调用标准 Provider，不加载 Host，不据此宣称 Host 导航/视觉验收。未知 total 仍遵循 Session 的满页探测策略，空目录明确失败，空歌曲页为合法采样结果。

只输出版本化 JSON 摘要：结果、阶段、固定错误键、可选 HTTP 状态、每阶段数量/hasMore/明确免费候选数；不输出原始响应、Qt errorString、headers、资源 URL、歌名、hash、用户或设备记录。`authenticated` 与 `audioTested` 固定为 false。成功返回 0，协议/网络/超时失败返回 1；成功仅证明本次采样匿名数据路径，不证明登录、账号权限、全量平台字段或 Phase 7 完成。

切片 23 在 schemaVersion 1 摘要中增加 `probe`（`ranks`/`artists`）字段；保留旧键及 `--live-public` 的榜单语义，摘要消费者应忽略未知新增字段。这是开发工具格式增量，不改变插件/Source SDK ABI、账号或磁盘格式。

## TLS 失败的处理

不得为通过 smoke 添加 `ignoreSslErrors`、关闭主机名/证书校验、HTTP 降级、重定向绕过或自动换到未验证服务。失败保持 SourceError 网络拒绝，不发布页面或伪造内置榜单。另有无外网夹具模拟 `SslHandshakeFailedError`，断言无成功回调、无第二次降级请求及错误脱敏。

2026-10-12 的初步只读诊断：固定 `mobilecdnbj.kugou.com` 榜单请求在本机 TLS 主机名校验失败，Python/OpenSSL verify code 为 62，curl 返回 60、HTTP 000；仓库已使用的 `mobilecdn.kugou.com` 同一路由探测也在 TLS 阶段失败。环境变量未配置代理、hosts 未发现这两个域名的覆盖；这不足以确定平台全局故障或排除网络路径因素。没有关闭校验、取得可信业务 JSON、变更端点或接受失败连接中的响应，不把这些失败当作字段/分页验收。真实 Qt Session 运行结果另记 Phase 7 切片 19。

2026-10-13，独立歌手模式实际返回 1，摘要 `probe=artists / stage=catalog / errorKey=kugou.requestFailed / pages=[]`，认证/音频标志均为 false。同日对固定 HTTPS `/api/v3/singer/rank` 路径的只读 curl 校验返回 60（证书 SAN 与 `mobilecdnbj.kugou.com` 不匹配）、HTTP 000；没有 `-k`、重定向/HTTP 降级或替换端点。工具通用错误本身不能确诊原因，独立 TLS 核验说明本机该连接失败；仍未取得可信业务 JSON，不代表平台全球服务状态，也不完成实际歌手链路验收。
