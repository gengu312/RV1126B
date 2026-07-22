"""Pure helpers and ADB operations for the pressure audio manager."""

from __future__ import annotations

import configparser
import io
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import uuid
import wave
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Sequence


REMOTE_ROOT = "/userdata/pressure_monitor"
REMOTE_AUDIO_DIR = f"{REMOTE_ROOT}/audio"
REMOTE_CONFIG = f"{REMOTE_ROOT}/config.ini"
SLOTS = ("light", "medium", "heavy")
SLOT_LABELS = {"light": "轻按", "medium": "中按", "heavy": "重按"}
SAFE_REMOTE_NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
PREVIEW_AUDIO_SUFFIXES = frozenset((".wav", ".mp3", ".flac", ".m4a", ".aac", ".ogg"))


class ManagerError(RuntimeError):
    """An error that can be shown directly in the graphical interface."""


@dataclass(frozen=True)
class AdbDevice:
    serial: str
    state: str
    details: str = ""

    @property
    def display_name(self) -> str:
        model = ""
        for part in self.details.split():
            if part.startswith("model:"):
                model = part.partition(":")[2].replace("_", " ")
                break
        suffix = f" · {model}" if model else ""
        return f"{self.serial} · {self.state}{suffix}"


@dataclass(frozen=True)
class AudioInfo:
    path: Path
    size: int
    is_wav: bool
    channels: Optional[int] = None
    sample_rate: Optional[int] = None
    sample_width_bits: Optional[int] = None
    duration_seconds: Optional[float] = None
    compression: Optional[str] = None

    @property
    def board_compatible(self) -> bool:
        return bool(
            self.is_wav
            and self.compression == "NONE"
            and self.channels in (1, 2)
            and self.sample_rate is not None
            and 8_000 <= self.sample_rate <= 96_000
            and self.sample_width_bits == 16
        )


@dataclass(frozen=True)
class PreparedAudio:
    path: Path
    converted: bool


@dataclass(frozen=True)
class ManagerConfig:
    enabled: bool = False
    mode: str = "tts"
    fallback_tts: bool = True
    volume_percent: int = 100
    light_threshold: int = 3
    medium_threshold: int = 25
    heavy_threshold: int = 60
    cooldown_ms: int = 2000


def format_bytes(size: int) -> str:
    value = float(max(0, size))
    units = ("B", "KiB", "MiB", "GiB", "TiB")
    for unit in units:
        if value < 1024 or unit == units[-1]:
            return f"{value:.0f} {unit}" if unit == "B" else f"{value:.1f} {unit}"
        value /= 1024
    return f"{value:.1f} TiB"


def parse_adb_devices(output: str) -> list[AdbDevice]:
    devices: list[AdbDevice] = []
    for raw_line in output.splitlines():
        line = raw_line.strip()
        if not line or line.startswith("List of devices attached") or line.startswith("*"):
            continue
        fields = line.split(maxsplit=2)
        if len(fields) < 2:
            continue
        devices.append(AdbDevice(fields[0], fields[1], fields[2] if len(fields) > 2 else ""))
    return devices


def inspect_audio(path: os.PathLike[str] | str) -> AudioInfo:
    source = Path(path)
    if not source.is_file():
        raise ManagerError(f"找不到音频文件：{source}")
    size = source.stat().st_size
    try:
        with wave.open(str(source), "rb") as wav:
            frames = wav.getnframes()
            sample_rate = wav.getframerate()
            return AudioInfo(
                path=source,
                size=size,
                is_wav=True,
                channels=wav.getnchannels(),
                sample_rate=sample_rate,
                sample_width_bits=wav.getsampwidth() * 8,
                duration_seconds=(frames / sample_rate if sample_rate else 0.0),
                compression=wav.getcomptype(),
            )
    except (wave.Error, EOFError):
        return AudioInfo(path=source, size=size, is_wav=False)


def describe_audio(info: AudioInfo, ffmpeg_available: bool) -> str:
    if info.is_wav:
        seconds = info.duration_seconds or 0.0
        details = (
            f"{format_bytes(info.size)} · {seconds:.1f} 秒 · "
            f"{info.sample_rate} Hz · {info.sample_width_bits} bit · {info.channels} 声道"
        )
        if info.board_compatible:
            return f"{details} · 可直接上传"
        action = "将自动转为 48 kHz/16 bit WAV" if ffmpeg_available else "需要 FFmpeg 转码"
        return f"{details} · {action}"
    action = "将自动转为 48 kHz/16 bit WAV" if ffmpeg_available else "需要 FFmpeg 转码"
    return f"{format_bytes(info.size)} · {info.path.suffix.lower() or '未知格式'} · {action}"


