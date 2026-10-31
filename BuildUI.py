#!/usr/bin/env python3
"""BuildUI.py - checkbox build tool for the module/carrier matrix.

Pick the modules you want, pick the shell that should host them, then build:
the selection is written to Config/Config_Flavor_<name>.json and Build.py is
invoked with --modules so unselected modules are not compiled at all.

Carriers (LoaderConfig::Mode):
  pet    - the pet window plus tray (the original full app)
  tray   - tray icon only, pet window stays hidden
  window - a plain module launcher window, pet window stays hidden

Run it with:  BuildUI.bat          (windowless: launches pythonw.exe)
          or:  py BuildUI.py      (keeps a console around, useful for debugging)
"""

import ctypes
import json
import os
import queue
import shutil
import subprocess
import sys
import threading
import traceback
from ctypes import wintypes
from pathlib import Path

# tkinter is imported defensively: under pythonw.exe (no console) a missing
# tkinter would otherwise kill the process before anything is visible. The
# failure is reported by main() through a MessageBox instead.
try:
    import tkinter as tk
    from tkinter import messagebox, ttk
    from tkinter.scrolledtext import ScrolledText
    TK_IMPORT_ERROR = None
except Exception as _exc:  # noqa: BLE001
    tk = None
    messagebox = ttk = ScrolledText = None
    TK_IMPORT_ERROR = _exc

ROOT = Path(__file__).resolve().parent
CONFIG_DIR = ROOT / "Config"
FLAVOR_DIR = ROOT / "Flavors"
STATE_FILE = ROOT / ".agent" / "buildui_state.json"
ERROR_LOG = ROOT / ".agent" / "buildui_error.log"

CARRIERS = [
    ("pet", "Pet 寵物視窗", "完整程式：寵物視窗 + 系統匣"),
    ("tray", "系統匣", "只有系統匣圖示，寵物視窗保持隱藏"),
    ("window", "UI 視窗", "模組啟動器視窗，寵物視窗保持隱藏"),
]

# The tool runs under pythonw (no console). A console child spawned from a
# process without a console allocates a console window of its own, which would
# flash as an empty black window during builds. CREATE_NO_WINDOW prevents that;
# every child's output is redirected into the log pane anyway.
NO_WINDOW = 0x08000000 if os.name == "nt" else 0

UI_FONT = ("Microsoft JhengHei UI", 10)
MONO_FONT = ("Consolas", 9)


# ---------------------------------------------------------------------------
# Environment helpers
# ---------------------------------------------------------------------------

def console_python():
    """Interpreter to use for the child build processes.

    The tool normally runs under pythonw.exe so no console window appears.
    pythonw has no usable console handles, so the children it spawns would not
    be able to attach one; use the console twin (python.exe) instead.
    """
    exe = Path(sys.executable)
    name = exe.name.lower()
    if name in ("pythonw.exe", "pyw.exe"):
        for candidate in (exe.with_name(name.replace("w.exe", ".exe")),
                          Path(sys.base_prefix) / "python.exe"):
            if candidate.exists():
                return str(candidate)
        return "python"
    return sys.executable or "python"


def load_modules():
    """Ask Build.py for the module inventory (single source of truth)."""
    try:
        result = subprocess.run(
            [console_python(), "Build.py", "--list-modules"],
            cwd=str(ROOT),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=60,
            creationflags=NO_WINDOW,
        )
    except Exception as exc:  # noqa: BLE001 - surfaced in the log pane
        raise RuntimeError(f"無法執行 Build.py --list-modules：{exc}") from exc

    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or "Build.py --list-modules 失敗")

    return json.loads(result.stdout)


def resolve_make_path():
    """MakePath.txt holds the full path to mingw32-make.exe (see AGENTS.md)."""
    make_path_file = ROOT / "MakePath.txt"
    candidates = []
    if make_path_file.exists():
        text = make_path_file.read_text(encoding="utf-8", errors="ignore").strip()
        if text:
            candidates.append(Path(text))
    candidates.append(Path(r"D:\Program Files\MSYS2Portable\App\msys64\ucrt64\bin\mingw32-make.exe"))
    found = shutil.which("mingw32-make")
    if found:
        candidates.append(Path(found))

    for candidate in candidates:
        if candidate.exists():
            return candidate
    return None


