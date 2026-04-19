# AArcade OpenJK — Build Guide

All commands run from the repo root. CMake path:
```
F:/Installed Programs/Visual Studio Community 2022/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe
```

Abbreviated as `cmake` below. Build directory: `build_aarcade`.

## Full Build (everything)

Builds sith_engine → aarcadecore.dll → openjkdf2-64.exe + copies UI files.

```bash
cmake --build build_aarcade --config Release
```

**Time**: Long — recompiles all changed C/C++ files across all targets.

## Fast Builds (partial)

### UI files only (HTML/JS/CSS changes)

No compilation needed — just copy source files to the output directory:

```bash
cp -r src/aarcadecore/ui/* build_aarcade/Release/aarcadecore/ui/
```

**Time**: Instant. Use this when only editing files in `src/aarcadecore/ui/`.

**Note**: The `/*` glob copies *contents* of the ui folder, not the folder itself. Do NOT use `cp -r src/aarcadecore/ui/icons build_aarcade/.../icons` — this nests `icons/icons/`. Always use the `ui/*` form above, or let the CMake post-build step handle it (triggered by `--target aarcadecore`).

### DLL only (aarcadecore.dll)

Rebuilds only the aarcadecore DLL + copies UI files (post-build step):

```bash
cmake --build build_aarcade --config Release --target aarcadecore
```

**Time**: Fast — only compiles changed `.cpp` files in `src/aarcadecore/`. Use when editing DLL source files (`aarcadecore.cpp`, `InstanceManager.cpp`, `UltralightInstance.cpp`, etc.) but NOT engine files.

### EXE only (openjkdf2-64.exe)

Rebuilds only the game executable (links against sith_engine):

```bash
cmake --build build_aarcade --config Release --target openjkdf2-64
```

**Time**: Medium — recompiles changed engine `.c` files + re-links. Use when editing engine files (`AACoreManager.c`, `sithCog.c`, `aaMainMenu.c`, etc.) but NOT DLL files.

### DLL + EXE (no dependency rebuild)

When you changed both DLL and engine files:

```bash
cmake --build build_aarcade --config Release --target aarcadecore
cmake --build build_aarcade --config Release --target openjkdf2-64
```

### UI + DLL (common case)

When editing UI files and DLL source:

```bash
cmake --build build_aarcade --config Release --target aarcadecore
```

The `aarcadecore` target's post-build step automatically copies UI files.

## What changed → What to build

| Files changed | Build command |
|--------------|---------------|
| `src/aarcadecore/ui/*.html/js/css` only | `cp -r src/aarcadecore/ui/* build_aarcade/Release/aarcadecore/ui/` |
| `src/aarcadecore/*.cpp` | `--target aarcadecore` |
| `src/aarcadecore/ui/*` + `src/aarcadecore/*.cpp` | `--target aarcadecore` |
| `src/Platform/Common/AACoreManager.c` | `--target openjkdf2-64` |
| `src/Cog/*.c`, `src/Main/*.c`, etc. | `--target openjkdf2-64` |
| Both DLL and engine files | `--target aarcadecore` then `--target openjkdf2-64` |
| CMakeLists.txt or everything | Full build (no `--target`) |

## Troubleshooting: EXE locked (game still running)

If the linker fails with `LNK1104: cannot open file 'openjkdf2-64.exe'`, close the game and retry. If you already compiled successfully but the link step failed, you only need to re-link — not recompile:

```bash
cmake --build build_aarcade --config Release --target openjkdf2-64
```

This is fast because the `.obj` files are already up to date — it just re-links.

For the DLL, the same applies — if the game had `aarcadecore.dll` locked:

```bash
cmake --build build_aarcade --config Release --target aarcadecore
```

The post-build UI copy also re-runs automatically with either of these.

## CMake Targets Reference

| Target | Output | Contains |
|--------|--------|----------|
| `sith_engine` | Object library | All engine `.c` files (intermediate) |
| `aarcadecore` | `aarcadecore.dll` | DLL `.cpp` files + UI copy |
| `openjkdf2-64` | `openjkdf2-64.exe` | Game main + links sith_engine |
