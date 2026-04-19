# Dynamic Embedded Instance Per-Thing Rendering — Research Notes

## Goal
Show different embedded instance content (e.g., Libretro on one screen, Steamworks browser on another) on different sithThings that share the same `compscreen.mat` material.

## What We Learned

### The Material System is Shared
- All sithThings using the same 3DO model (e.g., `slcompmoniter.3do`) share the **same `rdMaterial*` pointer** for `compscreen.mat`
- The material has ONE `rdDDrawSurface` with ONE `texture_id` (GL texture)
- ALL faces from ALL things using that material reference the SAME `rdDDrawSurface` pointer in the render list
- When the render list draws, it binds `texture_id` from the surface → all faces get the same texture

### Dynamic Texture Callback Behavior
- `rdDynamicTexture_Register("compscreen.mat", callback, userData)` attaches a callback to the material
- The callback fires from `rdMaterial_UpdateDynamicTexture()` inside `rdMaterial_AddToTextureCache()`
- For dynamic materials, the engine sets `texture_dirty = 1` and re-uploads every frame
- **The callback fires ONCE per material per frame** — NOT per-thing
  - After the first thing triggers the upload, subsequent things see `texture_loaded=1` and the material cache returns early
- Callback stats confirmed: `calls=60` (60 FPS), always from the same thing

### PreRenderThing Hook
- We added `AACoreManager_PreRenderThing(pThing)` in `sithRender_RenderThing()` before `rdThing_Draw()`
- This correctly fires for each visible sithThing with a model
- We confirmed the spawned things DO pass through this hook (visible at certain angles)
- The hook can find the `compscreen.mat` material on the model via `strstr(mat->mat_full_fpath, "compscreen")`

### Approach 1: GL Texture ID Swap in PreRenderThing
**Idea:** Pre-render each task to its own GL texture. In PreRenderThing, swap `opaqueMats[0].texture_id` to the task's GL texture.

**Problem:** The material is shared. When PreRenderThing swaps `texture_id`, it changes it for ALL things using that material. The render list accumulates faces from all things, then draws them all at once at the end of the frame. By then, `texture_id` has been set by the LAST thing to render — all faces show that thing's content.

**Flushing attempt:** We tried `rdCache_DrawRenderList()` + `rdCache_ResetRenderList()` in PostRenderThing to flush faces immediately after each thing. This didn't reliably work because:
- The dynamic callback's re-upload overwrote our GL texture swap
- Render order and batching weren't consistent

### Approach 2: Dynamic Callback with g_currentRenderTaskIndex
**Idea:** PreRenderThing sets a global `g_currentRenderTaskIndex`. The dynamic callback reads it to render the correct task's pixels.

**Problem:** The callback only fires once per frame (per material), not per-thing. So only one thing's task gets rendered to the shared texture.

### Approach 3: Direct GL Upload in PreRenderThing + Flush
**Idea:** PreRenderThing uploads task pixels directly to the material's GL texture via `glTexImage2D`, then flushes the render list.

**Problem:** Without the dynamic callback, the material is static (no `dynamicCallback` → no `texture_dirty=1`). The engine uploads the original texture once and never re-uploads. Our direct GL upload overwrites it, but the render list flush timing is inconsistent — sometimes it works (visible at certain camera angles), sometimes the original texture shows.

### Approach 4: Remove Dynamic Callback Entirely
**Idea:** No callback. Just use PreRenderThing GL upload + flush before/after draw.

**Problem:** Without the callback, the material caches the original texture. Our GL upload works but the engine's render pipeline may re-bind the cached texture during the draw.

### Approach 5: thingIdx on rdProcEntry + Sort + Flush Between Groups
**Idea:** Propagate `thingIdx` through the entire deferred render pipeline. Sort dynamic material faces by thingIdx so they're contiguous. Flush the render list between thingIdx groups so each group draws with its own texture content.

