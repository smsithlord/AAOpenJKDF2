# HUD Overlay System

The HUD overlay composites UI elements (cursor, browser tab, address bar) on top of embedded instance content (e.g. Steamworks Web Browser). It renders differently depending on the mode: fullscreen uses a high-res overlay quad, input mode composites onto the per-thing task texture.

## Architecture

```
overlay.html (Ultralight HUD instance)
    ├── Custom PNG cursor (follows mouse)
    ├── "Web Browser" tab (click to toggle address bar)
    └── Address bar (Back, Forward, Reload, URL input)
         └── Enter key → aapi.manager.navigateInstance(url)

Compositing pipeline:
    HUD renders → persistent pixel buffer (1920x1080 BGRA32)
                      ↓
    Fullscreen mode: alpha-blended onto fullscreen quad over SWB pixels
    Input mode: scaled + converted (BGRA32→RGB565) onto 1024x1024 task texture
```

## Rendering Modes

### Fullscreen Mode
- SWB renders to `pixelData` (1920x1080 BGRA32)
- HUD pixel buffer composited on top via `compositeHudOver()` (per-pixel alpha blend)
- Result uploaded to GL quad covering entire screen
- GL shader discards pixels with alpha < 0.01 (transparent areas show game behind)

### Input Mode (not fullscreen)
- No fullscreen overlay — `render_overlay` returns transparent pixels
- HUD pixel buffer scaled from 1920x1080 → 1024x1024 (nearest-neighbor)
- Converted from BGRA32 to RGB565 with alpha blending onto task texture
- Cursor + UI appear on the in-world sithThing screen

### Normal Gameplay / Menus
- HUD pixel buffer NOT updated (skipped when `g_overlayLoaded == false`)
- Menus (mainMenu.html, tabMenu.html) render directly as the HUD — no compositing

## Persistent Pixel Buffer

`UltralightManager_UpdateHudPixelBuffer()` renders the HUD Ultralight instance into a static 1920×1080×4 byte buffer. This runs **only when the overlay is active** (`g_overlayLoaded == true`), avoiding ~8MB/frame of unnecessary work during normal gameplay.

The buffer is read by:
- `aarcadecore_render_overlay()` — for fullscreen compositing
- `aarcadecore_render_task_texture()` — for input mode task texture compositing
- `isHudPixelOpaque()` — for click-through alpha testing

## Click-Through (Alpha Test)

Mouse events are routed based on the HUD pixel alpha at the cursor position:

```c
static bool isHudPixelOpaque(int x, int y) {
    // Returns true if alpha >= 250 at overlay coords (x,y)
}
```

- **Alpha >= 250** (opaque HUD element: tab, address bar, buttons): clicks go to HUD only
- **Alpha < 250** (transparent area or cursor at 0.9 opacity ≈ 230): clicks pass through to SWB
- `mouse_move`: always forwarded to HUD (cursor tracking), forwarded to instance only if non-opaque
- `mouse_down/up`: sent to HUD OR instance, not both

### Why 0.9 Cursor Opacity
The cursor image renders at `opacity: 0.9` (alpha ≈ 230), which is below the 250 threshold. This ensures clicks on the cursor itself pass through to the SWB rather than being consumed by the HUD.

## Keyboard Focus Routing

When a form input in the overlay (e.g. address bar) is focused, keyboard events should go to the HUD instead of the SWB.

**JS side** (overlay.html):
- `focusin` event: if target is `INPUT`/`TEXTAREA`/`[contenteditable]` → `aapi.manager.setHudInputActive(true)`
- `focusout` event: → `aapi.manager.setHudInputActive(false)`

**C++ side**:
- `g_hudInputActive` flag in UltralightManager
- `aarcadecore_key_down/up/char`: if flag is set and overlay is active, forwards to HUD via `UltralightManager_ForwardKeyDown/Up/Char` instead of to input target

## Overlay Lifecycle