def find_adb() -> Optional[str]:
    candidates: list[Optional[str]] = [os.environ.get("ADB"), shutil.which("adb")]
    for env_name in ("ANDROID_SDK_ROOT", "ANDROID_HOME"):
        root = os.environ.get(env_name)
        if root:
            candidates.append(str(Path(root) / "platform-tools" / "adb.exe"))
    local_app_data = os.environ.get("LOCALAPPDATA")
    if local_app_data:
        candidates.append(str(Path(local_app_data) / "Android" / "Sdk" / "platform-tools" / "adb.exe"))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return str(Path(candidate))
    return None


def find_ffmpeg() -> Optional[str]:
    configured = os.environ.get("FFMPEG")
    if configured and Path(configured).is_file():
        return str(Path(configured))
    return shutil.which("ffmpeg")


def validate_thresholds(light: int, medium: int, heavy: int, cooldown_ms: int, volume: int) -> None:
    if not 0 < light < medium < heavy <= 100:
        raise ManagerError("压力阈值必须满足 0 < 轻按 < 中按 < 重按 ≤ 100。")
    if not 0 <= volume <= 100:
        raise ManagerError("音量必须在 0～100 之间。")
    if not 0 <= cooldown_ms <= 60_000:
        raise ManagerError("播报间隔必须在 0～60000 毫秒之间。")


def build_config(
    *,
    enabled: bool,
    mode: str,
    fallback_tts: bool,
    volume_percent: int,
    light_threshold: int,
    medium_threshold: int,
    heavy_threshold: int,
    cooldown_ms: int,
) -> str:
    if mode not in ("tts", "clips"):
        raise ManagerError("声音模式只能是 tts 或 clips。")
    validate_thresholds(
        light_threshold, medium_threshold, heavy_threshold, cooldown_ms, volume_percent
    )
    parser = configparser.ConfigParser(interpolation=None)
    parser.optionxform = str
    parser["audio"] = {
        "enabled": "true" if enabled else "false",
        "mode": mode,
        "fallback_tts": "true" if fallback_tts else "false",
        "light": f"{REMOTE_AUDIO_DIR}/light.wav",
        "medium": f"{REMOTE_AUDIO_DIR}/medium.wav",
        "heavy": f"{REMOTE_AUDIO_DIR}/heavy.wav",
        "volume_percent": str(volume_percent),
    }
    parser["pressure"] = {
        "light_threshold": str(light_threshold),
        "medium_threshold": str(medium_threshold),
        "heavy_threshold": str(heavy_threshold),
        "cooldown_ms": str(cooldown_ms),
    }
    stream = io.StringIO(newline="\n")
    parser.write(stream, space_around_delimiters=False)
    return stream.getvalue()


def parse_config(content: str) -> ManagerConfig:
    if not content.strip():
        return ManagerConfig()

    parser = configparser.ConfigParser(interpolation=None)
    try:
        parser.read_string(content)
        config = ManagerConfig(
            enabled=parser.getboolean("audio", "enabled", fallback=False),
            mode=parser.get("audio", "mode", fallback="tts").strip().lower(),
            fallback_tts=parser.getboolean("audio", "fallback_tts", fallback=True),
            volume_percent=parser.getint("audio", "volume_percent", fallback=100),
            light_threshold=parser.getint("pressure", "light_threshold", fallback=3),
            medium_threshold=parser.getint("pressure", "medium_threshold", fallback=25),
            heavy_threshold=parser.getint("pressure", "heavy_threshold", fallback=60),
            cooldown_ms=parser.getint("pressure", "cooldown_ms", fallback=2000),
        )
    except (configparser.Error, ValueError) as exc:
        raise ManagerError(f"板端 config.ini 无法解析：{exc}") from exc

    if config.mode not in ("tts", "clips"):
        raise ManagerError("板端声音模式只能是 tts 或 clips。")
    validate_thresholds(
        config.light_threshold,
        config.medium_threshold,
        config.heavy_threshold,
        config.cooldown_ms,
        config.volume_percent,
    )
    return config


def is_safe_remote_filename(name: str) -> bool:
    return bool(SAFE_REMOTE_NAME.fullmatch(name)) and name not in (".", "..")


def parse_remote_audio_listing(output: str) -> list[tuple[str, int]]:
    result: list[tuple[str, int]] = []
    for line in output.splitlines():
        name, separator, raw_size = line.partition("\t")
        if not separator or not is_safe_remote_filename(name):
            continue
        try:
            size = int(raw_size.strip())
        except ValueError:
            continue
        result.append((name, max(0, size)))
    return sorted(result, key=lambda item: item[0].lower())


def _windows_creation_flags() -> int:
    return getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0


