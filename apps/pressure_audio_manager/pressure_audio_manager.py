"""Windows GUI for configuring pressure-triggered audio on the RV1126B board."""

from __future__ import annotations

import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk
from typing import Callable, Optional

from core import (
    AdbDevice,
    ManagerError,
    ManagerConfig,
    SLOT_LABELS,
    SLOTS,
    build_config,
    cleanup_prepared,
    delete_remote_audio,
    describe_audio,
    ensure_ready_device,
    find_adb,
    find_ffmpeg,
    format_bytes,
    inspect_audio,
    list_devices,
    list_remote_audio,
    preview_local_audio,
    prepare_audio,
    query_storage,
    read_remote_config,
    test_remote_audio,
    upload_audio,
    write_remote_config,
)


class PressureAudioManager(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("RV1126B 压力声音管理器")
        self.geometry("1120x760")
        self.minsize(980, 680)
        self.configure(bg="#151b20")

        self.adb = find_adb()
        self.ffmpeg = find_ffmpeg()
        self.devices: list[AdbDevice] = []
        self.selected_files: dict[str, Path] = {}
        self.busy = False

        self._configure_style()
        self._build_ui()
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.after(150, self.refresh_devices)

    def _on_close(self) -> None:
        if self.busy:
            messagebox.showinfo("任务进行中", "请等待当前转码或传输任务完成后再关闭。", parent=self)
            return
        self.destroy()

    def _configure_style(self) -> None:
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure(".", font=("Microsoft YaHei UI", 10), background="#151b20", foreground="#e8edf0")
        style.configure("TFrame", background="#151b20")
        style.configure("Panel.TFrame", background="#202830")
        style.configure("Card.TFrame", background="#27313a")
        style.configure("TLabel", background="#151b20", foreground="#e8edf0")
        style.configure("Muted.TLabel", background="#202830", foreground="#9aa8b2")
        style.configure("Card.TLabel", background="#27313a", foreground="#e8edf0")
        style.configure("Title.TLabel", font=("Microsoft YaHei UI", 22, "bold"), foreground="#f1f5f6")
        style.configure("Eyebrow.TLabel", font=("Consolas", 9, "bold"), foreground="#4dd0b5")
        style.configure("Section.TLabel", font=("Microsoft YaHei UI", 12, "bold"), background="#202830")
        style.configure("Slot.TLabel", font=("Microsoft YaHei UI", 12, "bold"), background="#27313a")
        style.configure("TButton", padding=(12, 7), background="#34414b", foreground="#f5f7f8", borderwidth=0)
        style.map("TButton", background=[("active", "#42515d"), ("pressed", "#2c363f")])
        style.configure("Accent.TButton", background="#d97735", foreground="#fffaf5")
        style.map("Accent.TButton", background=[("active", "#ee8b47"), ("pressed", "#b95f27")])
        style.configure("Danger.TButton", background="#713d3d", foreground="#fff4f4")
        style.map("Danger.TButton", background=[("active", "#8d4747")])
        style.configure("TEntry", fieldbackground="#11171b", foreground="#edf2f4", insertcolor="#edf2f4")
        style.configure("TCombobox", fieldbackground="#11171b", foreground="#edf2f4", arrowcolor="#4dd0b5")
        style.map("TCombobox", fieldbackground=[("readonly", "#11171b")], foreground=[("readonly", "#edf2f4")])
        style.configure("TSpinbox", fieldbackground="#11171b", foreground="#edf2f4", arrowcolor="#4dd0b5")
        style.configure("Treeview", background="#11171b", fieldbackground="#11171b", foreground="#dfe7eb", rowheight=28, borderwidth=0)
        style.configure("Treeview.Heading", background="#2b3740", foreground="#dce5e9", font=("Microsoft YaHei UI", 10, "bold"))
        style.map("Treeview", background=[("selected", "#37695f")])

    def _build_ui(self) -> None:
        header = ttk.Frame(self, padding=(28, 22, 28, 16))
        header.pack(fill="x")
        title_box = ttk.Frame(header)
        title_box.pack(side="left")
        ttk.Label(title_box, text="BOARD AUDIO / PRESSURE", style="Eyebrow.TLabel").pack(anchor="w")
        ttk.Label(title_box, text="压力声音管理器", style="Title.TLabel").pack(anchor="w", pady=(2, 0))
        ttk.Label(
            header,
            text="电脑选择素材 → 安全上传 → 开发板离线触发播放",
            foreground="#9aa8b2",
        ).pack(side="right", anchor="s", pady=(0, 5))

        body = ttk.Frame(self, padding=(28, 0, 28, 16))
        body.pack(fill="both", expand=True)
        body.columnconfigure(0, weight=0, minsize=288)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)

        self._build_connection_panel(body)
        self._build_workspace(body)

        footer = ttk.Frame(self, style="Panel.TFrame", padding=(20, 9))
        footer.pack(fill="x", side="bottom")
        self.status_var = tk.StringVar(value="准备就绪")
        ttk.Label(footer, textvariable=self.status_var, style="Muted.TLabel").pack(side="left")
        self.tool_var = tk.StringVar()
        self._update_tool_status()
        ttk.Label(footer, textvariable=self.tool_var, style="Muted.TLabel").pack(side="right")

    def _build_connection_panel(self, parent: ttk.Frame) -> None:
        panel = ttk.Frame(parent, style="Panel.TFrame", padding=20)
        panel.grid(row=0, column=0, sticky="nsew", padx=(0, 16))
        ttk.Label(panel, text="连接", style="Section.TLabel").pack(anchor="w")
        ttk.Label(panel, text="ADB 设备", style="Muted.TLabel").pack(anchor="w", pady=(18, 5))
        self.device_var = tk.StringVar()
        self.device_combo = ttk.Combobox(panel, textvariable=self.device_var, state="readonly", width=32)
        self.device_combo.pack(fill="x")
        self.device_combo.bind("<<ComboboxSelected>>", lambda _event: self.refresh_board_state())
        ttk.Button(panel, text="刷新设备", command=self.refresh_devices).pack(fill="x", pady=(10, 0))

        ttk.Label(panel, text="/userdata 存储", style="Muted.TLabel").pack(anchor="w", pady=(22, 5))
        self.storage_var = tk.StringVar(value="等待连接开发板")
        ttk.Label(
            panel,
            textvariable=self.storage_var,
            style="Muted.TLabel",
            justify="left",
            wraplength=245,
        ).pack(anchor="w")

        ttk.Separator(panel).pack(fill="x", pady=22)
        ttk.Label(panel, text="使用提示", style="Section.TLabel").pack(anchor="w")
        notes = (
            "• USB 调试是首次配置最稳定的方式\n"
            "• 上传采用临时文件原子替换\n"
            "• 素材最终保存在开发板中\n"
            "• 断开电脑后仍可触发播放"
        )
        ttk.Label(panel, text=notes, style="Muted.TLabel", justify="left", wraplength=245).pack(anchor="w", pady=(12, 0))

        self.refresh_remote_button = ttk.Button(panel, text="刷新板内素材", command=self.refresh_remote_audio)
        self.refresh_remote_button.pack(fill="x", side="bottom")

    def _build_workspace(self, parent: ttk.Frame) -> None:
        notebook = ttk.Notebook(parent)
        notebook.grid(row=0, column=1, sticky="nsew")
        upload_tab = ttk.Frame(notebook, style="Panel.TFrame", padding=18)
        config_tab = ttk.Frame(notebook, style="Panel.TFrame", padding=18)
        library_tab = ttk.Frame(notebook, style="Panel.TFrame", padding=18)
        notebook.add(upload_tab, text="  声音素材  ")
        notebook.add(config_tab, text="  触发配置  ")
        notebook.add(library_tab, text="  板内文件  ")

        ttk.Label(upload_tab, text="三个压力等级", style="Section.TLabel").pack(anchor="w")
        ttk.Label(
            upload_tab,
            text="每个槽位最终保存为固定名称 WAV；替换文件不会影响配置路径。",
            style="Muted.TLabel",
        ).pack(anchor="w", pady=(4, 14))
        self.slot_info_vars: dict[str, tk.StringVar] = {}
        for slot in SLOTS:
            self._build_slot_card(upload_tab, slot)

        ttk.Label(config_tab, text="播放与阈值", style="Section.TLabel").grid(row=0, column=0, columnspan=4, sticky="w")
        ttk.Label(
            config_tab,
            text="保存后原子写入 config.ini；压力应用运行时约 1 秒内自动生效。",
            style="Muted.TLabel",
        ).grid(row=1, column=0, columnspan=4, sticky="w", pady=(4, 20))

        self.mode_var = tk.StringVar(value="tts")
        self.enabled_var = tk.BooleanVar(value=False)
        self.fallback_var = tk.BooleanVar(value=True)
        self.volume_var = tk.IntVar(value=100)
        self.light_threshold_var = tk.IntVar(value=3)
        self.medium_threshold_var = tk.IntVar(value=25)
        self.heavy_threshold_var = tk.IntVar(value=60)
        self.cooldown_var = tk.IntVar(value=2000)

        fields = (
            ("声音模式", ttk.Combobox(config_tab, textvariable=self.mode_var, values=("clips", "tts"), state="readonly", width=15)),
            ("音量 (%)", ttk.Spinbox(config_tab, textvariable=self.volume_var, from_=0, to=100, width=16)),
            ("轻按起点 (%)", ttk.Spinbox(config_tab, textvariable=self.light_threshold_var, from_=1, to=100, width=16)),
            ("中按起点 (%)", ttk.Spinbox(config_tab, textvariable=self.medium_threshold_var, from_=0, to=100, width=16)),
            ("重按起点 (%)", ttk.Spinbox(config_tab, textvariable=self.heavy_threshold_var, from_=0, to=100, width=16)),
            ("播放间隔 (ms)", ttk.Spinbox(config_tab, textvariable=self.cooldown_var, from_=0, to=60000, increment=100, width=16)),
        )
        for index, (label, widget) in enumerate(fields):
            row = 2 + index // 2
            column = (index % 2) * 2
            ttk.Label(config_tab, text=label, style="Muted.TLabel").grid(row=row, column=column, sticky="w", padx=(0, 10), pady=9)
            widget.grid(row=row, column=column + 1, sticky="ew", pady=9, padx=(0, 24))
        config_tab.columnconfigure(1, weight=1)
        config_tab.columnconfigure(3, weight=1)
        ttk.Checkbutton(
            config_tab,
            text="启用压力声音反馈",
            variable=self.enabled_var,
        ).grid(row=5, column=0, columnspan=2, sticky="w", pady=(18, 8))
        ttk.Checkbutton(
            config_tab,
            text="素材缺失或播放失败时退回 eSpeak 合成语音",
            variable=self.fallback_var,
        ).grid(row=5, column=2, columnspan=2, sticky="w", pady=(18, 8))
        config_buttons = ttk.Frame(config_tab, style="Panel.TFrame")
        config_buttons.grid(row=6, column=0, columnspan=4, sticky="ew", pady=(14, 0))
        config_buttons.columnconfigure(0, weight=1)
        config_buttons.columnconfigure(1, weight=1)
        ttk.Button(config_buttons, text="读取板端配置", command=self.load_config).grid(
            row=0, column=0, sticky="ew", padx=(0, 6)
        )
        ttk.Button(config_buttons, text="保存配置到开发板", style="Accent.TButton", command=self.save_config).grid(
            row=0, column=1, sticky="ew", padx=(6, 0)
        )

        ttk.Label(library_tab, text="板内声音素材", style="Section.TLabel").pack(anchor="w")
        ttk.Label(
            library_tab,
            text="这里只管理工具上传的安全文件名；删除前会再次确认。",
            style="Muted.TLabel",
        ).pack(anchor="w", pady=(4, 12))
        columns = ("name", "size")
        self.remote_tree = ttk.Treeview(library_tab, columns=columns, show="headings", selectmode="browse")
        self.remote_tree.heading("name", text="文件名")
        self.remote_tree.heading("size", text="大小")
        self.remote_tree.column("name", width=360, anchor="w")
        self.remote_tree.column("size", width=130, anchor="e")
        self.remote_tree.pack(fill="both", expand=True)
        button_row = ttk.Frame(library_tab, style="Panel.TFrame")
        button_row.pack(fill="x", pady=(12, 0))
        ttk.Button(button_row, text="刷新", command=self.refresh_remote_audio).pack(side="left")
        ttk.Button(button_row, text="测试播放", command=self.test_selected_remote).pack(side="left", padx=8)
        ttk.Button(button_row, text="删除", style="Danger.TButton", command=self.delete_selected_remote).pack(side="right")

    def _build_slot_card(self, parent: ttk.Frame, slot: str) -> None:
        card = ttk.Frame(parent, style="Card.TFrame", padding=15)
        card.pack(fill="x", pady=6)
        card.columnconfigure(1, weight=1)
        ttk.Label(card, text=SLOT_LABELS[slot], style="Slot.TLabel", width=6).grid(row=0, column=0, rowspan=2, sticky="nw")
        info_var = tk.StringVar(value="尚未选择文件")
        self.slot_info_vars[slot] = info_var
        ttk.Label(card, textvariable=info_var, style="Card.TLabel", wraplength=440).grid(row=0, column=1, sticky="w", padx=(8, 12))
        ttk.Label(card, text=f"板内文件：{slot}.wav", style="Card.TLabel", foreground="#8ea0aa").grid(
            row=1, column=1, sticky="w", padx=(8, 12), pady=(4, 0)
        )
        ttk.Button(card, text="选择", command=lambda value=slot: self.choose_file(value)).grid(row=0, column=2, padx=4)
        ttk.Button(card, text="上传", style="Accent.TButton", command=lambda value=slot: self.upload_slot(value)).grid(row=0, column=3, padx=4)
        ttk.Button(card, text="本机试听", command=lambda value=slot: self.preview_slot(value)).grid(
            row=1, column=2, sticky="ew", padx=4, pady=(5, 0)
        )
        ttk.Button(card, text="板端试听", command=lambda value=slot: self.test_slot(value)).grid(
            row=1, column=3, sticky="ew", padx=4, pady=(5, 0)
        )

    def _update_tool_status(self) -> None:
        adb_text = "ADB 就绪" if self.adb else "未找到 ADB"
        ffmpeg_text = "FFmpeg 就绪" if self.ffmpeg else "无 FFmpeg（仅兼容 WAV）"
        self.tool_var.set(f"{adb_text}   |   {ffmpeg_text}")

    def _run_task(self, label: str, action: Callable[[], object], success: Optional[Callable[[object], None]] = None) -> None:
        if self.busy:
            self.status_var.set("已有任务正在执行，请稍候。")
            return
        self.busy = True
        self.status_var.set(label)

        def worker() -> None:
            try:
                result = action()
            except Exception as exc:  # surfaced on the UI thread
                self.after(0, lambda error=exc: self._finish_error(error))
                return
            self.after(0, lambda: self._finish_success(result, success))

        threading.Thread(target=worker, daemon=True).start()

    def _finish_error(self, error: Exception) -> None:
        self.busy = False
        text = str(error) or error.__class__.__name__
        self.status_var.set(f"失败：{text}")
        messagebox.showerror("操作失败", text, parent=self)

    def _finish_success(self, result: object, callback: Optional[Callable[[object], None]]) -> None:
        self.busy = False
        if callback:
            callback(result)

    def _selected_device(self) -> AdbDevice:
        index = self.device_combo.current()
        if index < 0 or index >= len(self.devices):
            raise ManagerError("请先连接并选择开发板。")
        ensure_ready_device(self.devices[index])
        return self.devices[index]

    def refresh_devices(self) -> None:
        if not self.adb:
            self.status_var.set("未找到 adb.exe；请安装 Android platform-tools 并加入 PATH。")
            return

        previous = self.devices[self.device_combo.current()].serial if self.devices and self.device_combo.current() >= 0 else None

        def done(value: object) -> None:
            self.devices = list(value)  # type: ignore[arg-type]
            self.device_combo["values"] = [device.display_name for device in self.devices]
            selected = 0
            if previous:
                selected = next((i for i, device in enumerate(self.devices) if device.serial == previous), 0)
            if self.devices:
                self.device_combo.current(selected)
                self.status_var.set(f"发现 {len(self.devices)} 个 ADB 设备。")
                self.refresh_board_state()
            else:
                self.device_var.set("")
                self.storage_var.set("未检测到设备")
                self.status_var.set("未检测到 ADB 设备。")

        self._run_task("正在扫描 ADB 设备…", lambda: list_devices(self.adb or "adb"), done)

    def refresh_board_state(self) -> None:
        try:
            device = self._selected_device()
        except ManagerError as exc:
            self.storage_var.set(str(exc))
            return

        def done(value: object) -> None:
            self.storage_var.set(str(value))
            self.status_var.set(f"已连接 {device.serial}")

        self._run_task("正在读取开发板存储…", lambda: query_storage(self.adb or "adb", device.serial), done)

    def choose_file(self, slot: str) -> None:
        filename = filedialog.askopenfilename(
            parent=self,
            title=f"选择{SLOT_LABELS[slot]}声音",
            filetypes=(
                ("音频文件", "*.wav *.mp3 *.flac *.m4a *.aac *.ogg"),
                ("所有文件", "*.*"),
            ),
        )
        if not filename:
            return
        try:
            info = inspect_audio(filename)
        except ManagerError as exc:
            messagebox.showerror("不能读取文件", str(exc), parent=self)
            return
        self.selected_files[slot] = info.path
        self.slot_info_vars[slot].set(f"{info.path.name}\n{describe_audio(info, bool(self.ffmpeg))}")
        self.status_var.set(f"已为{SLOT_LABELS[slot]}选择 {info.path.name}")

    def upload_slot(self, slot: str) -> None:
        source = self.selected_files.get(slot)
        if not source:
            messagebox.showinfo("先选择文件", f"请先为{SLOT_LABELS[slot]}选择一个音频文件。", parent=self)
            return
        try:
            device = self._selected_device()
        except ManagerError as exc:
            messagebox.showerror("设备不可用", str(exc), parent=self)
            return

        def action() -> tuple[str, bool]:
            prepared = prepare_audio(source, self.ffmpeg)
            try:
                destination = upload_audio(self.adb or "adb", device.serial, prepared.path, slot)
                return destination, prepared.converted
            finally:
                cleanup_prepared(prepared)

        def done(value: object) -> None:
            destination, converted = value  # type: ignore[misc]
            conversion = "（已自动转码）" if converted else ""
            self.status_var.set(f"上传完成{conversion}：{destination}")
            self.refresh_remote_audio()

        self._run_task(f"正在上传{SLOT_LABELS[slot]}声音…", action, done)

    def save_config(self) -> None:
        try:
            device = self._selected_device()
            content = build_config(
                enabled=self.enabled_var.get(),
                mode=self.mode_var.get(),
                fallback_tts=self.fallback_var.get(),
                volume_percent=int(self.volume_var.get()),
                light_threshold=int(self.light_threshold_var.get()),
                medium_threshold=int(self.medium_threshold_var.get()),
                heavy_threshold=int(self.heavy_threshold_var.get()),
                cooldown_ms=int(self.cooldown_var.get()),
            )
        except (ManagerError, ValueError, tk.TclError) as exc:
            messagebox.showerror("配置有误", str(exc), parent=self)
            return

        def done(_value: object) -> None:
            self.status_var.set("配置已安全写入开发板。")
            messagebox.showinfo("保存成功", "配置已写入 config.ini；正在运行的压力应用约 1 秒内自动生效。", parent=self)

        self._run_task(
            "正在原子写入配置…",
            lambda: write_remote_config(self.adb or "adb", device.serial, content),
            done,
        )

    def load_config(self) -> None:
        try:
            device = self._selected_device()
        except ManagerError as exc:
            messagebox.showerror("设备不可用", str(exc), parent=self)
            return

        def done(value: object) -> None:
            config = value
            if not isinstance(config, ManagerConfig):
                raise ManagerError("读取到的配置类型不正确。")
            self.enabled_var.set(config.enabled)
            self.mode_var.set(config.mode)
            self.fallback_var.set(config.fallback_tts)
            self.volume_var.set(config.volume_percent)
            self.light_threshold_var.set(config.light_threshold)
            self.medium_threshold_var.set(config.medium_threshold)
            self.heavy_threshold_var.set(config.heavy_threshold)
            self.cooldown_var.set(config.cooldown_ms)
            self.status_var.set("已读取板端配置。")

        self._run_task(
            "正在读取板端配置…",
            lambda: read_remote_config(self.adb or "adb", device.serial),
            done,
        )

    def refresh_remote_audio(self) -> None:
        try:
            device = self._selected_device()
        except ManagerError as exc:
            messagebox.showerror("设备不可用", str(exc), parent=self)
            return

        def done(value: object) -> None:
            for item in self.remote_tree.get_children():
                self.remote_tree.delete(item)
            entries = list(value)  # type: ignore[arg-type]
            for name, size in entries:
                self.remote_tree.insert("", "end", values=(name, format_bytes(size)))
            self.status_var.set(f"板内共有 {len(entries)} 个声音素材。")

        self._run_task("正在读取板内素材…", lambda: list_remote_audio(self.adb or "adb", device.serial), done)

    def _selected_remote_name(self) -> Optional[str]:
        selection = self.remote_tree.selection()
        if not selection:
            messagebox.showinfo("请选择文件", "请先在板内文件列表中选择一项。", parent=self)
            return None
        values = self.remote_tree.item(selection[0], "values")
        return str(values[0]) if values else None

    def test_slot(self, slot: str) -> None:
        self._test_remote_name(f"{slot}.wav")

    def preview_slot(self, slot: str) -> None:
        source = self.selected_files.get(slot)
        if not source:
            messagebox.showinfo("先选择文件", f"请先为{SLOT_LABELS[slot]}选择一个音频文件。", parent=self)
            return
        try:
            preview_local_audio(source)
        except ManagerError as exc:
            messagebox.showerror("本机试听失败", str(exc), parent=self)
            return
        self.status_var.set(f"已调用系统播放器：{source.name}")

    def test_selected_remote(self) -> None:
        name = self._selected_remote_name()
        if name:
            self._test_remote_name(name)

    def _test_remote_name(self, name: str) -> None:
        try:
            device = self._selected_device()
        except ManagerError as exc:
            messagebox.showerror("设备不可用", str(exc), parent=self)
            return

        def done(_value: object) -> None:
            self.status_var.set(f"开发板正在播放 {name}")

        self._run_task("正在启动板端试听…", lambda: test_remote_audio(self.adb or "adb", device.serial, name), done)

    def delete_selected_remote(self) -> None:
        name = self._selected_remote_name()
        if not name:
            return
        if not messagebox.askyesno("确认删除", f"确定从开发板删除 {name}？", parent=self):
            return
        try:
            device = self._selected_device()
        except ManagerError as exc:
            messagebox.showerror("设备不可用", str(exc), parent=self)
            return

        def done(_value: object) -> None:
            self.status_var.set(f"已删除 {name}")
            self.refresh_remote_audio()

        self._run_task("正在删除板内素材…", lambda: delete_remote_audio(self.adb or "adb", device.serial, name), done)


if __name__ == "__main__":
    PressureAudioManager().mainloop()