def target_exe_name():
    target_file = ROOT / "TargetName.txt"
    if target_file.exists():
        text = target_file.read_text(encoding="utf-8", errors="ignore").strip()
        if text:
            return text if text.lower().endswith(".exe") else text + ".exe"
    name_file = ROOT / "ProjectName.txt"
    if name_file.exists():
        text = name_file.read_text(encoding="utf-8", errors="ignore").strip()
        if text:
            return text + ".exe"
    return ROOT.name + ".exe"


def process_ids(exe_name):
    """PIDs of every process whose image *name* is `exe_name`, anywhere.

    tasklist filters on the image name only, which is exactly the scope the
    app's own single-instance guard uses (Core/UpdateManager.cpp: a peer is any
    process with the same file name, not the same directory).
    """
    try:
        result = subprocess.run(
            ["tasklist", "/FO", "CSV", "/NH", "/FI", f"IMAGENAME eq {exe_name}"],
            capture_output=True, text=True, encoding="utf-8", errors="replace",
            creationflags=NO_WINDOW,
        )
    except Exception:  # noqa: BLE001
        return []

    pids = []
    for line in (result.stdout or "").splitlines():
        fields = [f.strip().strip('"') for f in line.split('","')]
        if len(fields) < 2 or not fields[0].strip('"').lower() == exe_name.lower():
            continue
        try:
            pids.append(int(fields[1].strip('"')))
        except ValueError:
            continue
    return pids


def image_path(pid):
    """Full path of a running process's image, or '' when it cannot be read."""
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.OpenProcess.restype = wintypes.HANDLE
    kernel32.QueryFullProcessImageNameW.argtypes = [
        wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)
    ]
    kernel32.QueryFullProcessImageNameW.restype = wintypes.BOOL
    kernel32.CloseHandle.argtypes = [wintypes.HANDLE]

    handle = kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return ""
    try:
        size = wintypes.DWORD(32768)
        buffer = ctypes.create_unicode_buffer(size.value)
        if kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size)):
            return buffer.value
        return ""
    finally:
        kernel32.CloseHandle(handle)


def running_copies(exe_name):
    """[(pid, image_path)] for every running process named `exe_name`."""
    return [(pid, image_path(pid)) for pid in process_ids(exe_name)]


def same_file(left, right):
    if not left or not right:
        return False
    return os.path.normcase(os.path.abspath(left)) == os.path.normcase(os.path.abspath(right))


# ---------------------------------------------------------------------------
# Main window
# ---------------------------------------------------------------------------

