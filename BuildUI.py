#!/usr/bin/env python3
"""BuildUI.py - the single build entry point for this project.

This file replaces the old Build.py / Build.bat / Clear.bat trio. It contains
two halves:

  * codegen + build orchestration (module registry, icon, version,
    translations, GeneratedBuild.mk, make clean / make / make test)
  * the tkinter checkbox UI (modules x carrier x flavour)

Build mode is config-driven (`Config/BuildUI.json`, gitignored):

  release - normal build
  debug   - adds -DDEBUG_CONSOLE=1 (AllocConsole + DBG() + the
            \\\\.\\pipe\\ProjectDebug command pipe used by AI/debug tooling)

Headless CLI (used by the Makefile, scripts and AI agents):

  py BuildUI.py --generate [--modules A,B]     regenerate Generated* files
  py BuildUI.py --list-modules                 JSON module inventory
  py BuildUI.py --build [--mode release|debug] [--clean] [--test]
  py BuildUI.py --clear                        distclean (old Clear.bat)
  py BuildUI.py                                open the UI

Run it windowless with BuildUI.vbs (recommended) or BuildUI.bat.
"""

import configparser
import ctypes
import datetime
import json
import os
import queue
import re
import shutil
import struct
import subprocess
import sys
import threading
import traceback
import zlib
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

# ---------------------------------------------------------------------------
# Generated files
# ---------------------------------------------------------------------------

GENERATED_MK = ROOT / "GeneratedBuild.mk"
GENERATED_ICON_ICO = ROOT / "GeneratedIcon.ico"
GENERATED_ICON_RC = ROOT / "GeneratedIcon.rc"
GENERATED_ICON_OBJ = ROOT / "GeneratedIcon.o"
GENERATED_VERSION_RC = ROOT / "GeneratedVersion.rc"
GENERATED_VERSION_OBJ = ROOT / "GeneratedVersion.o"
GENERATED_REGISTRY_H = ROOT / "GeneratedModuleRegistry.h"
GENERATED_REGISTRY_CPP = ROOT / "GeneratedModuleRegistry.cpp"

ICON_EXTENSIONS = [".png", ".ico", ".jpg", ".jpeg", ".bmp"]
IGNORED_ICON_FILES = {
    "GeneratedIcon.ico",
    "GeneratedIcon.rc",
    "GeneratedIcon.o",
    "Pet.png",
}

CORE_DIRS = ["Core", "Services", "UI", "Pet", "Tray"]
MODULES_DIR = ROOT / "Modules"

# ---------------------------------------------------------------------------
# User config / UI state
# ---------------------------------------------------------------------------

CONFIG_DIR = ROOT / "Config"
FLAVOR_DIR = ROOT / "Flavors"
CONFIG_FILE = CONFIG_DIR / "BuildUI.json"
LEGACY_STATE_FILE = ROOT / ".agent" / "buildui_state.json"
ERROR_LOG = ROOT / ".agent" / "buildui_error.log"

DEFAULT_CONFIG = {
    "mode": "release",          # release | debug
    "carrier": "pet",
    "flavor": "Default",
    "clean": True,
    "all": True,
    "modules": [],
    "last_built_mode": "",
}

MODES = [
    ("release", "Release", "一般版本（不開 Console）"),
    ("debug", "Debug (AI)", "DEBUG_CONSOLE=1：AllocConsole、DBG()、\\\\.\\pipe\\ProjectDebug"),
]

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


# ===========================================================================
# Codegen (formerly Build.py)
# ===========================================================================

def print_info(msg: str):
    print(f"[INFO] {msg}")


def print_warn(msg: str):
    print(f"[WARNING] {msg}")


def print_error(msg: str):
    print(f"[ERROR] {msg}")


def normalize_make_path(path: Path) -> str:
    return str(path.relative_to(ROOT)).replace("\\", "/")


def command_exists(cmd: str) -> bool:
    return shutil.which(cmd) is not None


def read_text_file_if_exists(file_name: str) -> str:
    path = ROOT / file_name
    if not path.exists():
        return ""

    try:
        return path.read_text(encoding="utf-8", errors="ignore").strip()
    except Exception:
        return ""


def choose_project_name() -> str:
    text = read_text_file_if_exists("ProjectName.txt")
    if text:
        return text
    return ROOT.name


def choose_target_name() -> str:
    text = read_text_file_if_exists("TargetName.txt")
    if text:
        if not text.lower().endswith(".exe"):
            text += ".exe"
        return text
    return choose_project_name() + ".exe"


def find_icon_file():
    image_files = []

    for ext in ICON_EXTENSIONS:
        image_files.extend(ROOT.glob(f"*{ext}"))
        image_files.extend(ROOT.glob(f"*{ext.upper()}"))

    filtered = []
    seen = set()

    for path in image_files:
        if path.name in IGNORED_ICON_FILES:
            continue

        key = str(path.resolve()).lower()
        if key in seen:
            continue
        seen.add(key)
        filtered.append(path)

    filtered.sort()

    if not filtered:
        return None

    if len(filtered) > 1:
        print_warn("Multiple image files detected in project root. The first one will be used as icon:")
        for img in filtered:
            print_warn(f"  - {img.name}")

    return filtered[0]


# --- PNG -> ICO without Pillow ---------------------------------------------
#
# The icon resource used to be skipped when Pillow was missing, which silently
# produced an exe without IDI_APP_ICON on any machine that only had a bare
# Python. This decoder covers the formats a normal icon source uses (8-bit
# non-interlaced PNG: gray, gray+alpha, RGB, RGBA) and writes classic 32bpp
# BMP entries, so the icon no longer depends on third-party packages.

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
PNG_CHANNELS = {0: 1, 2: 3, 4: 2, 6: 4}
ICO_SIZES = [256, 128, 64, 48, 32, 16]


