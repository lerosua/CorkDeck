# CorkDeck 待办

## Proton 移植与逐游戏运行时选择

Valve 的 `proton_11.0` 支持 ARM64 构建，产物面向 Linux。生成能在 Switch 的 Horizon OS 上运行的 NRO，还要适配 CorkDeck 的线程、内存、文件访问和 x86 转译接口。目前完成了源码评估，真机移植可行性待验证。[Valve ARM64 构建说明](https://github.com/ValveSoftware/Proton/blob/1f089f5610a882c8bfcb4b58a3c86ee88bb4b9f1/README.md#arm64-builds) · [CorkDeck 构建流程](BUILDING.md)

- [ ] **先验证一款游戏（3–5 天）。** 选取在 CorkDeck 能稳定复现启动或关卡故障、且有 Proton 运行记录可对照的同一 Steam 版本。记录当前构建、运行设置、日志和卡点，区分 Wine、CPU 转译、图形转译或 Steam 客户端依赖，再决定值得移植的修复。排除《镜之边缘》这类已知受客户端签名检查阻断的首测候选。[已有实测](docs/COMPATIBILITY.md)

以下是验证后可选的交付范围。工期按一名熟悉 Wine 和 Switch 的全职开发者持续使用真机估算，各项独立估算，不累加。

- [ ] **逐游戏选择默认或实验配置（1–2 周）。** 组合已有的 FEX、Box64 和 DXVK 选项。按游戏保存选择，日志显示实际加载的组件，并验证能恢复默认设置。
- [ ] **移植一项 Proton 修复（1–4 周）。** 固定上游提交，在同一台 Switch、同一游戏版本和设置下做前后对照，验证能越过原先卡住的步骤。
- [ ] **移植一套基于 Proton Wine 的 Horizon 运行时（约 2–4 个月）。** 从干净仓库构建配套 NRO 和 Windows DLL，保留 Horizon 接口与 x86 转译，选定几款游戏验证进入实际关卡。底层接口差异可能延长工期。
- [ ] **让两套运行时并存并按游戏切换（约 4–8 个月起）。** 验证每次启动加载匹配的 NRO 和 DLL，明确每款游戏的 Wine 环境与存档如何跨版本使用，并在真机上验证切换、升级和回退。为两个版本持续运行回归测试。