class BuildUI:
    def __init__(self, root):
        self.root = root
        self.modules = []
        self.vars = {}
        self.log_queue = queue.Queue()
        self.worker = None
        self.last_returncode = 0

        self.flavor_var = tk.StringVar(value="Default")
        self.carrier_var = tk.StringVar(value="pet")
        self.all_var = tk.BooleanVar(value=True)
        self.clean_var = tk.BooleanVar(value=True)

        self._build_widgets()
        self._load_state()
        self._reload_modules()
        self.root.after(100, self._drain_log)

    # -- layout -------------------------------------------------------------

    def _build_widgets(self):
        self.root.title("Build Tool - 模組挑選器")
        self.root.geometry("940x640")
        self.root.minsize(820, 560)

        style = ttk.Style()
        try:
            style.theme_use("vista")
        except tk.TclError:
            pass

        outer = ttk.Frame(self.root, padding=10)
        outer.pack(fill="both", expand=True)

        top = ttk.Frame(outer)
        top.pack(fill="both", expand=True)

        # --- modules -------------------------------------------------------
        modules_box = ttk.LabelFrame(top, text="模組（勾選要編進這個 Flavor 的模組）", padding=8)
        modules_box.pack(side="left", fill="both", expand=True)

        tools = ttk.Frame(modules_box)
        tools.pack(fill="x", pady=(0, 6))
        ttk.Button(tools, text="全選", width=8, command=self._select_all).pack(side="left")
        ttk.Button(tools, text="全不選", width=8, command=self._select_none).pack(side="left", padx=6)
        ttk.Checkbutton(tools, text="全部編入（忽略勾選）", variable=self.all_var,
                        command=self._sync_all_state).pack(side="left", padx=12)
        ttk.Button(tools, text="重新整理", width=9, command=self._reload_modules).pack(side="right")

        list_wrap = ttk.Frame(modules_box)
        list_wrap.pack(fill="both", expand=True)

        self.canvas = tk.Canvas(list_wrap, highlightthickness=0, borderwidth=0)
        scrollbar = ttk.Scrollbar(list_wrap, orient="vertical", command=self.canvas.yview)
        self.module_frame = ttk.Frame(self.canvas)
        self.canvas.configure(yscrollcommand=scrollbar.set)
        self.module_frame.bind(
            "<Configure>",
            lambda _e: self.canvas.configure(scrollregion=self.canvas.bbox("all")),
        )
        self.canvas_window = self.canvas.create_window((0, 0), window=self.module_frame, anchor="nw")
        self.canvas.bind("<Configure>", self._on_canvas_resize)
        self.canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")

        # --- flavour / carrier --------------------------------------------
        side = ttk.Frame(top, padding=(12, 0, 0, 0))
        side.pack(side="right", fill="y")

        flavor_box = ttk.LabelFrame(side, text="Flavor", padding=8)
        flavor_box.pack(fill="x")
        ttk.Label(flavor_box, text="名稱").grid(row=0, column=0, sticky="w")
        ttk.Entry(flavor_box, textvariable=self.flavor_var, width=22).grid(
            row=0, column=1, sticky="we", padx=(6, 0))
        ttk.Label(flavor_box, text="輸出：Config/Config_Flavor_<名稱>.json",
                  foreground="#666").grid(row=1, column=0, columnspan=2, sticky="w", pady=(6, 0))

        carrier_box = ttk.LabelFrame(side, text="載體（用哪個介面顯示）", padding=8)
        carrier_box.pack(fill="x", pady=(10, 0))
        for key, label, hint in CARRIERS:
            ttk.Radiobutton(carrier_box, text=label, value=key,
                            variable=self.carrier_var).pack(anchor="w")
            ttk.Label(carrier_box, text="   " + hint, foreground="#666").pack(anchor="w")

        option_box = ttk.LabelFrame(side, text="建置選項", padding=8)
        option_box.pack(fill="x", pady=(10, 0))
        ttk.Checkbutton(option_box, text="建置前先 make clean", variable=self.clean_var).pack(anchor="w")
        self.exe_label = ttk.Label(option_box, text="", foreground="#666")
        self.exe_label.pack(anchor="w", pady=(6, 0))

        # --- actions -------------------------------------------------------
        actions = ttk.Frame(outer)
        actions.pack(fill="x", pady=(10, 6))

        self.build_button = ttk.Button(actions, text="Build", width=14, command=self.on_build)
        self.build_button.pack(side="left")
        self.run_button = ttk.Button(actions, text="執行", width=10, command=self.on_run)
        self.run_button.pack(side="left", padx=6)
        ttk.Button(actions, text="只產生 Flavor 檔", width=16,
                   command=self.on_export_flavor).pack(side="left")
        ttk.Button(actions, text="清空紀錄", width=10, command=self._clear_log).pack(side="right")

        self.status_var = tk.StringVar(value="待命")
        ttk.Label(actions, textvariable=self.status_var, foreground="#0a5").pack(side="right", padx=12)

        # --- log -----------------------------------------------------------
        log_box = ttk.LabelFrame(outer, text="輸出", padding=6)
        log_box.pack(fill="both", expand=False)
        self.log = ScrolledText(log_box, height=14, font=MONO_FONT, wrap="word",
                                state="disabled", background="#111", foreground="#ddd")
        self.log.pack(fill="both", expand=True)

    def _on_canvas_resize(self, event):
        self.canvas.itemconfigure(self.canvas_window, width=event.width)

    # -- module list --------------------------------------------------------

    def _reload_modules(self):
        try:
            self.modules = load_modules()
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("Build Tool", str(exc))
            return

        for child in self.module_frame.winfo_children():
            child.destroy()
        self.vars.clear()

        for module in self.modules:
            name = module.get("name") or module.get("dir")
            enabled = module.get("enabled", True)
            sources = module.get("sources") or []
            var = tk.BooleanVar(value=enabled)
            self.vars[name] = var

            row = ttk.Frame(self.module_frame)
            row.pack(fill="x", anchor="w")
            check = ttk.Checkbutton(row, text=name, variable=var)
            check.pack(side="left")
            detail = f"   {len(sources)} 個檔案"
            if module.get("class"):
                detail += f" · {module['class']}"
            if not enabled:
                detail += " · manifest Enabled=0"
                check.state(["disabled"])
            ttk.Label(row, text=detail, foreground="#777",
                      font=("Microsoft JhengHei UI", 9)).pack(side="left")

        self.exe_label.configure(text=f"輸出：{target_exe_name()}")
        self._sync_all_state()
        self._apply_state_flags()

    def _select_all(self):
        self.all_var.set(False)
        for var in self.vars.values():
            var.set(True)
        self._sync_all_state()

    def _select_none(self):
        self.all_var.set(False)
        for var in self.vars.values():
            var.set(False)
        self._sync_all_state()

    def _sync_all_state(self):
        disabled = bool(self.all_var.get())
        for child in self.module_frame.winfo_children():
            for widget in child.winfo_children():
                if isinstance(widget, ttk.Checkbutton):
                    widget.state(["disabled"] if disabled else ["!disabled"])

    # -- state --------------------------------------------------------------

    def _load_state(self):
        if not STATE_FILE.exists():
            return
        try:
            data = json.loads(STATE_FILE.read_text(encoding="utf-8"))
        except Exception:  # noqa: BLE001
            return
        self.flavor_var.set(data.get("flavor") or "Default")
        carrier = data.get("carrier")
        if carrier in [c[0] for c in CARRIERS]:
            self.carrier_var.set(carrier)
        self.clean_var.set(bool(data.get("clean", True)))
        self.all_var.set(bool(data.get("all", True)))
        self._state_modules = data.get("modules") or []

    _state_modules = []

    def _apply_state_flags(self):
        wanted = set(self._state_modules)
        if not wanted:
            return
        self.all_var.set(False)
        for name, var in self.vars.items():
            var.set(name in wanted)
        self._sync_all_state()

    def _save_state(self):
        STATE_FILE.parent.mkdir(parents=True, exist_ok=True)
        data = {
            "flavor": self.flavor_var.get().strip(),
            "carrier": self.carrier_var.get(),
            "clean": bool(self.clean_var.get()),
            "all": bool(self.all_var.get()),
            "modules": self.selected_modules(),
        }
        try:
            STATE_FILE.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
        except Exception:  # noqa: BLE001
            pass

    # -- selection ----------------------------------------------------------

    def selected_modules(self):
        return [name for name, var in self.vars.items() if var.get()]

    def flavor_name(self):
        raw = self.flavor_var.get().strip()
        if not raw:
            raw = self.carrier_var.get()
        safe = "".join(ch for ch in raw if ch.isalnum() or ch in "-_")
        return safe or "Default"

    # -- flavour file -------------------------------------------------------

    def _write_flavor(self, flavor, carrier, modules):
        CONFIG_DIR.mkdir(parents=True, exist_ok=True)
        path = CONFIG_DIR / f"Config_Flavor_{flavor}.json"
        payload = {"mode": carrier, "modules": modules}
        path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        self._log_line(f"[flavor] 已寫入 {path.relative_to(ROOT)}  (mode={carrier}, {len(modules)} 個模組)")
        return path

    def _write_launcher(self, flavor):
        FLAVOR_DIR.mkdir(parents=True, exist_ok=True)
        path = FLAVOR_DIR / f"Run_{flavor}.bat"
        content = (
            "@echo off\r\n"
            "cd /d \"%~dp0..\"\r\n"
            f"start \"\" \"%~dp0..\\{target_exe_name()}\" --flavor {flavor}\r\n"
        )
        path.write_text(content, encoding="utf-8")
        self._log_line(f"[flavor] 已寫入啟動檔 {path.relative_to(ROOT)}")
        return path

    def on_export_flavor(self):
        flavor = self.flavor_name()
        modules = self.selected_modules()
        if not modules and not self.all_var.get():
            if not messagebox.askyesno("Build Tool", "沒有勾選任何模組，仍要產生 Flavor 檔嗎？"):
                return
        self._write_flavor(flavor, self.carrier_var.get(), modules)
        self._write_launcher(flavor)
        self._save_state()
        self.status_var.set(f"Flavor 已輸出：{flavor}")

    # -- build --------------------------------------------------------------

    def on_build(self):
        if self.worker and self.worker.is_alive():
            return

        make = resolve_make_path()
        if make is None:
            messagebox.showerror(
                "Build Tool",
                "找不到 mingw32-make.exe。請在 MakePath.txt 寫入完整路徑，例如：\n"
                r"D:\Program Files\MSYS2Portable\App\msys64\ucrt64\bin\mingw32-make.exe",
            )
            return

        flavor = self.flavor_name()
        modules = self.selected_modules()
        if not self.all_var.get() and not modules:
            messagebox.showwarning("Build Tool", "沒有勾選任何模組。請勾選至少一個，或勾選「全部編入」。")
            return

        exe_name = target_exe_name()
        target = ROOT / exe_name
        copies = running_copies(exe_name)

        # Only the running image itself keeps a lock on the file the linker has
        # to overwrite. A copy started from another folder holds *that* file,
        # not ours, so it does not block the build - report it and carry on.
        ours = [(pid, path) for pid, path in copies if same_file(path, target)]
        others = [(pid, path) for pid, path in copies if (pid, path) not in ours]

        if others:
            for _pid, path in others:
                self._log_line(f"[build] 另一個副本正在執行：{path or '<路徑未知>'}（不影響建置）")

        if ours:
            if not messagebox.askyesno(
                "Build Tool",
                f"{exe_name} 正在執行，而且就是這次要覆寫的檔案：\n"
                f"{target}\n\n"
                "連結時會因為檔案被佔用而失敗（Permission denied）。\n"
                "要結束它並繼續建置嗎？",
            ):
                return
            for pid, _path in ours:
                subprocess.run(["taskkill", "/F", "/PID", str(pid)],
                               capture_output=True, text=True,
                               creationflags=NO_WINDOW)
            self._log_line(f"[build] 已結束 {exe_name}（PID {', '.join(str(p) for p, _ in ours)}）")

        self._write_flavor(flavor, self.carrier_var.get(), modules)
        self._write_launcher(flavor)
        self._save_state()

        commands = []
        if self.clean_var.get():
            commands.append([str(make), "SHELL=cmd.exe", "clean"])
        build_py = [console_python(), "Build.py"]
        if not self.all_var.get():
            build_py += ["--modules", ",".join(modules)]
        commands.append(build_py)
        commands.append([str(make), "SHELL=cmd.exe"])

        env = os.environ.copy()
        env["PATH"] = str(make.parent) + os.pathsep + env.get("PATH", "")

        self._clear_log()
        self._log_line(f"[build] Flavor = {flavor}  carrier = {self.carrier_var.get()}")
        self._log_line("[build] 模組 = " + (", ".join(modules) if not self.all_var.get() else "全部"))
        self.build_button.state(["disabled"])
        self.status_var.set("建置中…")

        self.worker = threading.Thread(target=self._run_commands, args=(commands, env), daemon=True)
        self.worker.start()

    def _run_commands(self, commands, env):
        self.last_returncode = 0
        for cmd in commands:
            self.log_queue.put(("line", "\n$ " + " ".join(cmd)))
            try:
                proc = subprocess.Popen(
                    cmd, cwd=str(ROOT), env=env,
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    text=True, encoding="utf-8", errors="replace", bufsize=1,
                    creationflags=NO_WINDOW,
                )
            except Exception as exc:  # noqa: BLE001
                self.log_queue.put(("error", f"無法執行：{exc}"))
                self.last_returncode = 1
                break

            assert proc.stdout is not None
            for line in proc.stdout:
                self.log_queue.put(("line", line.rstrip("\r\n")))
            proc.wait()
            self.last_returncode = proc.returncode
            if proc.returncode != 0:
                self.log_queue.put(("error", f"指令失敗（exit {proc.returncode}），中止後續步驟。"))
                break

        self.log_queue.put(("done", self.last_returncode))

    # -- log ----------------------------------------------------------------

    def _log_line(self, text):
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def _clear_log(self):
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")

    def _drain_log(self):
        try:
            while True:
                kind, payload = self.log_queue.get_nowait()
                if kind == "line":
                    self._log_line(payload)
                elif kind == "error":
                    self._log_line("[error] " + payload)
                elif kind == "done":
                    self.build_button.state(["!disabled"])
                    if payload == 0:
                        self.status_var.set("建置成功")
                        self._log_line("\n[build] 完成。可按「執行」測試目前 Flavor。")
                    else:
                        self.status_var.set("建置失敗")
                        self._log_line("\n[build] 失敗，請看上面的輸出。")
        except queue.Empty:
            pass
        self.root.after(100, self._drain_log)

    # -- run ----------------------------------------------------------------

    def on_run(self):
        exe = ROOT / target_exe_name()
        if not exe.exists():
            messagebox.showerror("Build Tool", f"找不到 {exe.name}，請先 Build。")
            return

        # The app treats "another process with the same image name" as the same
        # application (single instance + in-place update), no matter which
        # folder it lives in. Launching this build while a copy runs therefore
        # does not open a second window: it either hot-swaps the new file into
        # that copy's path and relaunches it, or just says "already running".
        copies = running_copies(exe.name)
        if copies:
            listed = "\n".join("  " + (path or f"PID {pid}") for pid, path in copies)
            if not messagebox.askyesno(
                "Build Tool",
                f"{exe.name} 已經有副本在執行：\n{listed}\n\n"
                "程式以「檔名」判定單一實例，所以直接啟動新檔不會開第二個視窗："
                "版本較新時會把新檔熱替換到那個副本的路徑並重啟它，"
                "否則只顯示「已在執行中」。\n\n"
                "要結束那些副本，再用目前這份建置啟動嗎？",
            ):
                return
            for pid, _path in copies:
                subprocess.run(["taskkill", "/F", "/PID", str(pid)],
                               capture_output=True, text=True,
                               creationflags=NO_WINDOW)
            self._log_line("[run] 已結束執行中的副本")

        flavor = self.flavor_name()
        if not (CONFIG_DIR / f"Config_Flavor_{flavor}.json").exists():
            self.on_export_flavor()

        self._save_state()
        args = [str(exe), "--flavor", flavor]
        self._log_line("[run] " + " ".join(args))
        try:
            subprocess.Popen(args, cwd=str(ROOT), close_fds=True,
                             creationflags=NO_WINDOW)
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("Build Tool", f"無法啟動：{exc}")

    # -- close --------------------------------------------------------------

    def on_close(self):
        self._save_state()
        self.root.destroy()