**Implementation:**
- Added `int32_t thingIdx` to `rdProcEntry` struct (`types.h`)
- Set `procEntry->thingIdx` during face submission in `rdModel3_DrawFace()` from `pCurThing->parentSithThing->thingIdx`
- Modified `rdCache_ProcFaceCompare` sort: non-dynamic faces first (by z_min), then dynamic faces grouped by thingIdx (then by z_min within group)
- Added `rdCache_currentThingIdx` global, set per-face before `rdMaterial_AddToTextureCache` in `SendFaceListToHardware`
- Callback in AACoreManager reads `rdCache_currentThingIdx` to generate unique color per thingIdx via Knuth multiplicative hash
- Added flush (`rdCache_DrawRenderList` + `rdCache_ResetRenderList`) between thingIdx groups in `SendFaceListToHardware`
- Fixed `tri_vert_idx` bug: re-sync `tri_vert_idx = rdCache_totalVerts` after each flush/reset (vertex indices were stale after reset)
- Reset `rdCache_currentThingIdx = -1` at start of each `SendFaceListToHardware` call
- Tried `glFlush()` after flush (removed — OpenGL's `glTexImage2D` already synchronizes internally)

**Result:** Still doesn't work. Each thing gets a unique color (confirmed via different viewing angles showing different colors), but when multiple compscreen things are visible simultaneously, they ALL render the SAME color. The color changes based on viewing angle but all screens always match.

**Why it fails:** Even though we flush the rdCache render list between thingIdx groups, `std3D_DrawRenderList()` internally re-batches all triangles sharing the same `rdDDrawSurface*` pointer into one `glDrawElements` call. The `glTexImage2D` called between groups DOES synchronize (GL guarantees pending draws complete before texture modification), so the flush approach should theoretically work — but in practice the symptom persists. The shared `rdDDrawSurface` means std3D's texture cache considers it "the same texture" and may optimize away re-binds or batch boundaries.

### Approach 6: Direct texture_id overwrite + rdCache_Flush per thing (WORKING)
**Idea:** Each spawned thing gets its own 256x256 GL texture (solid color). In PreRenderThing, call `rdCache_Flush()` to draw pending faces with the previous texture_id, then directly overwrite the shared `rdDDrawSurface.texture_id` on the original surface to this thing's GL texture. PostRenderThing is a no-op. The next PreRenderThing flush draws the current thing's faces before overwriting again.

**Implementation:**
- `AACoreManager_RegisterThingTask()` creates a per-thing GL texture via `glGenTextures` + `glTexImage2D` with a Knuth-hashed color
- `AACoreManager_PreRenderThing()` (called from `sithRender_RenderThing` before `rdThing_Draw`):
  1. Checks if this thing is in the tracked mapping
  2. Calls `rdCache_Flush()` — processes all pending faces with whatever texture_id is currently set
  3. Overwrites `originalTextures[0].alphaMats[0].texture_id` and `opaqueMats[0].texture_id` with this thing's GL texture
- End-of-frame `rdCache_Flush()` draws the last tracked thing's faces

**Result:** WORKS. Each spawned compscreen thing shows its own unique color simultaneously. No flickering, no view-angle dependencies.

**Why it works:** The direct overwrite modifies data AT the original `rdDDrawSurface` address. All compscreen tris point to this same address. At deferred draw time (`SendFaceListToHardware`), the surface's `texture_id` is whatever was last written. The `rdCache_Flush()` between tracked things ensures each thing's faces are drawn before the next overwrite.

**Why previous attempts failed:** Approach 5 tried swapping `material->textures` pointer to cloned rdTextures, expecting faces to reference different `rdDDrawSurface` addresses. But the renderer resolves through `material->textures` at deferred draw time, and the clone approach didn't produce visible changes. The direct overwrite bypasses pointer indirection entirely.

**Trade-offs:** One `rdCache_Flush()` per tracked compscreen thing per frame. For hundreds of things, this adds overhead. Acceptable for now.

## Root Cause (updated)
The deferred render pipeline stores `rdMaterial*` pointers in proc entries. The actual `rdDDrawSurface` is resolved at draw time via `material->textures[celIdx].alphaMats[mipLevel]`. The surface at that address is shared across all compscreen things.

**Working solution:** Direct `texture_id` overwrite on the shared surface, with `rdCache_Flush()` between tracked things to isolate each thing's draw batch.

## Files Involved
- `src/Engine/sithRender.c` — PreRenderThing/PostRenderThing hooks around `rdThing_Draw`
- `src/Platform/Common/AACoreManager.c` — PreRenderThing (flush + overwrite), RegisterThingTask (GL texture creation)
- `src/Raster/rdCache.c` — `rdCache_Flush()` used to force per-thing batch isolation
- `AACoreManager_RegisterThingTask` is called after spawning (via the Build Context Menu spawn path in `aarcadecore.dll`)

## Files Involved
- `src/Engine/rdMaterial.c` — `rdMaterial_AddToTextureCache`, `rdMaterial_UpdateDynamicTexture`
- `src/Engine/rdDynamicTexture.c` — callback registration
- `src/Raster/rdCache.c` — render list, face submission, `rdCache_DrawRenderList`
- `src/Engine/sithRender.c` — `sithRender_RenderThing` (PreRenderThing hook point)
- `src/Platform/GL/std3D.c` — `std3D_AddToTextureCache` (GL texture upload)
- `src/Platform/Common/AACoreManager.c` — PreRenderThing, PostRenderThing, GL upload
