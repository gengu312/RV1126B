# 实验 02：ADC

## 当前已跑通的部分

Linux 已识别 SAR ADC：

```text
/sys/bus/iio/devices/iio:device0
name=21f10000.saradc
in_voltage0_raw ... in_voltage7_raw
in_voltage_scale=0.219726562
```

这说明“读取 ADC 原始数据”的软件链路已经存在。首次检查读到了 8 个通道的数值，范围落在 `0～8191`。

厂家引脚表和核心板规格书进一步确认：按规格书横放方向，板子右下角的“可调电位器（ADC）”连接 `SARADC0_IN4`，对应当前系统的 `in_voltage4_raw`。在当前实物照片的竖放方向中，它位于屏幕右上侧，是按键下方带调节槽的蓝色小方块。因此第一个 ADC 实验不需要外接信号。

## 只读实验

在电脑 PowerShell 中执行：

```powershell
adb push .\labs\02_adc\read_adc.sh /tmp/rv1126b-read-adc.sh
adb shell sh /tmp/rv1126b-read-adc.sh
```

脚本输出每个通道的 `raw`、`scale` 和估算毫伏值。估算公式为：

```text
估算毫伏 = raw × scale
```

当前驱动给出的满量程推算值约为 1800 mV，与瑞芯微公开 Datasheet 的 SAR ADC 正常输入范围 0～1.8 V 一致。但在底板手册确认前，仍不能把“芯片输入范围”直接当成“扩展针脚允许输入范围”，因为底板可能有分压或保护电路。

## 旋钮连续观察实验

最简单的启动方式是在电脑 PowerShell 中运行：

```powershell
.\labs\02_adc\start_adc_visualizer.ps1
```

电脑上的 PowerShell 启动程序会自动把采样脚本发送到开发板。ADC 数据在开发板上读取，百分比、条形图、原始值和估算电压通过 ADB 实时显示在电脑终端：

```text
ADC  51.54% [#####################-------------------] raw=4221  voltage=927.466 mV
```

缓慢旋转蓝色电位器，条形图会随采样值实时伸缩；按 `Ctrl+C` 停止。

也可以手动执行下面两条命令：

先把脚本传到板子：

```powershell
adb push .\labs\02_adc\watch_adc.sh /tmp/rv1126b-watch-adc.sh
adb shell sh /tmp/rv1126b-watch-adc.sh
```

运行后缓慢转动可调电位器，观察百分比、条形图、`raw` 和 `voltage` 连续变化；按 `Ctrl+C` 停止。默认只读取通道 4，不涉及任何外部接线。

需要自动读取固定次数时，可在后面追加“间隔秒数”“采样次数”和“条形图宽度”，例如 `adb shell sh /tmp/rv1126b-watch-adc.sh 4 0.2 10 40`。

## ADC 控制 LED 闪烁速度

组合实验使用板载电位器控制工作/用户指示灯的闪烁速度：

```text
SARADC0_IN4 原始值增大
        -> 每次亮灭的等待时间缩短
        -> LED 闪烁加快
```

程序把 ADC 的 `0～8191` 线性映射为 `1.50～0.10` 秒的半周期。它只接管一个可编程用户灯；电源灯仍保持常亮。退出程序时会恢复运行前的 LED 触发器和亮度。

程序安装到开发板后，可进入开发板 Linux 手动运行：

```sh
/root/rv1126b-labs/adc_led_speed.sh
```

缓慢旋转蓝色电位器，观察 LED 闪烁速度变化；按 `Ctrl+C` 停止。需要只测试 3 次时：

```sh
/root/rv1126b-labs/adc_led_speed.sh 3
```

## 外接信号前必须完成

- [ ] 在原理图/排针图中确认 ADC 物理针脚和通道号。
- [ ] 确认该针脚最大输入电压和是否已有分压电路。
- [ ] 确认开发板 GND，并让信号源与开发板共地。
- [ ] 确认输入不含负电压或超过量程的尖峰。
- [ ] 不使用 USB 5V 直接测试。

## 后续验收

在安全接线确认后，对同一通道输入低、中、高三个已知电平：

1. 每档读取至少 50 次。
2. 原始值应随输入电压单调变化。
3. 数据不应大量固定在 0 或 8191。
4. 保存平均值、最小值、最大值和实际万用表读数。
