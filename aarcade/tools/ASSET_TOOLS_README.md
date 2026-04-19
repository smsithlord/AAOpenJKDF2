# Asset Conversion Tools

Two small Python helpers for preparing assets exported from modern tools so the Sith engine (Jedi Knight / OpenJKDF2) accepts them.

## Requirements
- Python 3
- `pip install Pillow` (only needed for `bmp16_convert.py`)

---

## bmp16_convert.py

**Why:** Sith engine textures are 16-bit BMPs. Modern art pipelines produce 24/32-bit BMPs, PNGs, or JPGs. This script converts any Pillow-readable image into a 16-bit BMP using a `BITMAPINFOHEADER` + `BI_BITFIELDS` layout that the engine (and common editors like Mat16) can load.

**Two output formats:**
- `rgb565` — no alpha, better color depth. **Default.** Use for opaque textures (walls, floors, solid props).
- `argb1555` — 1-bit alpha, slightly less color. Use when a texture needs transparency.

**Usage:**
```
python bmp16_convert.py input.png output.bmp                    # defaults to rgb565
python bmp16_convert.py input.png output.bmp --format argb1555  # alpha-capable
```

Accepts any format Pillow can open (BMP, PNG, JPG, etc.) as input.

---

## fix_3do_uvs.py

**Why:** The Blender Sith addon exports `.3do` files with two issues the Sith engine doesn't like:
1. **UVs in pixel coordinates of the source texture** (e.g. 0–1024 for a 1024×1024 texture). The stock engine expects UVs in the 0–256 range, regardless of texture size. Dividing by 4 converts 1024-range UVs into the range the engine renders correctly.
2. **Model-level `RADIUS`** is set incorrectly (often a tiny value like `0.000326`), causing the object to cull/disappear at normal distances. The first mesh's `RADIUS` is usually correct, so this script copies it up to the model level.

**Usage:**
```
python fix_3do_uvs.py model.3do                   # overwrites in place, scale=4
python fix_3do_uvs.py model.3do -o fixed.3do      # write to a new file
python fix_3do_uvs.py model.3do -s 2              # custom UV divisor (e.g. 512 → 256)
```

Re-run after every re-export from Blender — the addon overwrites the fixes each time.