def _png_unfilter(raw: bytes, width: int, height: int, bpp: int) -> bytearray:
    stride = width * bpp
    out = bytearray(stride * height)
    prev = bytearray(stride)
    pos = 0

    for y in range(height):
        filter_type = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos + stride])
        pos += stride

        if filter_type == 1:  # Sub
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif filter_type == 2:  # Up
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif filter_type == 3:  # Average
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa = p - a if p > a else a - p
                pb = p - b if p > b else b - p
                pc = p - c if p > c else c - p
                if pa <= pb and pa <= pc:
                    predictor = a
                elif pb <= pc:
                    predictor = b
                else:
                    predictor = c
                line[i] = (line[i] + predictor) & 0xFF
        elif filter_type != 0:
            raise ValueError(f"unsupported PNG filter type {filter_type}")

        out[y * stride:(y + 1) * stride] = line
        prev = line

    return out


def _read_png_rgba(path: Path):
    """Return (rgba_bytes, width, height) for a supported 8-bit PNG."""
    data = path.read_bytes()
    if data[:8] != PNG_SIGNATURE:
        raise ValueError("not a PNG file")

    pos = 8
    idat = []
    width = height = None
    bit_depth = color_type = interlace = None

    while pos + 8 <= len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        chunk_type = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length

        if chunk_type == b"IHDR":
            (width, height, bit_depth, color_type,
             _compression, _filter, interlace) = struct.unpack(">IIBBBBB", chunk)
        elif chunk_type == b"IDAT":
            idat.append(chunk)
        elif chunk_type == b"IEND":
            break

    if width is None:
        raise ValueError("PNG has no IHDR chunk")
    if bit_depth != 8 or color_type not in PNG_CHANNELS or interlace != 0:
        raise ValueError(
            f"unsupported PNG layout (bit depth {bit_depth}, colour type "
            f"{color_type}, interlace {interlace})")

    bpp = PNG_CHANNELS[color_type]
    raw = zlib.decompress(b"".join(idat))
    pixels = _png_unfilter(raw, width, height, bpp)

    if color_type == 6:  # RGBA
        return pixels, width, height

    count = width * height
    rgba = bytearray(count * 4)

    if color_type == 2:  # RGB
        rgba[0::4] = pixels[0::3]
        rgba[1::4] = pixels[1::3]
        rgba[2::4] = pixels[2::3]
        rgba[3::4] = b"\xff" * count
    elif color_type == 4:  # gray + alpha
        rgba[0::4] = pixels[0::2]
        rgba[1::4] = pixels[0::2]
        rgba[2::4] = pixels[0::2]
        rgba[3::4] = pixels[1::2]
    else:  # gray
        rgba[0::4] = pixels
        rgba[1::4] = pixels
        rgba[2::4] = pixels
        rgba[3::4] = b"\xff" * count

    return rgba, width, height


def _crop_square(rgba, width: int, height: int):
    if width == height:
        return rgba, width

    side = min(width, height)
    x0 = (width - side) // 2
    y0 = (height - side) // 2
    out = bytearray(side * side * 4)
    row_bytes = side * 4

    for y in range(side):
        src = ((y0 + y) * width + x0) * 4
        out[y * row_bytes:(y + 1) * row_bytes] = rgba[src:src + row_bytes]

    return out, side


