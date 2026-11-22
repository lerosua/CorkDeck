> **实验已暂停（2026-11-22）。** 本分支保存独立 renderer 的验证代码，不代表 Electron 游戏可运行。
> 最新实机结果：成功创建独立 SystemModule 并获得真实 PID，但只有 8 MiB 堆，
> 装不下约 86.13 MiB 的 Wine 映像及 BSS。子进程尚未进入 Wine，Mojo 往返和首帧尚未验证。
> 普通使用请保留 `codex/chinese-menu`；恢复开发前先解决 [TODO 内存卡点](../../../TODO.md)。

# Switch 独立 renderer 探针

此包用于已有 CorkDeck、Windows DLL 和 FEX 的 Atmosphère 主机。将 ZIP 解压到 SD 卡根目录，允许覆盖同名文件，保持压缩包里的目录结构。包里的 `exefs.nsp` 由 Atmosphère 按需读取，无需用 DBI 安装。它没有开机自启标志。

用原来的 CorkDeck 入口运行 `C:\ElectronProbe-10.1.5\ElectronProbe.exe`。总运行时间包括两份 Wine 的启动时间。成功时窗口会显示左紫、右绿的页面。即便黑屏或窗口消失，也可以读取这次的日志。

| 观察项目 | 实机证据 |
| --- | --- |
| Horizon 是否创建独立进程 | 浏览器端日志中的 `pmshellLaunchProgram` 给出系统返回码和 PID。`independent-pid verified` 表示子程序用 `svcGetProcessId` 自查得到相同 PID，并通过本次启动的随机握手。 |
| 子程序是否进入 renderer | `renderer bootstrap confirmed` 表示父进程核对了子程序 Wine TEB 中的 Windows PID 和 TID。`enter Electron renderer` 记录放行 guest 入口，随后应出现 JavaScript 的 `renderer-preload`。 |
| Electron 消息是否双向传递 | 同一个 `run` 的 `mojo-roundtrip` 必须是 `matched: true`。两端都看到传输字节只说明通道有流量。 |
| 页面是否画出首帧 | `capture-result` 的 `matched: true` 表示截取的页面左右两处像素符合预期。`frame-verified` 给出保存的 PNG。`probe-passed` 表示本次运行同时满足消息往返和像素校验。 |

浏览器原生日志位于 `/switch/wine/logs/ElectronProbe.log`，开启详细日志或会话记录时沿用 CorkDeck 的命名规则。独立子程序写入同目录的 `ElectronProbe-loader.log` 和 `ElectronProbe-worker.log`。JavaScript 在 EXE 旁边追加 `probe.log` 并保存 `frame-*.png`。按本次浏览器日志的 PID 匹配 loader 的记录。若启动服务直接报错，子程序可能没有本次日志。

探针仅接管指定目录下 ElectronProbe 的 renderer 启动请求。初始 Mojo 管道经本机回环接口转发，Chromium 的只读 field-trial 配置按启动快照传递。运行期间如需向远端复制其他 Wine 对象，浏览器会记录 `DuplicateHandle` 和返回码。子程序从系统内存池申请堆，申请大小和原始返回码记录在 loader 日志。实机能否提供足够的内存是这次验证的一部分。

## 分支归档校验（2026-11-22）

- ARM64 2026.10.12 镜像成功构建 `wine-nx-runtime-nro` 和 `electron-renderer-loader`。
- `sh horizon-wine/tests/run-host-checks.sh wine` 通过；线程夹具包含实验子进程字段。这些主机测试没有验证跨 Horizon 进程的 Mojo 往返。
- 重新生成 ExeFS，校验 ACI0 和 ACID 的 `application_type=0`，并通过探针 ZIP CRC、Electron 10.1.5 版本及官方分发包 SHA-256 校验。
- NRO SHA-256：`7f644a642d6bfe7ad9cc82b3432c0f3c7c87878c7cff2198db2478ae56bc00e1`。
- ExeFS SHA-256：`7bffc9a834db93b557e71849a01c3cfb6ddc32d55083131a73b7450e7b3cde9b`。
- 本轮没有再次上机；内存不足的结论未改变。
