import configparser
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
GENERATED_MK = ROOT / "GeneratedBuild.mk"
GENERATED_ICON_ICO = ROOT / "GeneratedIcon.ico"
GENERATED_ICON_RC = ROOT / "GeneratedIcon.rc"
GENERATED_ICON_OBJ = ROOT / "GeneratedIcon.o"
GENERATED_REGISTRY_H = ROOT / "GeneratedModuleRegistry.h"
GENERATED_REGISTRY_CPP = ROOT / "GeneratedModuleRegistry.cpp"

ICON_EXTENSIONS = [".png", ".ico", ".jpg", ".jpeg", ".bmp"]
IGNORED_ICON_FILES = {
    "GeneratedIcon.ico",
    "GeneratedIcon.rc",
    "GeneratedIcon.o",
    "Pet.png",
}

CORE_DIRS = ["Core", "Services", "UI", "Pet"]
MODULES_DIR = ROOT / "Modules"


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


def check_python_and_pillow():
    if not command_exists("py"):
        return False, False

    try:
        subprocess.run(
            ["py", "-c", "print('ok')"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    except Exception:
        return False, False

    try:
        subprocess.run(
            ["py", "-c", "from PIL import Image"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return True, True
    except Exception:
        return True, False


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
        has_py, has_pillow = check_python_and_pillow()
        if not has_py:
            print_warn("Python launcher 'py' not found. Skip icon generation.")
            return ""

        if not has_pillow:
            print_warn("Pillow is not installed. Skip icon generation.")
            print_warn("Install with: py -m pip install pillow")
            return ""

        try:
            subprocess.run(
                [
                    "py",
                    "-c",
                    (
                        "from PIL import Image; "
                        f"im=Image.open(r'{str(icon_file)}').convert('RGBA'); "
                        f"im.save(r'{str(GENERATED_ICON_ICO)}', format='ICO', "
                        "sizes=[(256,256),(128,128),(64,64),(48,48),(32,32),(16,16)])"
                    ),
                ],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except Exception as ex:
            print_warn(f"Failed to convert image to ICO: {ex}")
            return ""

    GENERATED_ICON_RC.write_text(
        'IDI_APP_ICON ICON "GeneratedIcon.ico"\n',
        encoding="utf-8"
    )

    if not command_exists("windres"):
        print_warn("windres not found. Skip icon object generation.")
        return ""

    try:
        subprocess.run(
            ["windres", str(GENERATED_ICON_RC), "-O", "coff", "-o", str(GENERATED_ICON_OBJ)],
            check=True
        )
    except Exception as ex:
        print_warn(f"Failed to compile GeneratedIcon.rc: {ex}")
        return ""

    print_info("Generated icon resource successfully.")
    return normalize_make_path(GENERATED_ICON_OBJ)


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


def scan_modules():
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
    header_lines.append("// Auto-generated by Build.py")
    header_lines.append("// Do not edit manually.")
    header_lines.append("")
    header_lines.append("#pragma once")
    header_lines.append("")
    header_lines.append("class ModuleManager;")
    header_lines.append("")
    header_lines.append("void RegisterGeneratedModules(ModuleManager& manager);")
    header_lines.append("")

    cpp_lines = []
    cpp_lines.append("// Auto-generated by Build.py")
    cpp_lines.append("// Do not edit manually.")
    cpp_lines.append("")
    cpp_lines.append('#include "GeneratedModuleRegistry.h"')
    cpp_lines.append("")
    cpp_lines.append('#include "Core/ModuleManager.h"')

    for mod in enabled_modules:
        include_path = f'Modules/{mod["name"]}/{mod["name"]}Module.h'
        cpp_lines.append(f'#include "{include_path}"')

    cpp_lines.append("")
    cpp_lines.append("void RegisterGeneratedModules(ModuleManager& manager) {")
    for mod in enabled_modules:
        cpp_lines.append(f'    manager.RegisterModule(new {mod["class"]}());')
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
    extra_libs
):
    cpp_objs = [to_object_path(p) for p in cpp_files]
    rc_objs = [to_object_path(p) for p in rc_files]

    lines = []
    lines.append("# Auto-generated by Build.py")
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


def main():
    print_info(f"Project root: {ROOT}")

    if not (ROOT / "Makefile").exists():
        print_error("Makefile not found.")
        return 1

    mod_cpp, mod_rc, mod_libs, parsed_modules = scan_modules()
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
    target_name = choose_target_name()

    print_info("Generating GeneratedBuild.mk ...")
    write_generated_makefile(
        target_name=target_name,
        cpp_files=unique_cpp,
        rc_files=unique_rc,
        icon_obj=icon_obj,
        extra_libs=extra_libs
    )

    print_info(f"Project name: {choose_project_name()}")
    print_info(f"Target: {target_name}")
    print_info(f"CPP files: {len(unique_cpp)}")
    print_info(f"RC files: {len(unique_rc)}")
    print_info("Generated GeneratedBuild.mk successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
