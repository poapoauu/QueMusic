# Phase 6：真应用原生窗口 smoke

此流程运行真实主程序、生产 QML、Host 服务与原生窗口，不是单独的测试 UI。范围限于隔离空配置；不登录、不连接真实服务、不开始实际音频播放。

## 前提与隔离

- macOS 有可用桌面会话；Debug 构建 `BUILD_TESTING=ON`，先完成构建再运行（不要并发构建和完整 CTest，打包测试会嵌套构建）。
- 每次使用新的 `mktemp` 目录和复制的 App；不得复用带账号数据的目录，也不得直接运行日常 App 做 smoke。
- `CFFIXED_USER_HOME` 重定向 macOS 配置位置，复制可执行文件隔离旧 AccountManager 在程序目录读取的 Account.ini。无需更改系统 `HOME`。
- 测试入口在读取设置/账号及平台身份写入前检查显式隔离根、配置路径、复制的可执行文件路径，并拒绝已有 Account.ini。无隔离根调用退出 64，由 CTest 守卫验证。此防误用检查不是安全沙箱。

## 运行

在已构建的仓库目录执行，按实际位置调整 `phase6_repo` 和 CMake 路径：

```sh
phase6_repo="$PWD"
phase6_smoke_root=$(mktemp -d /tmp/quemusic-phase6-ui.XXXXXX)
rsync -a --exclude='Account.ini' --exclude='logs' \
  "$phase6_repo/build-phase3/bin/QueMusic.app" "$phase6_smoke_root/"
env CFFIXED_USER_HOME="$phase6_smoke_root" \
  QUEMUSIC_PHASE6_SMOKE_ROOT="$phase6_smoke_root" \
  QT_QPA_PLATFORM=cocoa \
  QML_IMPORT_PATH="$phase6_repo/build-phase3" \
  "$phase6_smoke_root/QueMusic.app/Contents/MacOS/QueMusic" --phase6-ui-smoke
```

截图、report、日志及空配置写在临时隔离目录，保留供检查；不拷贝个人账号文件。`BUILD_TESTING=OFF` 不编入此 driver/入口，不能用于复现。

## 成功条件与限制

退出码 0、`report.json` 为 `passed=true`，平台为 `cocoa`，errors 为空，共 9 个 frame：Home、Playlist、Favourite、File、Download、Search、Source idle Queue、Source Player Options、暗色 Player Options。需要人工复核截图的布局、选中标签与弹窗；driver 不做像素相似度认证。

driver 使用原 Sidebar 导航、实际 File tab 鼠标事件、生产 Queue/Options，确认目录标签可见、公共 Theme 的字体及暗色状态同步。队列场景直接设置 Host 的 sticky Source 标记并保持无活跃曲目，验证该 UI 状态不退 Legacy；它不是一次真实播放后停播的端到端流程，播放生命周期另由现有 Coordinator/QML 测试覆盖。超时或捕获的 QML 类型/引用错误使运行失败。已知 SMTC 平台不支持提示与现有取色信号弃用提示不等于类型/引用错误，不能把报告理解为所有日志都无警告。

这是空态窗口 smoke，不覆盖真实认证、请求/分页、真实多实例媒体、实际音频、保存文件对话框、全屏/桌面歌词及跨平台 ABI。相关自动测试与真实集成/发布验收须单独执行，见[阶段出口报告](phase-6-exit-report.md)。
