# RV1126B 压力声音管理器（Windows）

这是配套 `pressure_monitor` 触屏应用的电脑端管理工具。它使用 Python 标准库 `tkinter` 和 `adb`，不需要安装额外 Python 包。

## 能做什么

- 在多个 ADB 设备之间选择目标开发板；
- 查看开发板 `/userdata` 存储情况；
- 为轻按、中按、重按分别选择电脑中的音频；
- 调用电脑默认播放器进行本机试听；
- 把素材上传到 `/userdata/pressure_monitor/audio`；
- 在开发板扬声器上测试播放；
- 查看和删除已上传素材；
- 设置声音模式、TTS 回退、音量、三级阈值和播放间隔；
- 读取已有板端配置，修改后再安全写回；
- 单独控制压力声音反馈是否启用；
- 原子写入 `/userdata/pressure_monitor/config.ini`，避免传输中断留下半个配置文件。

音频最终保存在开发板上，不需要电脑一直连接。工具通过参数列表调用 `adb`，不会把本地文件名拼入 Windows Shell 命令；板端文件名也限制为安全字符。

## 运行条件

1. Windows 安装 Python 3，并包含 Tk 支持。Python 官方 Windows 安装通常已包含。
2. `adb.exe` 位于 `PATH`，或者设置环境变量 `ADB` 为其完整路径。
3. 开发板已开启 USB 调试，并在 `adb devices -l` 中显示为 `device`。
4. 可选：把 `ffmpeg.exe` 加入 `PATH`，或设置环境变量 `FFMPEG` 为完整路径。

没有 FFmpeg 时，工具仍然可以上传以下 WAV：

- PCM（未压缩）；
- 16 bit；
- 单声道或双声道；
- 8～96 kHz。

存在 FFmpeg 时，其他 WAV 以及 MP3、FLAC、M4A、AAC、OGG 会在电脑临时转换为 `48 kHz / 16 bit / 双声道 WAV` 后上传。临时文件在上传结束后删除，电脑原文件不会被修改。

## 启动

在仓库根目录打开 PowerShell：

```powershell
.\apps\pressure_audio_manager\run.ps1
```

也可直接运行：

```powershell
python .\apps\pressure_audio_manager\pressure_audio_manager.py
```

## 推荐操作顺序

1. 连接开发板，点击“刷新设备”，选择状态为 `device` 的序列号。
2. 在“声音素材”页分别选择文件并点击“上传”。
3. 点击每个槽位的“试听”，确认板端实际出声。
4. 在“触发配置”页读取已有设置，选择 `clips`，按需要调整参数并保存。
5. 压力应用运行时会自动重新读取原子替换后的配置；如未运行，下次启动时生效。

固定路径如下：

```text
/userdata/pressure_monitor/
├── config.ini
└── audio/
    ├── light.wav
    ├── medium.wav
    └── heavy.wav
```

配置协议：

```ini
[audio]
enabled=false
mode=tts
fallback_tts=true
light=/userdata/pressure_monitor/audio/light.wav
medium=/userdata/pressure_monitor/audio/medium.wav
heavy=/userdata/pressure_monitor/audio/heavy.wav
volume_percent=100

[pressure]
light_threshold=3
medium_threshold=25
heavy_threshold=60
cooldown_ms=2000
```

`enabled=false` 表示应用启动后默认不发声；在管理器中勾选“启用压力声音反馈”后会写为 `true`。默认 `mode=tts` 与板端初始配置一致；上传素材后可改为 `clips` 使用真人音频片段。`fallback_tts=true` 表示片段不存在或启动播放失败时退回合成语音。

## 测试

测试不需要连接开发板：

```powershell
python -m unittest discover .\apps\pressure_audio_manager\tests -v
```

测试覆盖 ADB 设备解析、配置生成、阈值校验、WAV 兼容性识别和远端文件名过滤。

## 注意事项

- 上传时不要拔掉 USB；即使中断，正式文件也不会被半成品覆盖。
- 删除素材不会删除 `config.ini`。启用 TTS 回退时，对应压力等级会继续使用合成语音。
- “音量”在板端使用 `ffplay` 时作为软件音量生效；若应用回退到 `aplay`，则由 ALSA/系统混音器决定，管理器不会修改整机混音器。
- 转码或上传进行中时，窗口会阻止退出，以便本机临时文件和板端上传临时文件完成清理。
- 首次版本使用 USB ADB。后续即使使用无线 ADB，设备也会以独立序列号出现在同一个选择框中。
