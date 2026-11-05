# Resources — project reference

Agent-facing documentation and local development scripts for this project. Every
directory has an `INDEX.md` + `.index.json` node; follow the chain instead of
scanning files, and verify it with
`python D:\Agent\resources\tools\common\chain-check\chain_check.py --root D:\Project --verify`.

## Forward links

| Path | Description |
|------|-------------|
| [tools/](tools/INDEX.md) | Tool docs and entity files |
| [skills/](skills/INDEX.md) | Reusable AI agent skill definitions |
| [reference/](reference/INDEX.md) | Rules, conventions, and notes |

## Referenced by

- [INDEX.md](../INDEX.md)

## Structure

```
resources/
├── tools/
│   ├── common/         # Pointer to D:\Agent\resources\tools\common\
│   └── local/          # Project scripts (build, tests, logs, module picker)
├── skills/             # Pointer to D:\Agent\resources\skills\
└── reference/          # Project docs
    ├── design/         # Architecture, debugging guides, playbooks
    ├── chain/          # INDEX chain specification
    ├── autoit-v3/      # AutoIt window classes / messages
    └── *.md            # Layout, build, module and setup notes
```

Machine-local framework resources (`D:\Agent\resources\`) are read-only from this
project; project-specific additions go here.
