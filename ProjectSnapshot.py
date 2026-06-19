import argparse
import configparser
import datetime
import os
from pathlib import Path
from typing import List, Tuple

ROOT = Path(__file__).resolve().parent
OUTPUT_FILE = ROOT / "Project_Context_Summary.txt"

TEXT_EXTENSIONS = {
    ".txt", ".ini", ".mk", ".py", ".bat", ".h", ".hpp", ".c", ".cpp", ".cc", ".rc"
}

IMPORTANT_FILES = [
    "main.cpp",
    "HostApp.h",
    "HostApp.cpp",
    "Makefile",
    "Build.py",
    "Build.bat",
    "ProjectName.txt",
    "TargetName.txt",
    "HostHint.txt",
    "MakePath.txt",
    "GeneratedBuild.mk",
]

IMPORTANT_CORE_FILES = [
    "Core/IHostContext.h",
    "Core/IFeatureModule.h",
    "Core/IDropActionProvider.h",
    "Core/DropTypes.h",
    "Core/ConfigTypes.h",
    "Core/ModuleManager.h",
    "Core/ModuleManager.cpp",
    "Core/ConfigManager.h",
    "Core/ConfigManager.cpp",
    "Core/Logger.h",
    "Core/Logger.cpp",
]

IMPORTANT_UI_FILES = [
    "UI/MainWindow.h",
    "UI/MainWindow.cpp",
    "UI/SettingsWindow.h",
    "UI/SettingsWindow.cpp",
    "UI/resource.h",
    "UI/Project.rc",
]

IGNORE_DIR_NAMES = {
    ".git",
    ".vs",
    ".vscode",
    "__pycache__",
    "x64",
    "Debug",
    "Release",
    "Export_CleanProject",
}

IGNORE_FILE_NAMES = {
    "GeneratedIcon.ico",
    "GeneratedIcon.rc",
    "GeneratedIcon.o",
    "Build.log",
}

IGNORE_SUFFIXES = {
    ".o",
    ".obj",
    ".d",
    ".exe",
    ".dll",
    ".pdb",
    ".ilk",
    ".lib",
    ".exp",
    ".pyc",
}

SECTION_LINE = "=" * 80
SUBSECTION_LINE = "-" * 80