def run_command(
    args: Sequence[str], *, timeout: int = 60, check: bool = True
) -> subprocess.CompletedProcess[str]:
    try:
        result = subprocess.run(
            list(args),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            creationflags=_windows_creation_flags(),
            check=False,
        )
    except FileNotFoundError as exc:
        raise ManagerError(f"找不到命令：{args[0]}") from exc
    except subprocess.TimeoutExpired as exc:
        raise ManagerError(f"命令执行超时（{timeout} 秒）。") from exc
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise ManagerError(detail or f"命令执行失败，退出码 {result.returncode}。")
    return result


def adb_command(adb: str, serial: Optional[str], *args: str) -> list[str]:
    command = [adb]
    if serial:
        command.extend(("-s", serial))
    command.extend(args)
    return command


def run_adb(
    adb: str,
    serial: Optional[str],
    *args: str,
    timeout: int = 60,
    check: bool = True,
) -> subprocess.CompletedProcess[str]:
    return run_command(adb_command(adb, serial, *args), timeout=timeout, check=check)


def run_remote_script(
    adb: str, serial: str, script: str, *, timeout: int = 60
) -> subprocess.CompletedProcess[str]:
    # adb joins arguments after "shell" on the device. Quoting the complete script
    # makes it one argument to the board's `sh -c`, without invoking a Windows shell.
    return run_adb(adb, serial, "shell", "sh", "-c", shlex.quote(script), timeout=timeout)


def list_devices(adb: str) -> list[AdbDevice]:
    result = run_adb(adb, None, "devices", "-l", timeout=15)
    return parse_adb_devices(result.stdout)


def ensure_ready_device(device: AdbDevice) -> str:
    if device.state != "device":
        if device.state == "unauthorized":
            raise ManagerError("设备未授权：请在开发板屏幕上允许 USB 调试。")
        raise ManagerError(f"设备当前状态为 {device.state}，不能操作。")
    return device.serial


def prepare_audio(source: os.PathLike[str] | str, ffmpeg: Optional[str]) -> PreparedAudio:
    info = inspect_audio(source)
    if info.board_compatible:
        return PreparedAudio(info.path, converted=False)
    if not ffmpeg:
        raise ManagerError(
            "该文件不是兼容的 PCM 16-bit WAV。请安装 FFmpeg，或先转换为 "
            "48 kHz、16 bit、单/双声道 WAV。"
        )
    handle, output_name = tempfile.mkstemp(prefix="rv1126b-audio-", suffix=".wav")
    os.close(handle)
    output = Path(output_name)
    try:
        run_command(
            (
                ffmpeg,
                "-hide_banner",
                "-loglevel",
                "error",
                "-y",
                "-i",
                str(info.path),
                "-vn",
                "-acodec",
                "pcm_s16le",
                "-ar",
                "48000",
                "-ac",
                "2",
                str(output),
            ),
            timeout=300,
        )
        converted_info = inspect_audio(output)
        if not converted_info.board_compatible or converted_info.size == 0:
            raise ManagerError("FFmpeg 已结束，但没有生成兼容的 WAV 文件。")
        return PreparedAudio(output, converted=True)
    except Exception:
        output.unlink(missing_ok=True)
        raise


def upload_audio(adb: str, serial: str, source: Path, slot: str) -> str:
    if slot not in SLOTS:
        raise ManagerError(f"未知声音槽位：{slot}")
    run_remote_script(adb, serial, f"mkdir -p {shlex.quote(REMOTE_AUDIO_DIR)}")
    token = uuid.uuid4().hex[:12]
    remote_temp = f"{REMOTE_AUDIO_DIR}/.{slot}.wav.upload-{token}"
    destination = f"{REMOTE_AUDIO_DIR}/{slot}.wav"
    try:
        run_adb(adb, serial, "push", str(source), remote_temp, timeout=300)
        run_remote_script(
            adb,
            serial,
            f"chmod 0644 {shlex.quote(remote_temp)} && "
            f"mv -f {shlex.quote(remote_temp)} {shlex.quote(destination)} && sync",
        )
    except Exception:
        try:
            run_remote_script(adb, serial, f"rm -f {shlex.quote(remote_temp)}", timeout=15)
        except ManagerError:
            pass
        raise
    return destination


def write_remote_config(adb: str, serial: str, content: str) -> None:
    file_handle, local_name = tempfile.mkstemp(prefix="rv1126b-config-", suffix=".ini")
    os.close(file_handle)
    local_path = Path(local_name)
    remote_temp = f"{REMOTE_ROOT}/.config.ini.upload-{uuid.uuid4().hex[:12]}"
    try:
        local_path.write_text(content, encoding="utf-8", newline="\n")
        run_remote_script(adb, serial, f"mkdir -p {shlex.quote(REMOTE_AUDIO_DIR)}")
        run_adb(adb, serial, "push", str(local_path), remote_temp, timeout=60)
        run_remote_script(
            adb,
            serial,
            f"chmod 0644 {shlex.quote(remote_temp)} && "
            f"mv -f {shlex.quote(remote_temp)} {shlex.quote(REMOTE_CONFIG)} && sync",
        )
    except Exception:
        try:
            run_remote_script(adb, serial, f"rm -f {shlex.quote(remote_temp)}", timeout=15)
        except ManagerError:
            pass
        raise
    finally:
        local_path.unlink(missing_ok=True)


