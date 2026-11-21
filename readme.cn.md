# CorkDeck 简体中文介绍

CorkDeck 让你在 Nintendo Switch 上从 SD 卡启动 Windows 游戏，也可以登录自己的 Steam 账户，浏览游戏库并直接在主机上下载安装。它运行在 Switch 自带的 Horizon OS 上，用手柄就能选游戏、调整设置和查看启动结果。

[English](README.md) · [Русский](README.ru.md) · [安装指南](docs/INSTALL.md) · [游戏兼容性记录](docs/COMPATIBILITY.md) · [从源码构建](BUILDING.md)

![CorkDeck 在 Switch 上显示已安装的 Steam 游戏](docs/screenshots/library.jpg)

## 先了解游戏兼容性

CorkDeck 仍处于实验阶段。较早的 Direct3D 9 至 11 游戏是比较现实的尝试对象。项目记录了每款游戏实际测到的阶段，例如《辐射 3》Steam 版到达了启动器和主菜单，《刺客信条》Steam 版播放到了开场动画。这两项记录都还没有覆盖实际游玩。部分游戏依赖 Valve 桌面客户端的服务或保护机制，已知无法启动。[实测结果与已知限制](docs/COMPATIBILITY.md)

![从 CorkDeck 0.2 启动的《辐射 3》Steam 版，在 Switch 上进入主菜单](docs/screenshots/fallout3-on-switch.jpg)

本文截图来自运行 CorkDeck 0.2 的 Switch，展示的是当时的界面。中文菜单可以在「设置 → 界面 → 语言」中选择「简体中文」，默认跟随主机语言。

## 下载与安装

准备一台可运行 Atmosphère 自制软件的 Switch，并使用 emuMMC（虚拟系统）。SD 卡需为 CorkDeck 预留约 60 MB、为 Windows 组件预留约 1 GB，游戏另计。标准安装首次下载组件以及登录 Steam 都需要网络。游戏由用户自行准备。

