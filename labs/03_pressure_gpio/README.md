# 两脚压阻传感器无外接电阻试验

本实验只验证“按下/松开”，不测量连续力度。程序先将 40P 物理第 29 脚
`GPIO3_B1` 请求为输入，再将物理第 31 脚 `GPIO3_B0` 请求为高电平输出。两脚压阻
传感器只接在第 29 与第 31 脚之间，不接电源和地。

GPIO 内部下拉阻值和传感器阻值都存在较大公差，因此该方法只适合临时演示。连续压力曲线
仍需分压电阻接 SARADC，或使用 ADS1115 等外置 ADC。

## 安全接线顺序

1. 断电并拔下传感器。
2. 构建、上传并启动程序；程序成功占用两个 GPIO 后会打印 `READY`。
3. 确认 UART2 没有占用第 29、31 脚。
4. 再将传感器接在物理第 29 与第 31 脚之间。
5. 按压时观察 `PRESSED`/`RELEASED`。

不要接 3.3 V、5 V、1.8 V 或 GND。退出时程序先释放高电平源，第 29、31 脚恢复为
当前固件默认的下拉输入；不运行测试时仍建议拔下传感器。

## 构建和运行

```powershell
wsl -e sh -lc "cd /mnt/d/WorkSpace/RV1126B && sh labs/03_pressure_gpio/build_wsl.sh"
adb push build/pressure-gpio/pressure-gpio /tmp/pressure-gpio
adb shell "chmod 0755 /tmp/pressure-gpio && /tmp/pressure-gpio"
```
