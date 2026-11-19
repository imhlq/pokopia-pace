# 宝可梦生活加速 · Pokopia Pace

[English](README.md)

本模组可以调整移动、制作和采集倍率，缩短部分等待时间，延长料理增益，并可提前使用电磁飘浮。

**兼容环境** · Windows x64、宝可梦游戏版本 2.0.1（更新编号 655360）和 Modtopia 0.7.x。已在 Recompia 0.14.1 上测试。本模组不发起网络请求。联机时，所有玩家需使用相同的模组版本和设置。

## 安装与更新

1. 退出游戏。
2. 将 `builder_boost-1.6.4.pkmod` 放入 Recompia 游戏目录下的 `mods` 文件夹。更新时替换旧版 Builder Boost 文件，原有设置可继续使用。
3. 通过 Recompia 启动器启动游戏。如有提示，请允许加载模组代码并等待启动器完成重新构建。
4. 打开 **F1 → Mods → Pokopia Pace · 生活加速** 调整设置。如果同时安装了 **Run Speed** 或 **Gathering Boost**，请关闭这两个旧模组，以免效果重叠。

## 设置对照

Modtopia 的模组清单为每项设置提供一个固定标签。菜单显示英文设置名，可按下表查找。

| 菜单中的英文名称 | 中文说明 | 默认值 |
| --- | --- | --- |
| Stick-click sprint speed multiplier | 按下摇杆后触发的冲刺速度。范围 1–3 倍。 | 1.5 倍 |
| Full-stick movement speed multiplier | 摇杆推到底时的普通移动速度。范围 1–3 倍。 | 1.5 倍 |
| Common materials and building blocks crafted multiplier | 部分常见材料与建筑方块的单次制作产量。范围 1–5 倍。 | 3 倍 |
| Gathered materials, ores and crops multiplier | 部分采集掉落、矿石与农作物收获数量。范围 1–5 倍。 | 3 倍 |
| Gameplay wait duration (0.5 = half, 1 = normal) | 施工、加工和宝可梦出现相关的等待时间。范围 0.1–1 倍。 | 0.5 倍 |
| Cooked-food power-up duration multiplier | 新吃下的料理所提供的增益持续时间。范围 1–5 倍。 | 2 倍 |
| Unlock Magnet Rise early | 提前使用电磁飘浮。开启后可直接从招式轮盘选择。 | 开启 |
| Magnet Rise PP cost (0.5 = half, 1 = normal) | 电磁飘浮持续期间及执行动作时的 PP 消耗。范围 10%–100%。 | 50% |

默认情况下，1 个铁锭和 1 个石头可制作 30 个铁轨。配方消耗、任务物品、配方解锁和稀有制作产物保留原本的规则。默认等待倍率下，15 分钟的施工缩短为 7 分 30 秒，栖息地检查间隔从 180 秒缩短为 90 秒，宝可梦出现的条件依然适用。

电磁飘浮优先占用招式轮盘的第一个空位。轮盘已满时会暂时占用最后一格。关闭提前解锁后，轮盘会恢复已保存的布局，并按游戏原本的进度解锁电磁飘浮。PP 消耗可单独调整。

## 从源码构建

在 Windows x64 上安装 Python 3.11 或更新版本及 Zig，然后在源码目录执行以下命令。

```powershell
python build.py --zig C:\path\to\zig.exe
```

脚本会编译并测试 DLL，在 `build/` 生成安装包和 SHA-256 校验值。模组和附带的 Modtopia SDK 均采用 MIT 许可证。
