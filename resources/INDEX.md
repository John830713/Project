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
├── tools/              # Tool docs (common → D:\Agent, local → project)
│   ├── common/         → D:\Agent\resources\tools\common\
│   └── local/          # Project scripts + external tool refs
├── skills/             → D:\Agent\resources\skills\
└── reference/          # Reference topics
    ├── design/         # Tampermonkey UI patterns (project-specific)
    ├── chain/          # INDEX chain specification
    ├── *.md            # Project-specific docs
    └── common/         → D:\Agent\resources\reference\ (conventions, git, log, ...)
```
