# 实验 04：屏幕显示

## 当前已确认

```text
connector=DSI-1
status=connected
mode=720x1280
framebuffer=/dev/fb0
framebuffer_driver=rockchipdrmfb
bits_per_pixel=32
```

说明内核已识别 MIPI DSI 显示链路。当前还没有检查触摸屏，也没有确定系统中适合使用 framebuffer、DRM/KMS、Qt 还是厂商示例。

## 后续最小步骤

1. 确认屏幕实物是否正常点亮、方向是否正确。
2. 保存当前 framebuffer/DRM 信息。
3. 使用厂商已提供的显示示例先显示静态色块或图片。
4. 显示一行静态文字。
5. 把 ADC 数值和音量 RMS 刷新到屏幕上。

第一版界面只需三行：

```text
ADC:  xxx mV
GPIO: HIGH/LOW
AUDIO: █████░░░
```

不在确认显示栈前引入大型 UI 框架。
