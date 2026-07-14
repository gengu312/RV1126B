# ADC 调速灯触摸应用

这是安装在 ATK-DLRV1126B 开发板桌面上的 Qt5 触摸应用，使用板载资源：

- `SARADC0_IN4`：读取蓝色电位器；
- `/sys/class/leds/work`：控制工作/用户指示灯。

界面提供熄灭、常亮、慢闪、快闪、触摸滑杆调速和 ADC 调速模式。触摸滑杆向左变慢、向右变快；拖动后立即切换为触摸调速。ADC 调速把 `0～8191` 映射为 `1500～100 ms` 的半周期。退出应用时恢复进入应用之前的 LED 触发器和亮度。

## 构建

构建脚本在 WSL Ubuntu 中使用 ARM64 交叉编译器，并链接从实机取得的 Qt 5.15.11 库：

```sh
./apps/adc_led_touch/build_wsl.sh
```

构建输出位于忽略提交的目录：

```text
build/adc-led-touch/adcled
```

开发板安装路径：

```text
/opt/ui/src/apps/adcled
```

桌面启动项使用系统已有的 LED 图标，不覆盖正点原子的原 LED 应用。

桌面启动配置行保存在 `desktop-entry.txt`。安装时应先备份开发板的
`/opt/ui/src/ATK-DLRV1126B/apk2.cfg`，再以幂等方式追加该配置行。
