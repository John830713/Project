# INDEX chain — Specification v1

## 1. Purpose

The INDEX chain is a bidirectional linked-list structure over directories, where each node is a directory containing:
- `INDEX.md` — human/agent readable content
- `.index.json` — machine-readable metadata (links + version)

This enables an agent to navigate the framework's documentation tree lazily, reading only what it needs.

## 2. Node rules

Every node directory must contain:
- `INDEX.md` — with `## Forward links` and `## Referenced by` sections
- `.index.json` — matching the schema below

Leaf nodes (no sub-nodes) have an empty `forward` array.

## 3. `.index.json` schema

```json
{
  "version": 1,
  "forward": ["child-dir/"],
  "referenced_by": [".."]
}
```

| Field | Required | Description |
|-------|----------|-------------|
| `version` | Yes | Schema version (integer). Must match across all nodes. |
| `forward` | Yes | Array of child directory paths (relative to this JSON). Empty for leaf nodes. |
| `referenced_by` | Yes | Array of paths to parent nodes (relative to this JSON). `..` for direct parent, more hops for non-node intermediates. |

## 4. Chain rules

- **Forward links** — parent → child. `forward` entries point from a node to its sub-nodes.
- **Backward links** — child → parent. `referenced_by` entries point from a node to its nearest ancestor node directory.
- Every `forward` entry must have a matching `referenced_by` entry in the target.
- Every `referenced_by` entry must have a matching `forward` entry in the source.

## 5. Version policy

- All `.index.json` files must have the same `version` value.
- When the schema changes, all nodes are updated together.
- `chain-check --init` validates version consistency before running.

## 6. Project setup guide

Use `D:\Agent\` as the reference architecture — its INDEX chain is the canonical model.

### 6.1 Prerequisites

- `chain_check.py` installed (`D:\Agent\resources\tools\common\chain-check\chain_check.py`)
- Python 3.6+ and `git` on PATH

### 6.2 Identify nodes

Every directory that should be navigable as a tree node needs an `.index.json` + `INDEX.md`.

Common project node structure:

```
project-root/           → root node (forward points to children)
├── resources/          → resource node
│   ├── reference/      → leaf node (forward: [])
│   ├── skills/         → leaf node
│   └── tools/          → leaf node
└── .agent/             → runtime data, NOT a chain node
```

### 6.3 Create `.index.json` per node

Root node — forward to immediate children:
```json
{"version": 1, "forward": ["resources/"], "referenced_by": []}
```

Interior node — forward to sub-nodes, referenced_by to parent:
```json
{"version": 1, "forward": ["reference/", "skills/", "tools/"], "referenced_by": [".."]}
```

Leaf node — empty forward:
```json
{"version": 1, "forward": [], "referenced_by": [".."]}
```

### 6.4 Update each `INDEX.md`

Every node INDEX.md must have `## Forward links` and `## Referenced by` sections.

Root node example:
```markdown
## Forward links

| Path | Description |
|------|-------------|
| [resources/](resources/INDEX.md) | Project resources |

## Referenced by

- *(Root node — entry point for this project's INDEX chain)*
```

Interior node example:
```markdown
## Forward links

| Path | Description |
|------|-------------|
| [reference/](reference/INDEX.md) | Design reference |

## Referenced by

- [INDEX.md](../INDEX.md)
```

Leaf node:
```markdown
## Referenced by

- [resources/INDEX.md](../INDEX.md)
```

### 6.5 Verify

```powershell
python D:\Agent\resources\tools\common\chain-check\chain_check.py --root <project-dir> --init
python D:\Agent\resources\tools\common\chain-check\chain_check.py --root <project-dir> --verify
```

### 6.6 Commit

The project now has a valid INDEX chain. Commit the new `.index.json` files and updated `INDEX.md` files together.

## 7. Referenced by

- [reference/INDEX.md](../INDEX.md)
