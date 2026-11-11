# ACZero Mods v59

《ACE COMBAT ZERO: THE BELKAN WAR》Steam 独立版 **2.0.0.1 x64** 的单人游戏模组。

## 使用方法

1. 从 Release 下载 `ACZeroMods-v59-Steam-Standalone.zip` 并完整解压到可写目录。
2. 安装 Windows x64 Python 3（含 Tkinter）。运行 `Start-Trainer.cmd`，或在目录中运行 `python trainer.py`。
3. 正常打开 AC0，进入飞行任务并暂停。在面板选择独立版游戏的 `acz.exe`，然后点击连接。
4. 连接后可在面板调整功能开关，或打开 `dist/ACZeroMods.ini` 编辑各弹种参数。XAAM 对应游戏中的 XMAA。

面板会核对选定的游戏 EXE、已验证的版本及运行时代码，再通过 Windows 加载器把普通 v59 DLL 加载到该游戏进程。同一进程只加载一次。发布包包含 Python 源码，供检查加载与校验流程。

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

- [v59 更新记录](CHANGELOG.md)
- [全机体解锁与购买清单](docs/aircraft-unlocks.md)
- 本项目采用 MIT 许可。随附的 MinHook 保留其 BSD 许可，见 `vendor/minhook/LICENSE.txt`（发布包中为 `MinHook-LICENSE.txt`）。