def read_text_file(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        try:
            return path.read_text(encoding="cp950", errors="ignore")
        except Exception:
            return ""


def read_first_line(path: Path) -> str:
    text = read_text_file(path)
    if not text:
        return ""
    lines = text.splitlines()
    return lines[0].strip() if lines else ""


def is_ignored(path: Path) -> bool:
    parts = set(path.parts)

    for part in parts:
        if part in IGNORE_DIR_NAMES:
            return True

    if path.name in IGNORE_FILE_NAMES:
        return True

    if path.suffix.lower() in IGNORE_SUFFIXES:
        return True

    return False


def should_include_in_tree(path: Path) -> bool:
    return not is_ignored(path)


def get_project_name() -> str:
    p = ROOT / "ProjectName.txt"
    if p.exists():
        text = read_first_line(p)
        if text:
            return text
    return ROOT.name


def get_target_name() -> str:
    p = ROOT / "TargetName.txt"
    if p.exists():
        text = read_first_line(p)
        if text:
            return text if text.lower().endswith(".exe") else text + ".exe"
    return get_project_name() + ".exe"


def get_host_hint() -> str:
    p = ROOT / "HostHint.txt"
    if p.exists():
        text = read_first_line(p)
        if text:
            return text
    return "N/A"


def safe_rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except Exception:
        return str(path).replace("\\", "/")


def collect_tree_lines(base: Path, prefix: str = "") -> List[str]:
    lines = []

    try:
        entries = sorted(base.iterdir(), key=lambda p: (p.is_file(), p.name.lower()))
    except Exception:
        return lines

    filtered = [p for p in entries if should_include_in_tree(p)]

    for i, path in enumerate(filtered):
        is_last = i == len(filtered) - 1
        branch = "└─ " if is_last else "├─ "
        lines.append(prefix + branch + path.name)

        if path.is_dir():
            extension = "   " if is_last else "│  "
            lines.extend(collect_tree_lines(path, prefix + extension))

    return lines


def write_section(lines: List[str], title: str):
    lines.append(SECTION_LINE)
    lines.append(title)
    lines.append(SECTION_LINE)


def write_subsection(lines: List[str], title: str):
    lines.append(SUBSECTION_LINE)
    lines.append(title)
    lines.append(SUBSECTION_LINE)


def find_module_ini_files() -> List[Path]:
    modules_dir = ROOT / "Modules"
    if not modules_dir.exists():
        return []
    return sorted(modules_dir.glob("*/*.module.ini"))


def parse_module_ini(path: Path):
    parser = configparser.ConfigParser()
    parser.optionxform = str
    try:
        parser.read(path, encoding="utf-8")
    except Exception:
        return None

    if "Module" not in parser:
        return None

    sec = parser["Module"]
    return {
        "path": safe_rel(path),
        "name": sec.get("Name", path.parent.name),
        "enabled": sec.get("Enabled", "1"),
        "sources": sec.get("Sources", ""),
        "headers": sec.get("Headers", ""),
        "resources": sec.get("Resources", ""),
        "libraries": sec.get("Libraries", ""),
        "config_file": sec.get("ConfigFile", ""),
        "log_file": sec.get("LogFile", ""),
    }


def extract_signatures(text: str) -> List[str]:
    results = []
    for line in text.splitlines():
        s = line.strip()

        if not s:
            continue

        if s.startswith("//"):
            continue

        if s.startswith("class ") or s.startswith("struct ") or s.startswith("enum class "):
            results.append(s)
            continue

        if s.startswith("enum "):
            results.append(s)
            continue

        if "(" in s and ")" in s and (
            s.endswith(";") or s.endswith("{") or s.endswith("override;") or s.endswith("const = 0;")
        ):
            results.append(s)

    return results[:200]


def summarize_text_file(path: Path) -> List[str]:
    lines = []
    text = read_text_file(path)
    if not text:
        lines.append("[Unable to read file or file is empty]")
        return lines

    lines.append(f"Path: {safe_rel(path)}")
    lines.append(f"Size: {path.stat().st_size} bytes")
    lines.append("")

    if path.suffix.lower() in {".h", ".hpp", ".c", ".cpp", ".cc"}:
        lines.append("Extracted signatures:")
        sigs = extract_signatures(text)
        if sigs:
            for sig in sigs:
                lines.append(f"  {sig}")
        else:
            lines.append("  [No signatures extracted]")
    elif path.suffix.lower() in {".ini"}:
        lines.append("INI content:")
        for line in text.splitlines():
            lines.append(f"  {line}")
    else:
        preview_lines = text.splitlines()[:80]
        lines.append("Preview:")
        for line in preview_lines:
            lines.append(f"  {line}")

    return lines


def append_full_file(lines: List[str], path: Path):
    lines.append(f"FILE: {safe_rel(path)}")
    lines.append(SUBSECTION_LINE)
    text = read_text_file(path)
    if not text:
        lines.append("[Unable to read file or file is empty]")
    else:
        lines.append(text.rstrip())
    lines.append("")


def collect_existing_files(paths: List[str]) -> List[Path]:
    result = []
    for rel in paths:
        p = ROOT / rel
        if p.exists() and p.is_file():
            result.append(p)
    return result


def collect_module_related_files() -> List[Path]:
    files = []

    for ini in find_module_ini_files():
        files.append(ini)

        module_dir = ini.parent
        try:
            parser = configparser.ConfigParser()
            parser.optionxform = str
            parser.read(ini, encoding="utf-8")
            if "Module" not in parser:
                continue
            sec = parser["Module"]

            def split_list(value: str):
                items = []
                for part in value.replace(";", ",").split(","):
                    part = part.strip()
                    if part:
                        items.append(part)
                return items

            for name in split_list(sec.get("Sources", "")):
                p = module_dir / name
                if p.exists():
                    files.append(p)

            for name in split_list(sec.get("Headers", "")):
                p = module_dir / name
                if p.exists():
                    files.append(p)

            for name in split_list(sec.get("Resources", "")):
                p = module_dir / name
                if p.exists():
                    files.append(p)
        except Exception:
            continue

    unique = []
    seen = set()
    for p in files:
        key = str(p.resolve()).lower()
        if key not in seen:
            seen.add(key)
            unique.append(p)

    return unique


def list_files_in_dir(dir_name: str) -> List[str]:
    d = ROOT / dir_name
    if not d.exists():
        return []
    result = []
    for p in sorted(d.rglob("*")):
        if p.is_file() and not is_ignored(p):
            result.append(safe_rel(p))
    return result


def generate_summary() -> str:
    lines: List[str] = []

    write_section(lines, "PROJECT CONTEXT SUMMARY")

    lines.append(f"Generated At : {datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append(f"Project Root : {ROOT}")
    lines.append(f"Project Name : {get_project_name()}")
    lines.append(f"Target Name  : {get_target_name()}")
    lines.append(f"Host Hint    : {get_host_hint()}")
    lines.append("")

    write_section(lines, "DIRECTORY TREE")
    lines.append(ROOT.name)
    lines.extend(collect_tree_lines(ROOT))
    lines.append("")

    write_section(lines, "BUILD SUMMARY")
    lines.append(f"Makefile Exists : {(ROOT / 'Makefile').exists()}")
    lines.append(f"Build.py Exists : {(ROOT / 'Build.py').exists()}")
    lines.append(f"Build.bat Exists: {(ROOT / 'Build.bat').exists()}")
    lines.append(f"GeneratedBuild.mk Exists: {(ROOT / 'GeneratedBuild.mk').exists()}")
    lines.append("")
    lines.append("Build Naming Rules:")
    lines.append(f"  ProjectName.txt -> {get_project_name()}")
    lines.append(f"  TargetName.txt  -> {get_target_name()}")
    lines.append("")
    lines.append("Detected Module Metadata Files:")
    mod_inis = find_module_ini_files()
    if mod_inis:
        for p in mod_inis:
            lines.append(f"  {safe_rel(p)}")
    else:
        lines.append("  [No module ini files found]")
    lines.append("")

    write_section(lines, "MODULE SUMMARY")
    parsed_modules = [parse_module_ini(p) for p in find_module_ini_files()]
    parsed_modules = [m for m in parsed_modules if m is not None]

    if parsed_modules:
        for mod in parsed_modules:
            write_subsection(lines, f"Module: {mod['name']}")
            lines.append(f"Metadata   : {mod['path']}")
            lines.append(f"Enabled    : {mod['enabled']}")
            lines.append(f"Sources    : {mod['sources']}")
            lines.append(f"Headers    : {mod['headers']}")
            lines.append(f"Resources  : {mod['resources']}")
            lines.append(f"Libraries  : {mod['libraries']}")
            lines.append(f"ConfigFile : {mod['config_file']}")
            lines.append(f"LogFile    : {mod['log_file']}")
            lines.append("")
    else:
        lines.append("[No modules found]")
        lines.append("")

    write_section(lines, "IMPORTANT FILE SUMMARY")

    important_files = []
    important_files.extend(collect_existing_files(IMPORTANT_FILES))
    important_files.extend(collect_existing_files(IMPORTANT_CORE_FILES))
    important_files.extend(collect_existing_files(IMPORTANT_UI_FILES))
    important_files.extend(collect_module_related_files())

    unique_files = []
    seen = set()
    for p in important_files:
        key = str(p.resolve()).lower()
        if key not in seen:
            seen.add(key)
            unique_files.append(p)

    for path in unique_files:
        write_subsection(lines, safe_rel(path))
        lines.extend(summarize_text_file(path))
        lines.append("")

    write_section(lines, "CONFIG FILES")
    config_files = list_files_in_dir("Config")
    if config_files:
        for path in config_files:
            lines.append(path)
    else:
        lines.append("[No config files found]")
    lines.append("")

    write_section(lines, "LOG FILES")
    log_files = list_files_in_dir("Log")
    if log_files:
        for path in log_files:
            lines.append(path)
    else:
        lines.append("[No log files found]")
    lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def generate_full() -> str:
    lines: List[str] = []
    lines.append(generate_summary().rstrip())
    lines.append("")
    write_section(lines, "FULL IMPORTANT FILE CONTENT")

    files = []
    files.extend(collect_existing_files(IMPORTANT_FILES))
    files.extend(collect_existing_files(IMPORTANT_CORE_FILES))
    files.extend(collect_existing_files(IMPORTANT_UI_FILES))
    files.extend(collect_module_related_files())

    unique_files = []
    seen = set()
    for p in files:
        key = str(p.resolve()).lower()
        if key not in seen:
            seen.add(key)
            unique_files.append(p)

    for path in unique_files:
        append_full_file(lines, path)

    return "\n".join(lines).rstrip() + "\n"


def main():
    parser = argparse.ArgumentParser(description="Generate project context snapshot.")
    parser.add_argument(
        "--mode",
        choices=["summary", "full"],
        default="full",
        help="Snapshot mode: summary or full"
    )
    parser.add_argument(
        "--summary",
        action="store_true",
        help="Shortcut for --mode summary"
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="Shortcut for --mode full"
    )
    args = parser.parse_args()

    mode = args.mode
    if args.summary:
        mode = "summary"
    if args.full:
        mode = "full"

    if mode == "full":
        content = generate_full()
    else:
        content = generate_summary()

    OUTPUT_FILE.write_text(content, encoding="utf-8")
    print(f"[INFO] Snapshot generated: {OUTPUT_FILE}")
    print(f"[INFO] Mode: {mode}")


if __name__ == "__main__":
    main()