1. 全新安装可从 CorkDeck 上游发布页下载以 CorkDeck 命名的安装包，例如 `CorkDeck-0.3e.zip`。中文分支测试版的构建方法见下文。[上游发布页](https://github.com/plv890/CorkDeck/releases)
2. 解压安装包，把其中的 `switch` 目录复制到 SD 卡根目录。主程序应位于 `sdmc:/switch/wine/wine-nx-runtime.nro`。
3. 按住 **R** 启动一款 Switch 游戏，进入 Homebrew Menu，运行 `wine-nx-runtime`。按「首次设置」的提示安装 Windows 组件和 HOME 菜单入口。
4. 首次设置还提供可选的启动补丁。安装前先备份 SD 卡上的 `atmosphere` 和 `bootloader` 目录，也可以按 **B** 跳过这一步。如果安装了启动项，按指南重启并在 Hekate 中选择 CorkDeck。[完整安装与升级指南](docs/INSTALL.md)

**升级已有安装**时，先在「− → 电源」中关闭 CorkDeck，备份原有 NRO，再覆盖同一路径下的 `wine-nx-runtime.nro`。保留整个 `switch/wine` 目录，里面还有游戏、配置和保存的会话。Autorun 也使用这个目录；从 Autorun 迁移时请按安装指南操作。

## 添加并启动游戏

**从 SD 卡添加。** 将完整游戏目录复制到卡上，例如 `switch/wine/drive_c/Games/`。打开游戏库，按 **X** 添加程序，选中游戏的 `.exe`，再按 **A** 进入详情页启动。如果游戏自带启动器，可以在详情页指定先运行哪个程序。

**从 Steam 安装。** 打开「− → 维护 → Steam 账户」，用手机 Steam 应用扫码，或输入账户名和密码。进入「游戏库 → 未安装」，选择游戏下载安装。下载页会显示进度、速度和预计剩余时间。中断后可以继续下载；多次失败时可重试、取消或重新开始。会话可以选择加密保存在本机，方便下次登录。CorkDeck 内置的 Steam 客户端提供账户、游戏库和下载功能；好友、成就、云存档、叠加层和在线多人游戏目前不在支持范围内。

| 按键 | 常用操作 |
| --- | --- |
| − / + | 打开主菜单 / 状态面板 |
| A / B | 确认 / 返回 |
| X / Y | 操作菜单 / 搜索 |
| L / R | 切换游戏库标签 |

## 中文分支的 Steam 游戏库

以下功能对应 `codex/chinese-menu` 分支的开发和测试构建。上游发布包的具体功能以相应版本说明为准。

- **未安装游戏的封面**优先从 SD 卡读取。缺图时直接向 Steam 请求，成功取得的图片会缓存在卡上，无需填写第三方图片服务的 API Key。
- **游戏详情**优先展示 CorkDeck 的 Switch 实测记录。缺少实测时，会根据 Steam 商店列出的 Windows 最低配置给出判断和理由；资料不足时也会说明。Steam Deck 评级单独列出，它描述的是游戏在 Steam Deck 上的适配情况。
- **游戏库缓存**按账户保存。使用已保存的会话重新打开应用时，会在连接 Steam 前读取上次同步的列表。完整缓存默认复用 24 小时，过期后再联网刷新。此功能已在 Switch 上验证过缓存命中。
- **断网重连**已加入测试构建。连接中断时可继续浏览缓存，应用会在后台重试。右上角会显示「Steam 离线，等待重连」或「正在恢复 Steam 会话」，Steam 确认登录成功后显示「Steam 已登录」。同一进程内断网再联网的流程已通过模拟测试，真机结果待确认。

## 每款游戏可以单独设置什么

游戏库支持收藏、分类、搜索，以及网格和列表视图。在游戏详情页可以查看即将使用的 Windows 组件、图形渲染器、处理器转译器和上次退出结果。渲染器、线程、内存等选项都可以按游戏调整；设置页会说明选项的作用、可能影响，以及当前值来自默认设置、全局设置还是该游戏的配置。

| 游戏详情页 | 单个游戏的设置页 |
| --- | --- |
| ![游戏使用的组件、渲染器及上次启动结果](docs/screenshots/game-page.jpg) | ![当前设置的用途和可能影响](docs/screenshots/per-game-settings.jpg) |

启动前，CorkDeck 会检查程序导入的 DLL 并提示缺失项，可自动补装 Microsoft Visual C++ 2015–2022 运行库。Steam 安装脚本中的注册表项和文件夹复制操作也可在安装时应用。进入游戏后可显示 FPS 和帧时间。CorkDeck 将频率和电压设置交由用户在 Horizon OC 中管理。

## 在 Apple Silicon Mac 上构建

Mac 端使用 Homebrew 的 Bison 生成 Wine 所需文件，Switch 原生程序在 `ghcr.io/autorunhq/switch-dev:2026.10.12` 的 Linux ARM64 容器中编译。构建文档列出了工具链、镜像摘要、NRO 构建命令和 SD 卡 ZIP 的校验步骤。已有安装若需离线更新 Windows 组件，可使用 `horizon-wine/tools/package-offline-update.py` 将 NRO 与固定版本的 DLL 打入同一个更新包。[Mac 构建与离线打包步骤](BUILDING.md)

## 日志与问题反馈

每次启动游戏的报告保存在 `sdmc:/switch/wine/reports/`，运行日志位于 `sdmc:/switch/wine/logs/`。排查游戏库或登录问题时，从 `autorun_runtime.log` 的 `[NETDIAG]` 行开始。重开应用后，前两次日志分别轮转到 `autorun_runtime.1.log` 和 `autorun_runtime.2.log`。

| 日志标记 | 可以核对的内容 |
| --- | --- |
| `steam.library.cache.load` / `steam.library.cache.save` | 是否命中缓存、读到多少款游戏、同步后是否写卡成功 |
| `steam.reconnect` | 自动重连的尝试次数、失败原因和结果 |
| `steam.topbar` | 右上角上一次和本次实际绘制的状态文字 |

反馈兼容性问题时，请写明 Switch 型号、Atmosphère 和 CorkDeck 版本、游戏版本、启动的 EXE 及复现步骤。请注明实际测到了启动器、主菜单还是游戏内，以及游玩时长。分享日志前请移除账户标识和私人路径，并保管好 `steam-session.vault`、密码、令牌及登录二维码。上游 CorkDeck 项目接受英语和俄语报告。[反馈指南](CONTRIBUTING.md) · [上游问题跟踪](https://github.com/plv890/CorkDeck/issues)

## 项目来源、作者与许可

CorkDeck 源自 Autorun（原名 Wine-NX）。Autorun 作者与贡献者完成了在 Switch 上运行 Windows 软件的底层工作。CorkDeck 在此基础上开发了主界面和 Steam 工作流程。运行时使用 Wine、FEX 和 Box64，图形栈包含 Mesa、DXVK 和 VKD3D-Proton。[Autorun 项目](https://github.com/autorunhq/autorun) · [分支来源](FORK.md) · [上游项目介绍](README.upstream.md)

CorkDeck 作者 **Maj0ra'sMask（@plv890）** 负责产品设计、系统工程与真机测试。按照作者说明，CorkDeck 自身的代码和文档主要由 OpenAI Codex / GPT 和 Anthropic Claude 在其指导下编写，并通过自动化测试和真机运行检查。[作者主页](https://github.com/plv890) · [致谢](CREDITS.md)

Wine 及其 Horizon 移植改动采用 LGPL-2.1-or-later，其他组件沿用各自的许可证。发布包内的许可证位于 `switch/wine/licenses`。CorkDeck 是独立社区项目，与任天堂、Valve、微软及 Autorun 项目均无隶属或背书关系。相关商标归各自权利人所有。[项目许可证](LICENSE) · [LGPL 条文](COPYING.LIB) · [组件声明](NOTICES.md) · [第三方声明](THIRD_PARTY_NOTICES.md)
