# RomSeparator Module — ROM Split

Splits combined ROM into two separate ROMs. Three-tier: Module + Logic + Window.

## Architecture

```
RomSeparatorModule (IFeatureModule + IDropActionProvider)
  ├── owns → RomSeparatorWindow* (lazy, never destroyed until Module dtor)
  └── calls → RomSeparatorLogic (static: GetPossibleSplits, Generate)
```

## Split detection

3x3 brute-force on {8, 16, 32} × {8, 16, 32} MB.

| Total size | Options |
|------------|---------|
| 16MB | 8+8 |
| 24MB | 8+16, 16+8 |
| 32MB | 16+16 |
| 40MB | 8+32, 32+8 |
| 48MB | 16+32, 32+16 |
| 64MB | 32+32 |

## Generate algorithm

1. Read entire file
2. Slice at `leftSize` boundary: `data[0..left]` + `data[left..left+right]`
3. Write two files: `Rom1_XMB.rom` + `Rom2_XMB.rom` (equal splits) or `Rom_XMB.rom` + `Rom_YMB.rom` (unequal)

## Key patterns

| Aspect | Detail |
|--------|--------|
| **Drop** | Single file only, `.bin`/`.rom`, must be splittable size. |
| **Context menu** | 1 item: "Split ROM (Select file...)" — no `.bin` in filter (inconsistent with drop). |
| **Config** | 5 bool: `Enabled`, `OutputToHostFolderByDefault`, `ConfirmOverwrite`, `ShowSuccessMessage`, `VerboseLog` |
| **Window** | **Hide-on-close** (`SW_HIDE`), not destroyed. Reused across operations. Lazy-created in module. |
| **Warnings** | `SplitOption` uses `unsigned long` (32-bit → cap at ~4GB). Duplicate symmetric splits shown. |
