import argparse
import json
import shutil
from pathlib import Path
from fnmatch import fnmatch
from datetime import datetime

ROOT = Path(__file__).resolve().parent
RULE_FILE = ROOT / "Project_Packaging_Rule.json"
REPORT_FILE = ROOT / "Project_Packaging_Report.txt"
DEFAULT_EXPORT_DIR = ROOT / "Export_CleanProject"


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def safe_rel(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT)).replace("\\", "/")
    except Exception:
        return str(path).replace("\\", "/")


def path_exists(rel_path: str) -> bool:
    return (ROOT / rel_path).exists()


def collect_rule_items(rule: dict, key: str):
    return rule.get(key, [])


def write_section(lines, title: str):
    sep = "=" * 80
    lines.append(sep)
    lines.append(title)
    lines.append(sep)


def write_subsection(lines, title: str):
    sep = "-" * 80
    lines.append(sep)
    lines.append(title)
    lines.append(sep)


def generate_report(rule: dict) -> str:
    lines = []

    write_section(lines, "PROJECT PACKAGING REPORT")
    lines.append(f"Generated At : {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    lines.append(f"Project Root : {ROOT}")
    lines.append(f"Rule File    : {RULE_FILE.name}")
    lines.append(f"Project Name : {rule.get('project_name', ROOT.name)}")
    lines.append(f"Description  : {rule.get('description', '')}")
    lines.append("")

    for key, title in [
        ("keep_required", "KEEP REQUIRED"),
        ("keep_project_specific", "KEEP PROJECT SPECIFIC"),
        ("needs_adjustment", "NEEDS ADJUSTMENT"),
        ("remove_generated", "REMOVE GENERATED"),
        ("remove_legacy", "REMOVE LEGACY")
    ]:
        write_section(lines, title)
        items = collect_rule_items(rule, key)
        if not items:
            lines.append("[No items]")
            lines.append("")
            continue

        for item in items:
            rel = item.get("path", "")
            item_type = item.get("type", "")
            note = item.get("note", "")
            exists = path_exists(rel)

            lines.append(f"Path   : {rel}")
            lines.append(f"Type   : {item_type}")
            lines.append(f"Exists : {exists}")
            lines.append(f"Note   : {note}")
            lines.append("")

    write_section(lines, "REMOVE BY PATTERN")
    for pattern in rule.get("remove_by_pattern", []):
        lines.append(pattern)
    lines.append("")

    write_section(lines, "REMOVE DIR NAMES")
    for name in rule.get("remove_dir_names", []):
        lines.append(name)
    lines.append("")

    write_section(lines, "MISSING RULE PATHS")
    missing = []
    for key in [
        "keep_required",
        "keep_project_specific",
        "needs_adjustment",
        "remove_generated",
        "remove_legacy"
    ]:
        for item in collect_rule_items(rule, key):
            rel = item.get("path", "")
            if rel and not path_exists(rel):
                missing.append((key, rel))

    if missing:
        for key, rel in missing:
            lines.append(f"{key}: {rel}")
    else:
        lines.append("[None]")
    lines.append("")

    return "\n".join(lines).rstrip() + "\n"


def build_keep_set(rule: dict):
    keep_paths = set()

    for key in ["keep_required", "keep_project_specific"]:
        for item in collect_rule_items(rule, key):
            rel = item.get("path", "").strip()
            if rel:
                keep_paths.add(rel.replace("\\", "/"))

    return keep_paths


def should_skip_dir_name(name: str, rule: dict) -> bool:
    return name in set(rule.get("remove_dir_names", []))


def matches_remove_pattern(path: Path, rule: dict) -> bool:
    for pattern in rule.get("remove_by_pattern", []):
        if fnmatch(path.name, pattern):
            return True
    return False


def copy_keep_path(src: Path, dst: Path):
    if src.is_dir():
        shutil.copytree(src, dst, dirs_exist_ok=True)
    elif src.is_file():
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def cleanup_export_dir(export_root: Path, rule: dict):
    # Remove by exact legacy/generated rules if they exist in export folder
    for key in ["remove_generated", "remove_legacy"]:
        for item in collect_rule_items(rule, key):
            rel = item.get("path", "").strip()
            if not rel:
                continue

            target = export_root / rel
            if target.is_dir():
                shutil.rmtree(target, ignore_errors=True)
            elif target.exists():
                try:
                    target.unlink()
                except Exception:
                    pass

    # Remove directories by name
    for p in list(export_root.rglob("*")):
        if p.is_dir() and should_skip_dir_name(p.name, rule):
            shutil.rmtree(p, ignore_errors=True)

    # Remove files by pattern
    for p in list(export_root.rglob("*")):
        if p.is_file() and matches_remove_pattern(p, rule):
            try:
                p.unlink()
            except Exception:
                pass


def ensure_optional_dirs(export_root: Path):
    (export_root / "Config").mkdir(parents=True, exist_ok=True)
    (export_root / "Log").mkdir(parents=True, exist_ok=True)


def export_clean(rule: dict, out_dir: Path):
    if out_dir.exists():
        shutil.rmtree(out_dir)

    out_dir.mkdir(parents=True, exist_ok=True)

    keep_paths = build_keep_set(rule)

    for rel in sorted(keep_paths):
        src = ROOT / rel
        if not src.exists():
            continue

        dst = out_dir / rel
        copy_keep_path(src, dst)

    ensure_optional_dirs(out_dir)
    cleanup_export_dir(out_dir, rule)


def main():
    parser = argparse.ArgumentParser(description="Project packaging and clean export tool.")
    parser.add_argument(
        "--mode",
        choices=["report", "export-clean"],
        default="report",
        help="Operation mode"
    )
    parser.add_argument(
        "--out",
        default=str(DEFAULT_EXPORT_DIR),
        help="Output directory for export-clean mode"
    )
    args = parser.parse_args()

    if not RULE_FILE.exists():
        print(f"[ERROR] Rule file not found: {RULE_FILE}")
        return 1

    rule = read_json(RULE_FILE)

    if args.mode == "report":
        content = generate_report(rule)
        REPORT_FILE.write_text(content, encoding="utf-8")
        print(f"[INFO] Report generated: {REPORT_FILE}")
        return 0

    if args.mode == "export-clean":
        out_dir = Path(args.out)
        if not out_dir.is_absolute():
            out_dir = ROOT / out_dir

        export_clean(rule, out_dir)
        print(f"[INFO] Clean export generated: {out_dir}")
        return 0

    return 0


if __name__ == "__main__":
    raise SystemExit(main())