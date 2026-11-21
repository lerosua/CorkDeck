# 《黑暗笔录》Windows 版在 Mac Wine 与 Switch 上的实测

测试日期为 2026-11-21。目标是验证 Steam AppID `2004770` 能否显示主菜单，以及玩家能否点击第一处可交互场景。

| 平台 | 2026-11-21 实测结论 |
| --- | --- |
| Mac Wine Staging + 已登录的 Windows Steam | **可玩，已验证至首次场景交互。** 能进入主菜单、开始新游戏，并点击幸福商城玻璃门进入门口近景。 |
| Switch / CorkDeck 当前测试版本 | **启动黑屏，当前版本暂不支持。** 游戏未显示主菜单，也未进入可交互场景。 |

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

## 安装 Windows Steam 后复测

在同一 Wine 前缀内安装 Valve 官方 Windows 客户端，复核 `habl.exe` 和 `resources/app.asar` 的 SHA-256，均与首次测试一致。客户端完成更新和文件校验，版本号为 `1794536281`。Steam 的 `webhelper.txt` 在 22:38:04（北京时间）记录了登录窗口 `SP DesktopLoginWindow_uid0` 加载完成。[Valve 官方下载页](https://store.steampowered.com/about/)

用户扫码登录后，从游戏目录启动同一份 `habl.exe`，窗口标题为《黑暗笔录[1.0.2]》。

| 验证项 | 实际观察 |
| --- | --- |
| Steam 与 renderer | 游戏日志显示 `SteamAPI_Init()` 成功加载 `steamclient64.dll`，随后记录 `login verify success.`。macOS 进程表同时出现游戏主进程（PID 65195）和独立的 `--type=renderer` 子进程（PID 65318）。 |
| 主菜单与剧情 | 确认开场提示后，标题画面显示「继续游戏」「新游戏」「相册」「设置」「退出」。进入新游戏后，电视播放幸福商城的新闻，点击可以推进播音员对白。 |
| 首个场景交互 | 复跑时到达幸福商城外景，右侧物品栏可见。点击玻璃门上的绿色提示圈后，画面由商城全景切换到门口近景，新的交互提示出现在门把手处。 |

用户确认第一次登录后的运行由本人点击退出。退出时日志出现 V8 `Cannot create a handle without a HandleScope`，随后出现 `0xc0000005`，Wine 退出码为 `5`。该记录发生在手动退出过程中；复跑完成了上述门口交互。本次验证范围到商城入口，后续关卡与长时间稳定性尚待测试。

## Switch 实机结果

在 Switch 上启动已安装的 `habl.exe` 后一直黑屏。游戏日志显示 Wine 登记了子进程和初始线程，但在 `spawn_process` 调用 `fork()` 时返回 `ENOSYS`（`errno=88`），Wine 返回 `0xc0000002`，`host_child_pid=0`。游戏未画出首帧。

后续用同版本的 ElectronProbe 单独测试独立 SystemModule。Horizon 成功启动子进程，但其 8 MiB 堆无法容纳 86.13 MiB 的 Wine 映像和 BSS，探针 loader 返回 `0xD5B`。独立 renderer 方案已暂停。[内存实测与暂停原因](../../TODO.md)
