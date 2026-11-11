# ACZero Mods v59

《ACE COMBAT ZERO: THE BELKAN WAR》Steam 独立版 **2.0.0.1 x64** 的单人游戏模组。

## 使用方法

1. 从 Release 下载 `ACZeroMods-v59-Portable.zip` 并完整解压到可写目录。
2. 正常打开 AC0，进入飞行任务并暂停。
3. 运行 `ACZeroMods.exe`，点击“连接已启动的 AC0”。EXE 便携包无需安装 Python。
4. 连接后可在面板调整功能开关，或打开同目录的 `ACZeroMods.ini` 编辑各弹种参数。XAAM 对应游戏中的 XMAA。

面板会核对运行中的游戏 EXE、已验证的版本及运行时代码，再通过 Windows 加载器把普通 v59 DLL 加载到该游戏进程。同一进程只加载一次。EXE 面板源码见 `src/portable_launcher.cpp`，仓库也保留 Python 面板源码。

## 卡巴告警与验证状态

本启动器曾被卡巴报为 `VHO:Trojan-PSW.Win32.Convagent.gen`。代码检查未发现联网、读取浏览器密码或设置开机启动的逻辑。连接 AC0 时使用的 `WriteProcessMemory`、`CreateRemoteThread` 和 `LoadLibraryW` 可能触发启发式检测，具体检测原因尚未由卡巴确认，当前应视为疑似误报。

在用户自行添加本机排除项后，同一份 EXE 已通过离线文件校验、16 项开关的隐藏窗口布局检查、游戏版本检查，以及损坏游戏文件和 DLL 的拒绝测试。EXE 便携包的实际游戏连接仍待验证。排除项允许本机执行，不能作为厂商已确认误报的证明。可通过卡巴官方渠道提交样本复核。[卡巴处理说明](https://support.kaspersky.com/common/error/other/15332)

Release 附有 ZIP 和 EXE 的 SHA256，包内 `SHA256SUMS.txt` 可核对各文件。

## 功能与检查点

- 双 SP、独立余弹和冷却、五行武器 HUD。
- NEXT 目标预告、机头方向优先、远距目标框及目标名称。
- 可独立调整制导、弹速、锁距和航程。包含高 G、机炮辅助及弹药相关开关。
- 暂停菜单 `CHECKPOINT → SAVE / LOAD`。死亡回放后可从死亡菜单选择 `LOAD CHECKPOINT`。
- 手动与自动 CP 共用一个内存槽。每次成功保存替换前一点。重开任务、换任务或退出游戏后需要重新存点。
- `WAIT FOR WEAPONS TO CLEAR` 或 `WAIT FOR SPECIAL WEAPON TO CLEAR` 出现时，等武器活动结束再试。存读之间保持双 SP、无限弹药和快速装填开关一致。
- v59 已实测 M18S 第三轮死亡读点后恢复倒计时和 Pixy、击毁后进入过场。自动存点的支持范围随任务阶段而异。

公开配置为中性默认值。无限弹药、快速装填及其他增强默认关闭，按需要在面板开启。阶段自动保存默认开启。

## 从源码构建

Windows 上准备 Zig 和 PowerShell，在源码根目录运行 `./build.ps1 -Zig zig`。构建输出到 `dist/ACZeroMods-v59.dll`。启动器用到的游戏兼容性资料仅包含校验所需的版本、偏移和哈希。

原生面板运行 `./build-launcher.ps1 -Zig zig` 构建，输出为 `dist/ACZeroMods.exe`。该构建会绑定当前 `dist/ACZeroMods-v59.dll` 的 SHA256。发行包的原始兼容性资料保存在 `compat/portable_v59_profile.h`。

- [v59 更新记录](CHANGELOG.md)
- [全机体解锁与购买清单](docs/aircraft-unlocks.md)
- 本项目采用 MIT 许可。随附的 MinHook 保留其 BSD 许可，见 `vendor/minhook/LICENSE.txt`（发布包中为 `MinHook-LICENSE.txt`）。
