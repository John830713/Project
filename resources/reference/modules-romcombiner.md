# RomCombiner Module — ROM Concatenation

Concatenates two ROM files into one. Three-tier: Module + Logic + Window.

## Architecture

```
RomCombinerModule (IFeatureModule + IDropActionProvider)
  ├── owns → RomCombinerTypes.h (RomInfo, OpenRequest, GenerateResult)
  ├── calls → RomCombinerLogic (static: NormalizeRom, Generate)
  └── creates → RomCombinerWindow (modeless, 2-column info panels)
```

## Detection (NormalizeRom)

| Raw size | displayType | effectiveSize | needsTrim |
|----------|-------------|---------------|-----------|
| 8MB (8388608) | `8MB` | 8MB | false |
| 8MB+1024 | `8MB+1024` | 8MB | **true** |
| 16MB | `16MB` | 16MB | false |
| 16MB+1024 | `16MB+1024` | 16MB | **true** |
| 32MB | `32MB` | 32MB | false |
| 32MB+1024 | `32MB+1024` | 32MB | **true** |
| else | `Unknown` | raw size | false |

## Generate algorithm

1. Read both files fully
2. If `needsTrim`, write `.trimmed.rom` backup (but **non-trimmed data used in output**)
3. `reserve(r1 + r2)`, `insert(r1)`, `insert(r2)` → `Combined.rom`
4. `outputToOrg`: exe dir vs ROM1's dir

## Key patterns

| Aspect | Detail |
|--------|--------|
| **Drop** | 2-phase accumulator. Collects first 2 files only. `.bin`/`.rom`. |
| **Context menu** | 1 item: "Combine ROMs (Select files...)" — 2 sequential open dialogs. |
| **Config** | `Enabled`, `VerboseLog` (both bool, `VerboseLog` never checked) |
| **Warnings** | `new RomCombinerWindow` → **leak** (never deleted). `sizeRank` unused. Output path inconsistency: UI uses `GetModuleFileNameW`, generate uses `current_path()`. |
