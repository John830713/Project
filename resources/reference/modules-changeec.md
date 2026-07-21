# ChangeEC Module — EC Firmware Patcher

Patches EC binary into BIOS ROM at configurable offset. Three-tier: Module + Logic + Window.

## Architecture

```
ChangeECModule (IFeatureModule + IDropActionProvider)
  ├── owns → ChangeECTypes.h (ChangeECOpenRequest)
  ├── calls → ChangeECLogic (static: DetectFiles, Generate)
  └── creates → ChangeECWindow (modeless tool window)
```

## Key patterns

| Aspect | Detail |
|--------|--------|
| **Drop** | 2-phase accumulator (`m_pendingFiles`). Drops `.bin`/`.rom`. Auto-detects BIOS vs EC by extension then size heuristic. |
| **Context menu** | 1 item: "Patch EC (Select files...)" — opens 2 sequential `GetOpenFileName` dialogs (rom then bin). |
| **Config** | `Enabled`, `Offset` (hex string, default `0x6000`), `VerboseLog` (declared but **never read**). |
| **Output** | `<bios_stem>_mod.rom` in CWD ("Org") or ROM's dir ("Rom"), toggleable via button. |
| **Warnings** | `new ChangeECWindow` → **leak** (never deleted). `m_pendingFiles` not cleared on context-menu path. |

## Detection logic

1. Extension match (`.rom`=BIOS, `.bin`=EC)
2. Fallback: larger file = BIOS (wrong if EC > ROM)
3. No warning on ambiguous match

## Generate algorithm

1. Read ROM → `vector<uint8_t>`, read EC → `vector<uint8_t>`
2. If ROM too small: `resize(offset + ec.size(), 0xFF)` — pad with 0xFF
3. `std::copy(ec → rom at offset)`
4. Write `_mod.rom`
