# 独立压力监测应用

这是为 ATK-DLRV1126B 开发板制作的独立 Qt Widgets 触屏应用，不依赖“RV1126B 实验台”。安装后，开发板桌面会出现单独的“压力监测”图标，可执行文件名为 `pressuremonitor`。

## 功能

- 每 100 ms 只读一次 N1 对应的 ADC：`in_voltage1_raw`。
- 启动或点击“重新校准”后，使用前 20 个采样点完成约 2 秒的松开零点校准。
- 使用 5 点中值滤波和系数为 0.35 的 EMA 平滑。
- 显示 ADC 原始值、平滑值、电压、相对力度和松开/轻按/中等/重按状态。
- 保存最近 300 点，即约 30 秒的相对力度曲线。
- 提供默认关闭的“语音播报”开关。开启时先提示“语音播报已开启”，之后只播报轻按、中等压力或压力较大，不播报具体数值。
- 压力状态连续稳定 5 个采样点（约 0.5 秒）且与上次播报状态不同时才播报；两次播报至少间隔 2 秒，稳定松开后下一次按压可再次播报。
- 提供触屏“重新校准”和“退出”按钮，也支持系统返回键或 Esc 退出。

相对力度是根据零点到 ADC 满量程归一后的结果，只能用于比较按压力度，不是经过标定的牛顿值。

## 接线

当前已经在 ATK-DLRV1126B 上验证可用的接法如下。找针脚时应**优先认板上丝印，物理编号只用于复核**。

先把开发板按下面的方向摆放：屏幕在左、蓝色 40P 排针在右，电源和 USB 接口朝上，`MENU/RESET` 按键朝下。此时靠屏幕的左列是偶数脚，从上到下依次为 `40、38、36……2`；靠板边的右列是奇数脚，从上到下依次为 `39、37、35……1`。

| 部件 | 板上直接寻找的丝印 | 位置与编号复核 | 说明 |
| --- | --- | --- | --- |
| 压力传感器第 1 脚 | **`1.8V`** | 靠屏幕的左列、从顶部数第 5 排，即物理第 32 脚；右边同排丝印是 `U2_RX` | 为无源两脚 FSR 供电 |
| 压力传感器第 2 脚 | 公共测量节点，无单独丝印 | 与下一行的 `N1` 以及 10 kΩ 电阻一端接在一起 | 三者必须相连 |
| 公共测量节点 | **`N1`** | 靠屏幕的左列、从底部数第 4 排，即物理第 8 脚；右边同排丝印是 `N6` | 对应 `SARADC0_IN1_CDS`、`in_voltage1_raw` |
| 10 kΩ 电阻另一端 | **`GND`** | 靠屏幕的左列、从顶部数第 7 排，即物理第 28 脚；右边同排丝印是 `MISO` | 与传感器组成分压电路 |

只看接线附近的排针丝印，可以按下面三组确认：

```text
靠屏幕的左列（偶数）      靠板边的右列（奇数）

32  1.8V                 31  U2_RX       ← 传感器第 1 脚接这里
30  NC                   29  U2_TX
28  GND                  27  MISO        ← 10 kΩ 电阻末端接这里
       ……中间若干排……
 8  N1                    7  N6          ← 公共测量节点接这里
       ……下面还有 3 排……
 2  最底排                 1  最底排
```

```text
物理 32 脚 1.8 V ── 压力传感器 ──●── 物理 8 脚 N1（ADC）
                                      │
                                      └── 10 kΩ ── 物理 28 脚 GND
```

这个 `●` 是同一个公共节点：传感器第 2 脚、丝印 `N1` 的针脚和 10 kΩ 电阻的一端三者必须相连。不要把传感器只接在 `1.8V` 与 `GND` 之间，也不要接到同排旁边的 `U2_RX`、`MISO`、`N6`，或任何 `NC` 针脚。N1 的正常模拟输入范围为 `0～1.8 V`，不得接入 3.3 V 或 5 V。

进入应用或点击“重新校准”后，必须完全松开传感器约 2 秒。应用只读取 IIO 文件，不会导出或驱动 GPIO。

## 语音播报

语音播报默认关闭，点击曲线上方的“语音播报：关闭”按钮后开启，再次点击即可关闭并立即终止当前播报。应用使用板端 `/usr/bin/espeak -v zh -s 145` 非阻塞播报；前一条语音尚未结束时不会叠加播放。若板端没有可执行的 `espeak`，按钮会被禁用并显示“语音播报：不可用”，页面中会给出程序路径。

## 交叉编译

先确保 `build/board-sysroot/usr/lib` 中已经准备好开发板的 Qt 5.15.11 动态库；如果尚未准备，可先运行现有的 `apps/rv1126b_lab/prepare_sysroot.ps1`。然后在 WSL 中执行：

```sh
cd /mnt/d/WorkSpace/RV1126B
sh apps/pressure_monitor/build_wsl.sh
```

输出文件：

```text
build/pressure-monitor/pressuremonitor
```

## 离屏测试

测试时可用普通文件替代 ADC 和 scale。程序会固定使用 720×1280，并保存截图：

```sh
export QT_QPA_PLATFORM=offscreen
export PRESSUREMONITOR_ADC_PATH=/tmp/pressure-raw
export PRESSUREMONITOR_SCALE_PATH=/tmp/pressure-scale
export PRESSUREMONITOR_TTS_PROGRAM=/tmp/fake-espeak
export PRESSUREMONITOR_SCREENSHOT=/tmp/pressure-monitor.png
export PRESSUREMONITOR_AUTO_EXIT_MS=3200
./pressuremonitor
```

可用环境变量：

- `PRESSUREMONITOR_ADC_PATH`：覆盖 ADC 原始值文件路径。
- `PRESSUREMONITOR_SCALE_PATH`：覆盖 ADC scale 文件路径。
- `PRESSUREMONITOR_TTS_PROGRAM`：覆盖语音程序路径，默认 `/usr/bin/espeak`；可指向假的记录脚本进行无声测试。
- `PRESSUREMONITOR_SCREENSHOT`：保存离屏截图；设置后窗口固定为 720×1280。
- `PRESSUREMONITOR_SCREENSHOT_DELAY_MS`：截图延迟，默认 2500 ms。
- `PRESSUREMONITOR_AUTO_EXIT_MS`：自动退出延迟。

## 安装

开发板通过 ADB 连接后，在 PowerShell 中运行：

```powershell
.\apps\pressure_monitor\install.ps1
```

安装脚本会先在 `/tmp` 中检查动态库依赖，再原子替换 `/opt/ui/src/apps/pressuremonitor`，并以幂等方式把下面这一项放入 `apk1.cfg`，同时从其他页面清除重复项：

```text
appicons/sensor.png 压力监测 pressuremonitor
```

首次修改启动器配置时会保留 `.codex-backup` 备份。安装完成后重启开发板刷新桌面图标。
