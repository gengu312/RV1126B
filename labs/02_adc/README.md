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
