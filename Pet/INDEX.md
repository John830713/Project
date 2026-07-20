# Pet

Desktop pet host/carrier UI for all feature modules. Transparent GDI+ layered window.

## Files

| File | Role |
|------|------|
| `MainWindow.h/cpp` | Layered transparent window, drag-drop, context menu, movement system |
| `PetConfig.h/cpp` | INI config (`Config/Config_Pet.ini`) for pet settings |
| `lang/zh-TW.ini` | Pet-specific translations |

## Right-Click Menu Structure

```
Pet >
  [✓] 置頂                — Toggle WS_EX_TOPMOST
  移動 >
    [●] 靜止 / [○] 移動   — Radio: enable/disable movement timer
    ---
    步伐...               — Slider popup (1-50 px)
    速度...               — Slider popup (10-1000 ms)
    [✓] 穿梭              — Toggle edge wrap vs bounce
  ---
  透明度...               — Slider popup (0-255)
  比例...                 — Slider popup (10-500 %)
Function >
  [module context menu items...]
---
Settings...
---
About Project...
---
Exit
```

## Movement System

- Timer-based (`SetTimer ID=1`) at `moveSpeed` interval
- Random initial direction (dx, dy from {-1,0,1}, not both zero)
- Each tick: window += (dx * step, dy * step)
- Screen edges: bounce (reverse axis) or shuttle (wrap to opposite side)
- Timer auto-stops on `WM_DESTROY` or when `moveEnabled = false`

## Adsorption (Magnetic Snap)

- When pet is within "stick" distance of a screen edge, snap flush to that edge
- Threshold = `(windowWidth + windowHeight) / 4`, minimum 40px

## Pet.png Rendering

- Loads `Pet.png` from executable directory via GDI+ (`Gdiplus::Image`)
- If missing/fails: renders green solid square as fallback

## Language Reload

- Context menu labels rebuilt on `WM_APP_RELOAD_PETCONFIG` (sent by SettingsDialog when language changes)
- Reloads TranslationService and rebuilds all menu strings

## Referenced by

- [INDEX.md](../INDEX.md)
