# Local tools

Machine-specific debug & dev tools for this project.

`.agent/` is scratch space for single-use scripts and logs — gitignored and
disposable. A helper only belongs here once it is stable and reusable; until then
keep it in `.agent/`.

## Scripts

| Script | Purpose |
|--------|---------|
| [select-modules.ps1](select-modules.ps1) | Manage Config_Loader.json — pick which modules load |
| [debug-build.ps1](debug-build.ps1) | Clean debug build (`DEBUG_CONSOLE=1`) + optional module filter |
| [run-tests.ps1](run-tests.ps1) | Run test suite by name or all |
| [view-log.ps1](view-log.ps1) | Tail module or category log files |
| [clean-debug.ps1](clean-debug.ps1) | Nuke build artifacts, logs, debug_log.txt, JSON config |

## Referenced by

- [tools/INDEX.md](../INDEX.md)
