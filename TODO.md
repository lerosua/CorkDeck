# CorkDeck 待办

## Switch 上的 Electron 独立 renderer（暂缓）

2026-11-21 在 Switch 测试 ElectronProbe 和《黑暗笔录》时，两者的双向启动管道、`new_process` 和初始线程登记均成功。Wine 分配的 PID/TID 分别为 `104/108` 和 `136/140`。两者都在 `spawn_process` 调用 `fork()` 时得到 `ENOSYS`（`host_errno=88`），Wine 返回 `0xc0000002`，`host_child_pid=0`。探针创建了可见窗口，随后报告 `renderer-gone: launch-failed`；两者均未画出游戏首帧。

同一份 `habl.exe` 在 Mac Wine Staging 下启动了独立 renderer，随后因 Windows Steam 客户端未运行而退出。主菜单与首个场景的交互仍待验证。[Mac Wine 实测记录](docs/research/DARK_NOTES_MAC_WINE_20261121.md)

- [ ] **先取得可玩的对照结果。** 在运行着已登录 Windows Steam 客户端的 Wine 环境中，用同一份游戏进入主菜单，点击第一处可交互场景并记录画面变化。届时再决定是否开发 Switch 子程序。
- [ ] **验证 Horizon 能同时运行父子进程。** 用可单独移除的最小子程序测试启动方式。分别记录启动返回码、父子两端读取的 Horizon PID 和退出结果，确认父进程持续运行、子进程可回收，并测量 4 GiB Switch 上启动前后的内存余量。子程序的打包和安装方式待验证。
- [ ] **验证子进程能接回父进程的 Wine 会话。** 现有 Wine 句柄表和 Mojo 管道缓冲区位于父进程内。需要通过 Horizon 跨进程通信传递启动参数和继承句柄，让子进程接入对应的 Wine 进程与初始线程，并用 ElectronProbe 验证 Mojo 两端各完成一次发送和接收。
- [ ] **验证真实 renderer 和游戏交互。** 由子程序加载游戏的 `--type=renderer` 入口。先验证 ElectronProbe 显示首帧并响应输入，再测《黑暗笔录》的 Steam 初始化、主菜单和第一处交互。

## Proton 移植与逐游戏运行时选择

Valve 的 `proton_11.0` 支持 ARM64 构建，产物面向 Linux。生成能在 Switch 的 Horizon OS 上运行的 NRO，还要适配 CorkDeck 的线程、内存、文件访问和 x86 转译接口。目前完成了源码评估，真机移植可行性待验证。[Valve ARM64 构建说明](https://github.com/ValveSoftware/Proton/blob/1f089f5610a882c8bfcb4b58a3c86ee88bb4b9f1/README.md#arm64-builds) · [CorkDeck 构建流程](BUILDING.md)

- [ ] **先验证一款游戏（3–5 天）。** 选取在 CorkDeck 能稳定复现启动或关卡故障、且有 Proton 运行记录可对照的同一 Steam 版本。记录当前构建、运行设置、日志和卡点，区分 Wine、CPU 转译、图形转译或 Steam 客户端依赖，再决定值得移植的修复。排除《镜之边缘》这类已知受客户端签名检查阻断的首测候选。[已有实测](docs/COMPATIBILITY.md)

以下是验证后可选的交付范围。工期按一名熟悉 Wine 和 Switch 的全职开发者持续使用真机估算，各项独立估算，不累加。

- [ ] **逐游戏选择默认或实验配置（1–2 周）。** 组合已有的 FEX、Box64 和 DXVK 选项。按游戏保存选择，日志显示实际加载的组件，并验证能恢复默认设置。
- [ ] **移植一项 Proton 修复（1–4 周）。** 固定上游提交，在同一台 Switch、同一游戏版本和设置下做前后对照，验证能越过原先卡住的步骤。
- [ ] **移植一套基于 Proton Wine 的 Horizon 运行时（约 2–4 个月）。** 从干净仓库构建配套 NRO 和 Windows DLL，保留 Horizon 接口与 x86 转译，选定几款游戏验证进入实际关卡。底层接口差异可能延长工期。
- [ ] **让两套运行时并存并按游戏切换（约 4–8 个月起）。** 验证每次启动加载匹配的 NRO 和 DLL，明确每款游戏的 Wine 环境与存档如何跨版本使用，并在真机上验证切换、升级和回退。为两个版本持续运行回归测试。
