# Resources — New machine starting reference

Copy this template to `D:\Agent\resources\` when setting up a new machine:

```powershell
Copy-Item -Recurse "D:\Agent\template\resources" "D:\Agent\resources"
```

## Forward links

| Path | Description |
|------|-------------|
| [tools/](tools/INDEX.md) | Tool docs and entity files |
| [skills/](skills/INDEX.md) | Reusable AI agent skill definitions |
| [reference/](reference/INDEX.md) | Rules, conventions, and notes |

## Referenced by

- [INDEX.md](../INDEX.md)

## Notes

- This template is tracked by git and serves as the starting reference for new machines
- `D:\Agent\resources\` also contains force-tracked content not in this template

## Structure

```
resources/
├── tools/              # Tool docs (common + local)
├── skills/             # Skill definitions
└── reference/          # Reference topics
    ├── design/         # Tampermonkey UI patterns
    ├── task/           # Task lifecycle
    ├── log/            # Session log format
    ├── tool/           # Tool management rules
    ├── conventions/    # Naming + data sync rules
    ├── git/            # Git rules and workflow
    ├── mneme/          # Mneme memory rules
    ├── opencode/        # OpenCode app rules, restart, send-to-self
    └── chain/          # INDEX chain specification
```