def report_fatal(message):
    """Show a startup failure without relying on tkinter or a console."""
    try:
        import ctypes
        ctypes.windll.user32.MessageBoxW(None, message, "Build Tool", 0x10)
    except Exception:  # noqa: BLE001
        pass


def main():
    if TK_IMPORT_ERROR is not None:
        report_fatal("Build Tool 無法啟動：找不到 tkinter。\n\n" + str(TK_IMPORT_ERROR) +
                     "\n\n請安裝含 Tcl/Tk 的 Python，或改用 py BuildUI.py 查看錯誤。")
        return 1

    root = tk.Tk()
    try:
        root.option_add("*Font", UI_FONT)
    except tk.TclError:
        pass
    app = BuildUI(root)
    root.protocol("WM_DELETE_WINDOW", app.on_close)
    root.mainloop()
    return 0


if __name__ == "__main__":
    try:
        code = main()
    except Exception:  # noqa: BLE001
        # No console exists under pythonw, so a traceback would vanish.
        detail = traceback.format_exc()
        try:
            ERROR_LOG.parent.mkdir(parents=True, exist_ok=True)
            ERROR_LOG.write_text(detail, encoding="utf-8")
        except Exception:  # noqa: BLE001
            pass
        report_fatal("Build Tool 發生未預期的錯誤：\n\n" + detail[-1200:] +
                     "\n\n詳細記錄：" + str(ERROR_LOG))
        code = 1
    sys.exit(code)