### Loading
- Triggered by `UltralightManager_LoadOverlay()` when entering fullscreen or input mode
- Checks `g_overlayLoaded` flag — skips if already loaded (preserves UI state like open address bar)
- Tracks which instance via `g_overlayForInstance` — reloads overlay if switching to a different instance

### Unloading
- Triggered by `UltralightManager_UnloadOverlay()` when:
  - The associated embedded instance is deactivated (`deactivateInstance()`)
  - A HUD-native page opens (menu, tab menu, etc.) — clears `g_overlayLoaded`
- NOT unloaded on input/fullscreen exit — overlay stays loaded for quick re-entry
- Clears `g_hudInputActive` flag

### Instance Association
- `g_overlayForInstance` tracks which `EmbeddedInstance*` the overlay was loaded for
- Switching to a different instance → force reload (unload + load)
- `aarcadecore_clearOverlayAssociation()` clears the pointer when instance deactivates

## Navigate (Address Bar)

The `EmbeddedInstanceVtable` has a `navigate` entry:
```c
typedef void (*EmbeddedInstance_NavigateFn)(EmbeddedInstance* inst, const char* url);
```

- **SWB**: calls `ISteamHTMLSurface::LoadURL(handle, url, NULL)`
- **Libretro/Ultralight**: `NULL` (not supported)
- **Bridge**: `aapi.manager.navigateInstance(url)` finds the active input/fullscreen instance and calls its `navigate` vtable entry

## overlay.html Structure

```html
<body>
    <div class="reload-btn">●</div>          <!-- location.reload() -->
    <div class="overlay-bar">
        <div class="address-bar">             <!-- animated flyout -->
            <button>◀</button>                <!-- Back -->
            <button>▶</button>                <!-- Forward -->
            <button>↻</button>                <!-- Reload -->
            <input placeholder="Address...">  <!-- URL, Enter to navigate -->
        </div>
        <div class="overlay-tab">Web Browser</div>  <!-- click to toggle -->
    </div>
    <div class="overlay-cursor">              <!-- follows mouse -->
        <img src="cursors/default.png">
    </div>
</body>
```

All styles are inline (no arcadeHud.css dependency due to Ultralight caching issues). All interactive elements have fully opaque backgrounds (alpha=1.0) to work with click-through detection.

## Key Files

| File | Purpose |
|------|---------|
| `src/aarcadecore/ui/overlay.html` | HUD overlay page (cursor, tab, address bar) |
| `src/aarcadecore/ui/cursors/*.png` | Custom cursor images |
| `src/aarcadecore/UltralightManager.cpp` | Overlay load/unload, pixel buffer, mouse/key forwarding |
| `src/aarcadecore/aarcadecore.cpp` | Compositing in render_overlay/render_task_texture, click-through, keyboard routing |
| `src/aarcadecore/aarcadecore_internal.h` | `navigate` vtable entry |
| `src/aarcadecore/UltralightInstance.cpp` | JS bridge: getOverlayInstanceInfo, setHudInputActive, navigateInstance |
| `src/aarcadecore/InstanceManager.cpp` | getInstanceForBrowser, overlay cleanup in deactivateInstance |
| `src/Platform/Common/AACoreManager.c` | Host-side overlay rendering (DrawOverlay), cursor wedge removed |

## Mouse/Key Forwarding Functions (UltralightManager)

| Function | Purpose |
|----------|---------|
| `ForwardMouseMove(x, y)` | Send mouse position to HUD for cursor tracking |
| `ForwardMouseDown/Up(button)` | Send click to HUD (when alpha test passes) |
| `ForwardKeyDown/Up(vk, mods)` | Send key to HUD (when hudInputActive) |
| `ForwardKeyChar(char, mods)` | Send char to HUD (when hudInputActive) |
| `SetHudInputActive(bool)` | Set/clear keyboard focus routing flag |
| `IsHudInputActive()` | Check if HUD has keyboard focus |
| `UpdateHudPixelBuffer()` | Render HUD to persistent buffer (only when overlay loaded) |
| `GetHudPixels()` | Get pointer to persistent buffer |
