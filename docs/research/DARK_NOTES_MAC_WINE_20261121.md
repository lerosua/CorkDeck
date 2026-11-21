# 《黑暗笔录》Windows 版的 Mac Wine 实测

测试日期为 2026-11-21。目标是验证 Steam AppID `2004770` 能否显示主菜单，以及玩家能否点击第一处可交互场景。

## 环境与样本

- Apple M4，32 GiB 内存，macOS 26.6.2（25G83）。
- `wine --version` 输出 `wine-11.0-rc4 (Staging)`。使用新建的独立 Wine 前缀。
- 从 Switch 的 DBI MTP 回读已安装的游戏本体，共 1,835 个文件、495,294,964 字节。配套的美术资料目录 `art-picture` 及其 ZIP 未参与测试。
- `habl.exe` 是 Windows x86-64 可执行文件，SHA-256 为 `9001b1248a4a499958d1e455991d6ebcdbf1d5ccb79d8b9e3e2e567287225d51`，与此前从该设备读取的副本一致。`resources/app.asar` 的 SHA-256 为 `7c3a09e3bb652566415863529b2b93ab4b87b7c43fdbf47041b3d176656de8d8`。

## 直接启动游戏

将游戏放到测试前缀的 `drive_c/DarkNotes` 下，在该目录运行以下命令。`WINEPREFIX` 指向测试前缀。保留游戏自带的 Steam 库和 `steam_appid.txt`。

```sh
WINEDEBUG=warn+seh ELECTRON_ENABLE_LOGGING=1 wine ./habl.exe
```

| 验证项 | 实际观察 |
| --- | --- |
| 主进程与 renderer | 在 macOS 进程表中同时看到主进程（PID 46336）和带 `--type=renderer` 的子进程（PID 46752）。第二次观察时对应 PID 为 48802 和 48822。 |
| 屏幕输出 | 游戏弹出错误对话框，英文原文为 `Steam initialization failed: Steam is not running.`。点击 OK 后应用退出。 |
| 主菜单 | 本次未看到主菜单。 |
| 第一处交互 | 本次未进入场景，无法验证点击。 |

Wine 日志显示 `SteamAPI_IsSteamRunning() did not locate a running instance of Steam`。该前缀内没有 Windows Steam 客户端。图形日志另有 `eglInitialize D3D11 failed with error EGL_NOT_INITIALIZED, trying next display type`，目前无法据此判定实际游戏场景的渲染结果。

## 与 Switch 的关系

Mac 上已经观察到真实 renderer 子进程。此前 Switch 在 `spawn_process` 返回 `0xc0000002`、`errno=88`、`host_child_pid=0`。本轮同时暴露出 Steam 初始化这一独立条件。建议等同一份游戏在 Windows Steam 加 Wine 的组合下显示主菜单并完成首次场景交互后，再决定是否投入 Switch 的独立 renderer。