def read_remote_config(adb: str, serial: str) -> ManagerConfig:
    script = (
        f"if [ -f {shlex.quote(REMOTE_CONFIG)} ]; then "
        f"cat {shlex.quote(REMOTE_CONFIG)}; fi"
    )
    return parse_config(run_remote_script(adb, serial, script, timeout=15).stdout)


def list_remote_audio(adb: str, serial: str) -> list[tuple[str, int]]:
    run_remote_script(adb, serial, f"mkdir -p {shlex.quote(REMOTE_AUDIO_DIR)}")
    script = (
        f"for f in {shlex.quote(REMOTE_AUDIO_DIR)}/*; do "
        "[ -f \"$f\" ] || continue; "
        "b=${f##*/}; "
        "case \"$b\" in *[!A-Za-z0-9._-]*) continue;; esac; "
        "s=$(wc -c < \"$f\"); printf '%s\\t%s\\n' \"$b\" \"$s\"; "
        "done"
    )
    return parse_remote_audio_listing(run_remote_script(adb, serial, script).stdout)


def delete_remote_audio(adb: str, serial: str, name: str) -> None:
    if not is_safe_remote_filename(name):
        raise ManagerError("拒绝删除不安全的文件名。")
    path = f"{REMOTE_AUDIO_DIR}/{name}"
    run_remote_script(adb, serial, f"rm -f {shlex.quote(path)} && sync")


def test_remote_audio(adb: str, serial: str, name: str) -> None:
    if not is_safe_remote_filename(name):
        raise ManagerError("拒绝播放不安全的文件名。")
    path = f"{REMOTE_AUDIO_DIR}/{name}"
    script = (
        f"test -f {shlex.quote(path)} || exit 2; "
        "if command -v aplay >/dev/null 2>&1; then player=aplay; "
        "elif command -v ffplay >/dev/null 2>&1; then player=ffplay; "
        "else echo 'Neither aplay nor ffplay is installed.' >&2; exit 127; fi; "
        "pidfile=/tmp/pressure-audio-manager.pid; "
        "if [ -r \"$pidfile\" ]; then "
        "oldpid=$(cat \"$pidfile\" 2>/dev/null); "
        "oldpath=$(sed -n '2p' \"$pidfile\" 2>/dev/null); "
        f"case \"$oldpath\" in {shlex.quote(REMOTE_AUDIO_DIR)}/*) :;; *) oldpath=;; esac; "
        "case \"$oldpid\" in *[!0-9]*|'') :;; *) "
        "if [ -n \"$oldpath\" ] && [ -r \"/proc/$oldpid/cmdline\" ] "
        "&& grep -F \"$oldpath\" \"/proc/$oldpid/cmdline\" >/dev/null 2>&1; then "
        "kill \"$oldpid\" 2>/dev/null || true; fi;; esac; "
        "fi; "
        "rm -f \"$pidfile\"; "
        f"if [ \"$player\" = aplay ]; then aplay -q {shlex.quote(path)} "
        ">/tmp/pressure-audio-test.log 2>&1 </dev/null & "
        f"else ffplay -nodisp -autoexit -loglevel quiet -i {shlex.quote(path)} "
        ">/tmp/pressure-audio-test.log 2>&1 </dev/null & fi; "
        "newpid=$!; printf '%s\n%s\n' \"$newpid\" "
        f"{shlex.quote(path)} > \"$pidfile\""
    )
    run_remote_script(adb, serial, script, timeout=15)


def query_storage(adb: str, serial: str) -> str:
    result = run_adb(adb, serial, "shell", "df", "-h", "/userdata", timeout=15)
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    return "\n".join(lines[-2:]) if lines else "未获得 /userdata 存储信息"


def preview_local_audio(path: os.PathLike[str] | str) -> None:
    source = Path(path)
    if not source.is_file():
        raise ManagerError(f"找不到音频文件：{source}")
    if source.suffix.lower() not in PREVIEW_AUDIO_SUFFIXES:
        raise ManagerError("本机试听只允许 WAV、MP3、FLAC、M4A、AAC 或 OGG 音频文件。")
    try:
        if os.name == "nt":
            os.startfile(str(source))  # type: ignore[attr-defined]
        else:
            subprocess.Popen(("xdg-open", str(source)), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    except OSError as exc:
        raise ManagerError(f"无法调用系统播放器：{exc}") from exc


def cleanup_prepared(prepared: PreparedAudio) -> None:
    if prepared.converted:
        prepared.path.unlink(missing_ok=True)
