from __future__ import annotations

import sys
import tempfile
import unittest
import wave
from pathlib import Path
from unittest.mock import patch


APP_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(APP_DIR))

from core import (  # noqa: E402
    ManagerError,
    build_config,
    inspect_audio,
    is_safe_remote_filename,
    parse_adb_devices,
    parse_config,
    parse_remote_audio_listing,
    preview_local_audio,
    test_remote_audio,
    validate_thresholds,
)


class DeviceParsingTests(unittest.TestCase):
    def test_parses_multiple_states_and_model(self) -> None:
        output = """List of devices attached
ABC123 device product:rv1126 model:ATK_RV1126B transport_id:1
WIFI42 unauthorized transport_id:2

"""
        devices = parse_adb_devices(output)
        self.assertEqual([device.serial for device in devices], ["ABC123", "WIFI42"])
        self.assertEqual(devices[0].state, "device")
        self.assertIn("ATK RV1126B", devices[0].display_name)
        self.assertEqual(devices[1].state, "unauthorized")


class ConfigurationTests(unittest.TestCase):
    def test_builds_shared_board_contract(self) -> None:
        content = build_config(
            enabled=False,
            mode="clips",
            fallback_tts=True,
            volume_percent=80,
            light_threshold=3,
            medium_threshold=25,
            heavy_threshold=60,
            cooldown_ms=2000,
        )
        self.assertIn("[audio]", content)
        self.assertIn("enabled=false", content)
        self.assertIn("mode=clips", content)
        self.assertIn("light=/userdata/pressure_monitor/audio/light.wav", content)
        self.assertIn("fallback_tts=true", content)
        self.assertIn("[pressure]", content)
        self.assertIn("heavy_threshold=60", content)
        self.assertNotIn("\r\n", content)

    def test_rejects_out_of_order_thresholds(self) -> None:
        with self.assertRaises(ManagerError):
            validate_thresholds(3, 60, 25, 2000, 80)

    def test_parses_existing_board_config(self) -> None:
        config = parse_config(
            """[audio]
enabled=true
mode=tts
fallback_tts=false
volume_percent=65
[pressure]
light_threshold=5
medium_threshold=35
heavy_threshold=75
cooldown_ms=1500
"""
        )
        self.assertTrue(config.enabled)
        self.assertEqual(config.mode, "tts")
        self.assertFalse(config.fallback_tts)
        self.assertEqual(config.volume_percent, 65)
        self.assertEqual(config.cooldown_ms, 1500)

    def test_empty_config_uses_defaults(self) -> None:
        config = parse_config("")
        self.assertFalse(config.enabled)
        self.assertEqual(config.mode, "tts")
        self.assertEqual(config.volume_percent, 100)
        self.assertEqual(config.heavy_threshold, 60)

    def test_rejects_zero_light_threshold_like_board_app(self) -> None:
        with self.assertRaises(ManagerError):
            validate_thresholds(0, 25, 60, 2000, 100)


class AudioTests(unittest.TestCase):
    def test_recognizes_compatible_pcm_wav(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.wav"
            with wave.open(str(path), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(48_000)
                wav.writeframes(b"\x00\x00" * 480)
            info = inspect_audio(path)
        self.assertTrue(info.board_compatible)
        self.assertEqual(info.sample_rate, 48_000)
        self.assertEqual(info.sample_width_bits, 16)

    def test_non_wav_is_not_directly_compatible(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "sample.mp3"
            path.write_bytes(b"not really mp3")
            info = inspect_audio(path)
        self.assertFalse(info.is_wav)
        self.assertFalse(info.board_compatible)

    def test_local_preview_rejects_executable_suffix(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "not-audio.exe"
            path.write_bytes(b"MZ")
            with self.assertRaises(ManagerError):
                preview_local_audio(path)

    @patch("core.run_remote_script")
    def test_board_preview_checks_player_and_owned_pid(self, run_remote_script_mock) -> None:
        test_remote_audio("adb", "SERIAL", "light.wav")
        script = run_remote_script_mock.call_args.args[2]
        self.assertIn("command -v aplay", script)
        self.assertIn("/proc/$oldpid/cmdline", script)
        self.assertIn("/userdata/pressure_monitor/audio/light.wav", script)
        self.assertNotIn("pkill", script)


class RemoteListingTests(unittest.TestCase):
    def test_filters_unsafe_remote_names(self) -> None:
        parsed = parse_remote_audio_listing(
            "light.wav\t1200\nhello world.wav\t25\n../bad\t50\nheavy.wav\t3000\n"
        )
        self.assertEqual(parsed, [("heavy.wav", 3000), ("light.wav", 1200)])
        self.assertTrue(is_safe_remote_filename("medium.wav"))
        self.assertFalse(is_safe_remote_filename("../../config.ini"))


if __name__ == "__main__":
    unittest.main()
