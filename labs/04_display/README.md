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

显示和触摸链路都已实机确认：系统桌面由 Weston/Wayland 运行，应用层可直接使用 Qt5 Widgets；Goodix 触摸设备为 `/dev/input/event1`，坐标范围 `0～719 × 0～1279`，`ABS_MT_SLOT=0～4`，即最多 5 个同时触点。

自制 [RV1126B 触摸软件实验台](../../apps/rv1126b_lab/README.md) 已在真实 `720x1280` 竖屏完成全部页面渲染，并安装到开发板桌面。五点触摸页会显示每个触点的编号、坐标、彩色轨迹和前两点距离。

## 已完成步骤

1. 屏幕正常点亮，方向和 `720x1280` 触摸坐标一致。
2. 已保存 framebuffer、DRM、Goodix 输入设备和多点触摸能力。
3. Qt5 程序能从桌面全屏启动、返回主页并退出。
4. ADC 曲线、系统状态、音频波形和摄像头格式均能在屏幕显示。
5. 五点触摸页面已准备好，最终五指实操需用户在屏幕上完成。

早期计划中的三行界面：

```text
ADC:  xxx mV
GPIO: HIGH/LOW
AUDIO: █████░░░
```

已经扩展为统一实验台；仍只链接板上已有的 QtCore、QtGui 和 QtWidgets，曲线与触点由 `QPainter` 绘制，没有额外安装大型 UI 框架。