def _resize_rgba(rgba, src_w: int, src_h: int, dst_w: int, dst_h: int) -> bytearray:
    """Box-filter resize; averages every source pixel that covers the target."""
    dst = bytearray(dst_w * dst_h * 4)

    for dy in range(dst_h):
        y0 = dy * src_h // dst_h
        y1 = max(y0 + 1, (dy + 1) * src_h // dst_h)
        for dx in range(dst_w):
            x0 = dx * src_w // dst_w
            x1 = max(x0 + 1, (dx + 1) * src_w // dst_w)

            r = g = b = a = 0
            count = 0
            for y in range(y0, y1):
                offset = (y * src_w + x0) * 4
                for _x in range(x0, x1):
                    r += rgba[offset]
                    g += rgba[offset + 1]
                    b += rgba[offset + 2]
                    a += rgba[offset + 3]
                    offset += 4
                    count += 1

            out = (dy * dst_w + dx) * 4
            dst[out] = r // count
            dst[out + 1] = g // count
            dst[out + 2] = b // count
            dst[out + 3] = a // count

    return dst


def _write_ico(path: Path, entries):
    """Write classic 32bpp BMP ICO entries (no PNG-in-ICO tricks)."""
    header = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    directory = bytearray()
    blobs = []

    for size, pixels in entries:
        # BITMAPINFOHEADER: height is doubled because it covers XOR + AND maps.
        dib = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0,
                          size * size * 4, 0, 0, 0, 0)

        xor = bytearray()
        for y in range(size - 1, -1, -1):
            row = pixels[y * size * 4:(y + 1) * size * 4]
            bgra = bytearray(len(row))
            bgra[0::4] = row[2::4]
            bgra[1::4] = row[1::4]
            bgra[2::4] = row[0::4]
            bgra[3::4] = row[3::4]
            xor.extend(bgra)

        mask_stride = ((size + 31) // 32) * 4
        mask = bytes(mask_stride * size)
        blob = dib + bytes(xor) + mask

        dimension = 0 if size >= 256 else size
        directory += struct.pack("<BBBBHHII", dimension, dimension, 0, 0,
                                 1, 32, len(blob), offset)
        offset += len(blob)
        blobs.append(blob)

    path.write_bytes(header + bytes(directory) + b"".join(blobs))


def convert_png_to_ico(src: Path, dst: Path):
    rgba, width, height = _read_png_rgba(src)
    rgba, side = _crop_square(rgba, width, height)

    # Downscale once to 256, then derive the smaller sizes from that copy:
    # this keeps the one-time conversion fast for large source images.
    base = _resize_rgba(rgba, side, side, 256, 256) if side != 256 else rgba

    entries = []
    for size in ICO_SIZES:
        image = base if size == 256 else _resize_rgba(base, 256, 256, size, size)
        entries.append((size, image))

    _write_ico(dst, entries)


def has_pillow() -> bool:
    try:
        subprocess.run(
            ["py", "-c", "from PIL import Image"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return True
    except Exception:
        return False


def _icon_with_pillow(icon_file: Path) -> bool:
    """Fallback for PNG layouts the built-in decoder does not handle."""
    if not has_pillow():
        return False
    try:
        subprocess.run(
            [
                "py",
                "-c",
                (
                    "from PIL import Image; "
                    f"im=Image.open(r'{icon_file}').convert('RGBA'); "
                    f"im.save(r'{GENERATED_ICON_ICO}', format='ICO', "
                    "sizes=[(256,256),(128,128),(64,64),(48,48),(32,32),(16,16)])"
                ),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return True
    except Exception as ex:
        print_warn(f"Failed to convert image to ICO: {ex}")
        return False


def generate_icon_resource():
    icon_file = find_icon_file()
    if icon_file is None:
        print_info("No image file found in project root. Build will continue without icon resource.")
        return ""

    print_info(f"Icon file detected: {icon_file.name}")

    if icon_file.suffix.lower() == ".ico":
        try:
            if icon_file.resolve() != GENERATED_ICON_ICO.resolve():
                shutil.copyfile(icon_file, GENERATED_ICON_ICO)
        except Exception as ex:
            print_warn(f"Failed to copy ICO file: {ex}")
            return ""
    else:
        reuse = (GENERATED_ICON_ICO.exists()
                 and GENERATED_ICON_ICO.stat().st_mtime >= icon_file.stat().st_mtime)
        if reuse:
            print_info("Reusing GeneratedIcon.ico (source image unchanged).")
        else:
            converted = False
            try:
                convert_png_to_ico(icon_file, GENERATED_ICON_ICO)
                converted = True
            except Exception as ex:
                print_warn(f"Built-in icon conversion failed: {ex}")

            if not converted and not _icon_with_pillow(icon_file):
                print_warn("Could not convert the icon image. Install Pillow as a fallback:")
                print_warn("Install with: py -m pip install pillow")
                return ""

    GENERATED_ICON_RC.write_text(
        '#include "Core/ResourceIds.h"\n'
        'IDI_APP_ICON ICON "GeneratedIcon.ico"\n',
        encoding="utf-8"
    )

    # The object is normally rebuilt by make through the generic %.o: %.rc
    # rule. Precompiling here only saves one invocation; a missing windres at
    # codegen time must not drop the icon from GeneratedBuild.mk.
    if command_exists("windres"):
        try:
            subprocess.run(
                ["windres", str(GENERATED_ICON_RC), "-O", "coff", "-o", str(GENERATED_ICON_OBJ)],
                check=True
            )
        except Exception as ex:
            print_warn(f"Failed to compile GeneratedIcon.rc: {ex}")

    print_info("Generated icon resource successfully.")
    return normalize_make_path(GENERATED_ICON_OBJ)


def parse_previous_version():
    """Read FILEVERSION from an existing GeneratedVersion.rc, if any."""
    try:
        if GENERATED_VERSION_RC.exists():
            text = GENERATED_VERSION_RC.read_text(encoding="utf-8", errors="ignore")
            m = re.search(r"FILEVERSION\s+(\d+),(\d+),(\d+),(\d+)", text)
            if m:
                return tuple(int(g) for g in m.groups())
    except Exception:
        pass
    return None


def generate_version_resource():
    """Generate a VERSIONINFO resource so the running copy can be version-compared.

    Version layout: (YYYY, MMDD, HHMM, SS*100 + seq) - every component fits the
    16-bit FILEVERSION fields and the value is monotonic over time, so
    "greater than" means "newer build". Two builds in the same second would
    collide, so a 0-99 sequence counter is carried over from the previous
    GeneratedVersion.rc (written by the previous codegen run).
    """
    now = datetime.datetime.now()
    base = (
        now.year,
        now.month * 100 + now.day,
        now.hour * 100 + now.minute,
        now.second,
    )

    seq = 0
    prev = parse_previous_version()
    if prev and prev[:3] == base[:3]:
        seq = min(prev[3] % 100 + 1, 99)

    version = base[:3] + (base[3] * 100 + seq,)
    version_str = ".".join(str(v) for v in version)

    project_name = choose_project_name()
    original_name = choose_target_name()

    lines = []
    lines.append("// Auto-generated by BuildUI.py")
    lines.append("// Do not edit manually.")
    lines.append("")
    lines.append("1 VERSIONINFO")
    lines.append(f" FILEVERSION {version[0]},{version[1]},{version[2]},{version[3]}")
    lines.append(f" PRODUCTVERSION {version[0]},{version[1]},{version[2]},{version[3]}")
    lines.append(" FILEFLAGSMASK 0x3fL")
    lines.append(" FILEFLAGS 0x0L")
    lines.append(" FILEOS 0x40004L")
    lines.append(" FILETYPE 0x1L")
    lines.append(" FILESUBTYPE 0x0L")
    lines.append("BEGIN")
    lines.append("    BLOCK \"StringFileInfo\"")
    lines.append("    BEGIN")
    lines.append("        BLOCK \"040904b0\"")
    lines.append("        BEGIN")
    lines.append(f"            VALUE \"FileDescription\", \"{project_name}\"")
    lines.append(f"            VALUE \"FileVersion\", \"{version_str}\"")
    lines.append(f"            VALUE \"InternalName\", \"{original_name}\"")
    lines.append(f"            VALUE \"OriginalFilename\", \"{original_name}\"")
    lines.append(f"            VALUE \"ProductName\", \"{project_name}\"")
    lines.append(f"            VALUE \"ProductVersion\", \"{version_str}\"")
    lines.append("        END")
    lines.append("    END")
    lines.append("    BLOCK \"VarFileInfo\"")
    lines.append("    BEGIN")
    lines.append("        VALUE \"Translation\", 0x409, 1200")
    lines.append("    END")
    lines.append("END")
    lines.append("")

    GENERATED_VERSION_RC.write_text("\n".join(lines), encoding="utf-8")
    print_info(f"Generated version resource successfully ({version_str}).")
    return normalize_make_path(GENERATED_VERSION_OBJ)


def scan_root_cpp():
    result = []
    for file_name in ["main.cpp", "HostApp.cpp", "GeneratedModuleRegistry.cpp"]:
        p = ROOT / file_name
        if p.exists():
            result.append(p)
    return result


def scan_fixed_dirs_cpp():
    result = []
    for d in CORE_DIRS:
        dir_path = ROOT / d
        if not dir_path.exists():
            continue
        result.extend(sorted(dir_path.glob("*.cpp")))
    return result


def scan_ui_rc():
    result = []
    ui_dir = ROOT / "UI"
    if not ui_dir.exists():
        return result
    result.extend(sorted(ui_dir.glob("*.rc")))
    return result


def parse_module_ini(module_ini: Path):
    parser = configparser.ConfigParser()
    parser.optionxform = str
    parser.read(module_ini, encoding="utf-8")

    if "Module" not in parser:
        return None

    sec = parser["Module"]

    enabled = sec.get("Enabled", "1").strip()
    if enabled not in ("1", "true", "TRUE", "True", "yes", "YES", "Yes"):
        return None

    module_dir = module_ini.parent

    def split_list(value: str):
        value = value.strip()
        if not value:
            return []
        items = []
        for part in value.replace(";", ",").split(","):
            part = part.strip()
            if part:
                items.append(part)
        return items

    sources = [module_dir / x for x in split_list(sec.get("Sources", ""))]
    resources = [module_dir / x for x in split_list(sec.get("Resources", ""))]
    libraries = split_list(sec.get("Libraries", ""))

    module_name = sec.get("Name", module_dir.name).strip()
    module_class = sec.get("Class", "").strip()
    if not module_class:
        module_class = module_name + "Module"

    return {
        "name": module_name,
        "class": module_class,
        "dir": module_dir,
        "sources": sources,
        "resources": resources,
        "libraries": libraries,
        "ini": module_ini,
    }


def scan_modules(only_modules=None):
    cpp_files = []
    rc_files = []
    libraries = []
    parsed_modules = []

    if not MODULES_DIR.exists():
        return cpp_files, rc_files, libraries, parsed_modules

    for module_ini in sorted(MODULES_DIR.glob("*/*.module.ini")):
        parsed = parse_module_ini(module_ini)
        if not parsed:
            continue

        # Build-time flavour filter (BuildUI.py passes --modules). Modules that
        # are not selected are left out of the registry, the object list and
        # therefore the exe. This is a separate axis from the manifest
        # Enabled=0 flag, which stays a global opt-out.
        if only_modules is not None and parsed["name"] not in only_modules:
            print_info(f"Skipped module (not selected): {parsed['name']}")
            continue

        parsed_modules.append(parsed)
        print_info(f"Enabled module: {parsed['name']}")

        for src in parsed["sources"]:
            if src.exists():
                cpp_files.append(src)
            else:
                print_warn(f"Missing module source: {src}")

        for rc in parsed["resources"]:
            if rc.exists():
                rc_files.append(rc)
            else:
                print_warn(f"Missing module resource: {rc}")

        for lib in parsed["libraries"]:
            libraries.append(lib)

    return cpp_files, rc_files, libraries, parsed_modules


def write_text_if_changed(path: Path, content: str):
    try:
        if path.exists():
            old = path.read_text(encoding="utf-8", errors="ignore")
            if old == content:
                return
    except Exception:
        pass

    path.write_text(content, encoding="utf-8")


def generate_module_registry(parsed_modules):
    enabled_modules = [m for m in parsed_modules if m is not None]

    header_lines = []
    header_lines.append("// Auto-generated by BuildUI.py")
    header_lines.append("// Do not edit manually.")
    header_lines.append("")
    header_lines.append("#pragma once")
    header_lines.append("")
    header_lines.append("#include <set>")
    header_lines.append("#include <string>")
    header_lines.append("")
    header_lines.append("class ModuleManager;")
    header_lines.append("")
    header_lines.append("void RegisterGeneratedModules(ModuleManager& manager,")
    header_lines.append("    const std::set<std::wstring>& enabledModules);")
    header_lines.append("")

    cpp_lines = []
    cpp_lines.append("// Auto-generated by BuildUI.py")
    cpp_lines.append("// Do not edit manually.")
    cpp_lines.append("")
    cpp_lines.append('#include "GeneratedModuleRegistry.h"')
    cpp_lines.append("")
    cpp_lines.append('#include "Core/ModuleManager.h"')

    for mod in enabled_modules:
        include_path = f'Modules/{mod["name"]}/{mod["name"]}Module.h'
        cpp_lines.append(f'#include "{include_path}"')

    cpp_lines.append("")
    cpp_lines.append("void RegisterGeneratedModules(ModuleManager& manager, const std::set<std::wstring>& enabledModules) {")
    for mod in enabled_modules:
        guard = f'enabledModules.empty() || enabledModules.count(L"{mod["name"]}")'
        cpp_lines.append(f'    if ({guard})')
        cpp_lines.append(f'        manager.RegisterModule(new {mod["class"]}());')
    cpp_lines.append("}")
    cpp_lines.append("")

    write_text_if_changed(GENERATED_REGISTRY_H, "\n".join(header_lines))
    write_text_if_changed(GENERATED_REGISTRY_CPP, "\n".join(cpp_lines))

    print_info("Generated module registry successfully.")


def to_object_path(src: Path) -> str:
    rel = src.relative_to(ROOT)
    rel_no_suffix = rel.with_suffix("")
    return str(rel_no_suffix).replace("\\", "/") + ".o"


def make_lib_flags(libraries):
    result = []
    for lib in libraries:
        lib = lib.strip()
        if not lib:
            continue
        if lib.startswith("-l") or lib.startswith("-L") or lib.startswith("-Wl,"):
            result.append(lib)
        else:
            result.append(f"-l{lib}")
    return result


def write_generated_makefile(
    target_name: str,
    cpp_files,
    rc_files,
    icon_obj: str,
    version_obj: str,
    extra_libs
):
    cpp_objs = [to_object_path(p) for p in cpp_files]
    rc_objs = [to_object_path(p) for p in rc_files]

    lines = []
    lines.append("# Auto-generated by BuildUI.py")
    lines.append("")

    lines.append(f"TARGET = {target_name}")
    lines.append("")

    lines.append("CPP_OBJS = \\")
    if cpp_objs:
        for i, obj in enumerate(cpp_objs):
            suffix = " \\" if i != len(cpp_objs) - 1 else ""
            lines.append(f"\t{obj}{suffix}")
    else:
        lines.append("\t")
    lines.append("")

    lines.append("RC_OBJS = \\")
    if rc_objs:
        for i, obj in enumerate(rc_objs):
            suffix = " \\" if i != len(rc_objs) - 1 else ""
            lines.append(f"\t{obj}{suffix}")
    else:
        lines.append("\t")
    lines.append("")

    lines.append(f"GENERATED_ICON_OBJ = {icon_obj}")
    lines.append("")
    lines.append(f"GENERATED_VERSION_OBJ = {version_obj}")
    lines.append("")

    libs_text = " ".join(extra_libs)
    lines.append(f"LIBS_EXTRA = {libs_text}")
    lines.append("")

    GENERATED_MK.write_text("\n".join(lines), encoding="utf-8")


MERGE_LANG_OUT = ROOT / "Translation"


def parse_ini_sections(path: Path):
    """Parse a simple INI file into {section: {key: value}} preserving order."""
    result = {}
    current_section = ""
    with open(path, "r", encoding="utf-8-sig") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith(";") or line.startswith("#"):
                continue
            if line.startswith("[") and line.endswith("]"):
                current_section = line[1:-1].strip()
                if current_section not in result:
                    result[current_section] = {}
            elif "=" in line and current_section:
                key, _, val = line.partition("=")
                result[current_section][key.strip()] = val.strip()
    return result


def merge_ini_files(sources: list[Path]) -> str:
    """Merge multiple INI files into one string (same section keys merge together)."""
    merged = {}
    for src in sources:
        sections = parse_ini_sections(src)
        for sec, kv in sections.items():
            if sec not in merged:
                merged[sec] = {}
            merged[sec].update(kv)

    lines = []
    for sec, kv in merged.items():
        lines.append(f"[{sec}]")
        for k, v in kv.items():
            lines.append(f"{k}={v}")
        lines.append("")
    return "\n".join(lines)


def generate_translation():
    src_dir = ROOT / "Translation" / "_src"
    if not src_dir.exists():
        return

    for src_file in sorted(src_dir.glob("*.ini")):
        lang = src_file.stem
        sources = [src_file]

        # Pet/lang/ overrides
        pet_lang = ROOT / "Pet" / "lang" / src_file.name
        if pet_lang.exists():
            sources.append(pet_lang)

        # Modules/lang/ overrides
        for module_ini in sorted((ROOT / "Modules").glob("*/*.module.ini")):
            mod_lang = module_ini.parent / "lang" / src_file.name
            if mod_lang.exists():
                sources.append(mod_lang)

        content = merge_ini_files(sources)
        out_path = MERGE_LANG_OUT / src_file.name
        try:
            if out_path.exists():
                old = out_path.read_bytes()
                new_bytes = content.encode("utf-16-le")
                bom = b"\xff\xfe"
                if old == bom + new_bytes:
                    continue
            out_path.write_bytes(b"\xff\xfe" + content.encode("utf-16-le"))
        except Exception:
            pass
        print_info(f"Generated Translation/{src_file.name} ({len(sources)} source(s))")


def list_modules_info():
    """Machine-readable module inventory for the UI and scripts.

    Unlike scan_modules() this keeps disabled manifests in the list so the tool
    can show them greyed out instead of silently dropping them.
    """
    items = []
    if not MODULES_DIR.exists():
        return items

    for module_ini in sorted(MODULES_DIR.glob("*/*.module.ini")):
        parser = configparser.ConfigParser()
        parser.optionxform = str
        try:
            parser.read(module_ini, encoding="utf-8")
        except configparser.Error:
            continue
        if "Module" not in parser:
            continue

        sec = parser["Module"]
        enabled_raw = sec.get("Enabled", "1").strip().lower()
        sources = [x.strip() for x in sec.get("Sources", "").replace(";", ",").split(",") if x.strip()]

        items.append({
            "name": sec.get("Name", module_ini.parent.name).strip(),
            "dir": module_ini.parent.name,
            "class": sec.get("Class", "").strip(),
            "enabled": enabled_raw not in ("0", "false", "no", "off"),
            "sources": sources,
            "config": sec.get("ConfigFile", "").strip(),
        })

    return items


def normalize_module_filter(value: str):
    names = [x.strip() for x in value.replace(";", ",").split(",")]
    return {x for x in names if x}


def run_codegen(argv):
    """Regenerate every Generated* file (old `py Build.py` behaviour)."""
    only_modules = None
    if "--modules" in argv:
        idx = argv.index("--modules")
        value = argv[idx + 1] if idx + 1 < len(argv) else ""
        only_modules = normalize_module_filter(value)
        if not only_modules:
            print_warn("--modules was empty; building every enabled module.")
            only_modules = None

    print_info(f"Project root: {ROOT}")

    if not (ROOT / "Makefile").exists():
        print_error("Makefile not found.")
        return 1

    if only_modules is not None:
        print_info("Module filter: " + ", ".join(sorted(only_modules)))

    mod_cpp, mod_rc, mod_libs, parsed_modules = scan_modules(only_modules)
    generate_module_registry(parsed_modules)
    generate_translation()

    cpp_files = []
    cpp_files.extend(scan_root_cpp())
    cpp_files.extend(scan_fixed_dirs_cpp())

    rc_files = []
    rc_files.extend(scan_ui_rc())

    cpp_files.extend(mod_cpp)
    rc_files.extend(mod_rc)

    unique_cpp = []
    seen_cpp = set()
    for p in cpp_files:
        key = str(p.resolve()).lower()
        if key not in seen_cpp:
            seen_cpp.add(key)
            unique_cpp.append(p)

    unique_rc = []
    seen_rc = set()
    for p in rc_files:
        key = str(p.resolve()).lower()
        if key not in seen_rc:
            seen_rc.add(key)
            unique_rc.append(p)

    extra_libs = make_lib_flags(mod_libs)
    icon_obj = generate_icon_resource()
    version_obj = generate_version_resource()
    target_name = choose_target_name()

    print_info("Generating GeneratedBuild.mk ...")
    write_generated_makefile(
        target_name=target_name,
        cpp_files=unique_cpp,
        rc_files=unique_rc,
        icon_obj=icon_obj,
        version_obj=version_obj,
        extra_libs=extra_libs
    )

    print_info(f"Project name: {choose_project_name()}")
    print_info(f"Target: {target_name}")
    print_info(f"CPP files: {len(unique_cpp)}")
    print_info(f"RC files: {len(unique_rc)}")
    print_info("Generated GeneratedBuild.mk successfully.")
    return 0


# ===========================================================================
# Config, environment helpers, headless commands
# ===========================================================================

def console_python():
    """Interpreter to use for child processes.

    The tool normally runs under pythonw.exe so no console window appears.
    pythonw has no usable console handles, so children use its console twin.
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


def load_config():
    config = dict(DEFAULT_CONFIG)

    if CONFIG_FILE.exists():
        try:
            data = json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
            if isinstance(data, dict):
                config.update({k: data[k] for k in DEFAULT_CONFIG if k in data})
        except Exception:  # noqa: BLE001
            pass
    elif LEGACY_STATE_FILE.exists():
        # One-time migration from the old .agent state file.
        try:
            data = json.loads(LEGACY_STATE_FILE.read_text(encoding="utf-8"))
            if isinstance(data, dict):
                for key in ("mode", "carrier", "flavor", "clean", "all", "modules"):
                    if key in data:
                        config[key] = data[key]
        except Exception:  # noqa: BLE001
            pass

    if config.get("mode") not in ("release", "debug"):
        config["mode"] = "release"
    if config.get("carrier") not in [c[0] for c in CARRIERS]:
        config["carrier"] = "pet"
    if not isinstance(config.get("modules"), list):
        config["modules"] = []

    return config


def save_config(config):
    CONFIG_DIR.mkdir(parents=True, exist_ok=True)
    try:
        CONFIG_FILE.write_text(
            json.dumps(config, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
    except Exception:  # noqa: BLE001
        pass


def arg_value(argv, flag, default=""):
    if flag in argv:
        idx = argv.index(flag)
        if idx + 1 < len(argv):
            return argv[idx + 1]
    return default


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


def make_env(make_path):
    env = os.environ.copy()
    env["PATH"] = str(make_path.parent) + os.pathsep + env.get("PATH", "")
    return env


def build_commands(config, make_path, mode, clean, modules):
    """[(argv, env, label)] for one build in the requested mode."""
    env = make_env(make_path)
    generate = [console_python(), str(ROOT / "BuildUI.py"), "--generate"]
    if modules is not None:
        generate += ["--modules", ",".join(modules)]

    make = [str(make_path), "SHELL=cmd.exe"]
    make_build = make + (["CXXFLAGS_EXTRA=-DDEBUG_CONSOLE=1"] if mode == "debug" else [])

    commands = []
    if clean:
        commands.append((make + ["clean"], env))
    commands.append((generate, env))
    commands.append((make_build, env))
    return commands


def console_write(text):
    """Write to stdout without crashing on characters the console cannot encode."""
    stream = sys.stdout
    encoding = getattr(stream, "encoding", None) or "utf-8"
    try:
        stream.write(text)
    except UnicodeEncodeError:
        stream.write(text.encode(encoding, "replace").decode(encoding, "replace"))
    stream.flush()


def run_commands_sync(commands):
    for cmd, env in commands:
        console_write("\n$ " + " ".join(cmd) + "\n")
        proc = subprocess.Popen(
            cmd, cwd=str(ROOT), env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, encoding="utf-8", errors="replace", bufsize=1,
        )
        assert proc.stdout is not None
        for line in proc.stdout:
            console_write(line.rstrip("\r\n") + "\n")
        proc.wait()
        if proc.returncode != 0:
            print_error(f"Command failed with exit code {proc.returncode}.")
            return proc.returncode
    return 0


def cli_build(argv):
    config = load_config()
    mode = arg_value(argv, "--mode", config.get("mode", "release"))
    if mode not in ("release", "debug"):
        print_error("--mode must be release or debug.")
        return 2

    make_path = resolve_make_path()
    if make_path is None:
        print_error("mingw32-make.exe not found. Write its full path to MakePath.txt.")
        return 2

    modules_arg = arg_value(argv, "--modules", "")
    if modules_arg:
        modules = sorted(normalize_module_filter(modules_arg))
    elif config.get("all", True):
        modules = None
    else:
        modules = config.get("modules") or []

    clean = ("--clean" in argv or mode != config.get("last_built_mode", ""))

    print_info(f"Build mode: {mode}" + (" (clean first)" if clean else ""))
    commands = build_commands(config, make_path, mode, clean, modules)

    if "--test" in argv:
        commands.append(([str(make_path), "SHELL=cmd.exe", "test"], make_env(make_path)))

    rc = run_commands_sync(commands)
    if rc == 0:
        config["last_built_mode"] = mode
        save_config(config)
    return rc


def cli_clear():
    make_path = resolve_make_path()
    if make_path is None:
        print_error("mingw32-make.exe not found. Write its full path to MakePath.txt.")
        return 2
    return run_commands_sync([([str(make_path), "SHELL=cmd.exe", "distclean"],
                               make_env(make_path))])


def cli_test():
    make_path = resolve_make_path()
    if make_path is None:
        print_error("mingw32-make.exe not found. Write its full path to MakePath.txt.")
        return 2
    return run_commands_sync([([str(make_path), "SHELL=cmd.exe", "test"],
                               make_env(make_path))])


def run_cli(argv):
    """Return an exit code for a headless command, or None to open the UI."""
    if "--list-modules" in argv:
        print(json.dumps(list_modules_info(), ensure_ascii=False, indent=2))
        return 0
    if "--generate" in argv:
        return run_codegen(argv)
    if "--build" in argv:
        return cli_build(argv)
    if "--test" in argv:
        return cli_test()
    if "--clear" in argv:
        return cli_clear()
    return None


# ===========================================================================
# UI
# ===========================================================================

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


class BuildUI:
    def __init__(self, root):
        self.root = root
        self.modules = []
        self.vars = {}
        self.log_queue = queue.Queue()
        self.worker = None
        self.last_returncode = 0
        self.config = load_config()

        self.mode_var = tk.StringVar(value=self.config.get("mode", "release"))
        self.flavor_var = tk.StringVar(value=self.config.get("flavor") or "Default")
        self.carrier_var = tk.StringVar(value=self.config.get("carrier", "pet"))
        self.all_var = tk.BooleanVar(value=bool(self.config.get("all", True)))
        self.clean_var = tk.BooleanVar(value=bool(self.config.get("clean", True)))
        self._state_modules = list(self.config.get("modules") or [])

        self._build_widgets()
        self._reload_modules()
        self.root.after(100, self._drain_log)

    # -- layout -------------------------------------------------------------

    def _build_widgets(self):
        self.root.title("Build Tool - 模組挑選器與建置模式")
        self.root.geometry("940x660")
        self.root.minsize(820, 580)

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

        # --- mode / flavour / carrier -------------------------------------
        side = ttk.Frame(top, padding=(12, 0, 0, 0))
        side.pack(side="right", fill="y")

        mode_box = ttk.LabelFrame(side, text="建置模式（Config/BuildUI.json）", padding=8)
        mode_box.pack(fill="x")
        for key, label, hint in MODES:
            ttk.Radiobutton(mode_box, text=label, value=key,
                            variable=self.mode_var,
                            command=self._on_mode_change).pack(anchor="w")
            ttk.Label(mode_box, text="   " + hint, foreground="#666").pack(anchor="w")
        self.mode_hint = ttk.Label(mode_box, text="", foreground="#0a5")
        self.mode_hint.pack(anchor="w", pady=(6, 0))
        self._update_mode_hint()

        flavor_box = ttk.LabelFrame(side, text="Flavor", padding=8)
        flavor_box.pack(fill="x", pady=(10, 0))
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
        self.test_button = ttk.Button(actions, text="執行測試", width=10, command=self.on_test)
        self.test_button.pack(side="left")
        self.clear_button = ttk.Button(actions, text="清理", width=8, command=self.on_clear)
        self.clear_button.pack(side="left", padx=6)
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

    def _update_mode_hint(self):
        mode = self.mode_var.get()
        if mode == "debug":
            self.mode_hint.configure(text="AI debug：Console + DBG() + 命名管道")
        else:
            self.mode_hint.configure(text="一般版本：不開 Console")

    def _on_mode_change(self):
        self._update_mode_hint()
        self.config["mode"] = self.mode_var.get()
        self._save_config()

    # -- module list --------------------------------------------------------

    def _reload_modules(self):
        try:
            self.modules = self._load_modules()
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

    def _load_modules(self):
        """Module inventory straight from the merged codegen (no subprocess)."""
        return list_modules_info()

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

    # -- config -------------------------------------------------------------

    def _apply_state_flags(self):
        if self.all_var.get():
            return
        wanted = set(self._state_modules)
        if not wanted:
            return
        self.all_var.set(False)
        for name, var in self.vars.items():
            var.set(name in wanted)
        self._sync_all_state()

    def _save_config(self):
        self.config.update({
            "mode": self.mode_var.get(),
            "flavor": self.flavor_var.get().strip(),
            "carrier": self.carrier_var.get(),
            "clean": bool(self.clean_var.get()),
            "all": bool(self.all_var.get()),
            "modules": [] if self.all_var.get() else self.selected_modules(),
        })
        save_config(self.config)

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
        self._save_config()
        self.status_var.set(f"Flavor 已輸出：{flavor}")

    # -- build --------------------------------------------------------------

    def _set_busy(self, busy):
        state = ["disabled"] if busy else ["!disabled"]
        for widget in (self.build_button, self.run_button,
                       self.test_button, self.clear_button):
            widget.state(state)

    def _start_commands(self, commands, status, success_text, failure_text):
        if self.worker and self.worker.is_alive():
            return

        self._clear_log()
        self._set_busy(True)
        self.status_var.set(status)

        self.worker = threading.Thread(
            target=self._run_commands,
            args=(commands, success_text, failure_text),
            daemon=True,
        )
        self.worker.start()

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

        mode = self.mode_var.get()
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

        build_modules = None if self.all_var.get() else modules
        self._write_flavor(flavor, self.carrier_var.get(), modules)
        self._write_launcher(flavor)
        self._save_config()

        # Switching between Release and Debug without a clean would link
        # objects compiled with the other flag set, so force it.
        clean = bool(self.clean_var.get())
        if mode != self.config.get("last_built_mode", ""):
            clean = True
            self._log_line(f"[build] 模式變更為 {mode}，自動加上 make clean。")

        commands = build_commands(self.config, make, mode, clean, build_modules)
        self.config["last_built_mode"] = mode
        self._save_config()

        self._log_line(f"[build] 模式 = {mode}  carrier = {self.carrier_var.get()}")
        self._log_line("[build] 模組 = " + (", ".join(modules) if not self.all_var.get() else "全部"))

        self._start_commands(
            commands,
            "建置中…",
            "\n[build] 完成。可按「執行」測試目前 Flavor。",
            "\n[build] 失敗，請看上面的輸出。",
        )

    def on_test(self):
        if self.worker and self.worker.is_alive():
            return
        make = resolve_make_path()
        if make is None:
            messagebox.showerror("Build Tool", "找不到 mingw32-make.exe。請先在 MakePath.txt 寫入完整路徑。")
            return
        commands = [([str(make), "SHELL=cmd.exe", "test"], make_env(make))]
        self._start_commands(
            commands,
            "測試中…",
            "\n[test] 完成，全部測試通過。",
            "\n[test] 有測試失敗，請看上面的輸出。",
        )

    def on_clear(self):
        if self.worker and self.worker.is_alive():
            return
        make = resolve_make_path()
        if make is None:
            messagebox.showerror("Build Tool", "找不到 mingw32-make.exe。請先在 MakePath.txt 寫入完整路徑。")
            return
        if not messagebox.askyesno(
            "Build Tool",
            "清理會刪除所有物件檔、Generated* 與合併後的翻譯檔（make distclean）。\n要繼續嗎？",
        ):
            return
        commands = [([str(make), "SHELL=cmd.exe", "distclean"], make_env(make))]
        self._start_commands(
            commands,
            "清理中…",
            "\n[clear] 清理完成，下次建置會重新產生所有檔案。",
            "\n[clear] 清理失敗，請看上面的輸出。",
        )

    def _run_commands(self, commands, success_text, failure_text):
        self.last_returncode = 0
        for cmd, env in commands:
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

        self.log_queue.put(("done", (self.last_returncode, success_text, failure_text)))

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
                    code, success_text, failure_text = payload
                    self._set_busy(False)
                    if code == 0:
                        self.status_var.set("完成")
                        self._log_line(success_text)
                    else:
                        self.status_var.set("失敗")
                        self._log_line(failure_text)
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

        self._save_config()
        args = [str(exe), "--flavor", flavor]
        self._log_line("[run] " + " ".join(args))
        try:
            subprocess.Popen(args, cwd=str(ROOT), close_fds=True,
                             creationflags=NO_WINDOW)
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror("Build Tool", f"無法啟動：{exc}")

    # -- close --------------------------------------------------------------

    def on_close(self):
        self._save_config()
        self.root.destroy()


def report_fatal(message):
    """Show a startup failure without relying on tkinter or a console."""
    try:
        import ctypes
        ctypes.windll.user32.MessageBoxW(None, message, "Build Tool", 0x10)
    except Exception:  # noqa: BLE001
        pass


def main():
    cli_code = run_cli(sys.argv[1:])
    if cli_code is not None:
        return cli_code

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
