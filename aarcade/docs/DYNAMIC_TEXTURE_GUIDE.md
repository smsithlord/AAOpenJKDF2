# Dynamic Texture Callback System

This guide explains the dynamic texture callback system implemented for OpenJKDF2, which allows C code to modify texture pixels every frame before they are uploaded to the GPU.

## Overview

The dynamic texture callback system enables real-time modification of specific textures by name. When a material with a registered callback is rendered, the callback function is invoked with a pointer to the pixel data, allowing frame-by-frame manipulation of the texture.

## Use Case

The primary use case is for the `compscreen.mat` texture, which can now display dynamic content such as:
- Real-time game statistics
- Animated patterns or effects
- Custom HUD elements
- Debug visualization
- Video playback or streaming content

## Architecture

### Core Components

1. **rdDynamicTexture.h/c** - Public API for registering callbacks
2. **rdMaterial.h/c** - Internal callback management and invocation
3. **types.h** - Extended data structures
4. **std3D.c** - Texture re-upload logic for dynamic textures

### Data Flow

```
1. Registration: rdDynamicTexture_Register("compscreen.mat", callback, userData)
   └─> Stores callback in hash table by material name

2. Material Load: rdMaterial_LoadEntry_Common(...)
   └─> Checks hash table for registered callback
   └─> Attaches callback to rdMaterial structure

3. Frame Render: rdMaterial_AddToTextureCache(...)
   └─> Calls rdMaterial_UpdateDynamicTexture(...)
   └─> Invokes callback with pixel data pointer
   └─> Marks texture as dirty
   └─> Forces re-upload to GPU via std3D_AddToTextureCache(...)
```

## API Reference

### Registration Function

```c
void rdDynamicTexture_Register(
    const char* matName,              // e.g., "compscreen.mat"
    rdDynamicTextureCallback callback, // Your callback function
    void* userData                     // Optional user data
);
```

### Callback Signature

```c
typedef void (*rdDynamicTextureCallback)(
    rdMaterial* material,   // The material being rendered
    rdTexture* texture,      // Current texture/cel
    int mipLevel,           // Which mipmap level (0 = highest res)
    void* pixelData,        // Pointer to pixel buffer (MODIFIABLE)
    int width,              // Texture width at this mip level
    int height,             // Texture height at this mip level
    rdTexFormat format,     // Pixel format information
    void* userData          // User data from registration
);
```

## Usage Example

### Basic Example

```c
#include "Engine/rdDynamicTexture.h"

void myCallback(rdMaterial* material, rdTexture* texture, int mipLevel,
                void* pixelData, int width, int height,
                rdTexFormat format, void* userData)
{
    // Only modify highest resolution mipmap
    if (mipLevel != 0) return;

    uint8_t* pixels = (uint8_t*)pixelData;

    // Draw horizontal stripes
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int index = y * width + x;
            pixels[index] = (y / 8) % 2 == 0 ? 255 : 0;
        }
    }
}

// In Main_Startup() or similar initialization:
rdDynamicTexture_Register("compscreen.mat", myCallback, NULL);
```

### Animated Example

```c
typedef struct {
    int frameCounter;
} MyUserData;

void animatedCallback(rdMaterial* material, rdTexture* texture, int mipLevel,
                      void* pixelData, int width, int height,
                      rdTexFormat format, void* userData)
{
    if (mipLevel != 0) return;

    MyUserData* data = (MyUserData*)userData;
    uint8_t* pixels = (uint8_t*)pixelData;

    // Draw moving scanline
    int scanline = (data->frameCounter / 2) % height;
    for (int x = 0; x < width; x++) {
        pixels[scanline * width + x] = 255;
    }

    data->frameCounter++;
}

// In Main_Startup():
static MyUserData userData = {0};
rdDynamicTexture_Register("compscreen.mat", animatedCallback, &userData);
```

### Clear/Fill Example

```c
void clearCallback(rdMaterial* material, rdTexture* texture, int mipLevel,
                   void* pixelData, int width, int height,
                   rdTexFormat format, void* userData)
{
    if (mipLevel != 0) return;

    // Clear to black (palette index 0)
    memset(pixelData, 0, width * height);

    // Or fill with specific palette color
    // memset(pixelData, 128, width * height);
}
```

## Texture Format Notes

### 8-bit Paletted Textures (Most Common in JK)

- Each pixel is a single byte (uint8_t)
- Pixel value is an index into the material's color palette
- Palette is stored in `material->palette_alloc`
- Typical size: 256 colors (RGB)

Example palette access:
```c
rdColor24* palette = material->palette_alloc;
uint8_t paletteIndex = pixels[y * width + x];
uint8_t red = palette[paletteIndex].r;
uint8_t green = palette[paletteIndex].g;
uint8_t blue = palette[paletteIndex].b;
```

### 16-bit Textures (Less Common)

- RGB565 format: 5 bits red, 6 bits green, 5 bits blue
- RGB1555 format: 1 bit alpha, 5 bits each RGB
- Check `format.is16bit` to determine texture depth

## Performance Considerations

1. **Callback Overhead**: Callbacks are only invoked for registered materials, so there's zero overhead for normal textures.

2. **GPU Re-upload**: Dynamic textures are re-uploaded to the GPU every frame they're rendered. This uses `glTexSubImage2D` for efficiency.

3. **Mipmap Levels**: Consider only modifying mipLevel 0 (highest resolution) to reduce processing time. Lower mipmaps can be left unchanged or updated selectively.

4. **Frame Throttling**: For very expensive operations, consider only updating every N frames:
   ```c
   if (frameCounter % 3 == 0) {
       // Update texture
   }
   ```

## Implementation Details

### Structure Changes

**rdMaterial (types.h:1257-1294)**
```c
typedef struct rdMaterial {
    // ... existing fields ...
    rdDynamicTextureCallback dynamicCallback;
    void* dynamicCallbackUserData;
} rdMaterial;
```

**rdDDrawSurface (types.h:996-1031)**
```c
typedef struct rdDDrawSurface {
    // ... existing fields ...
    int texture_dirty;  // Flag for re-upload
} rdDDrawSurface;
```

### Registration System

Materials are registered in a hash table mapping material filename to callback+userData. When a material is loaded, the system checks if it matches a registered dynamic texture and attaches the callback.

### Re-upload Mechanism

When a dynamic texture is detected during `rdMaterial_AddToTextureCache`:
1. Callback is invoked with pixel data
2. `texture_dirty` flag is set
3. `std3D_AddToTextureCache` detects dirty flag
4. Texture is re-uploaded using existing OpenGL texture ID
5. Dirty flag is cleared

## Debugging Tips

1. **Print callback invocations**:
   ```c
   stdPlatform_Printf("Dynamic texture callback: %dx%d mip %d\n", width, height, mipLevel);
   ```

2. **Verify texture is loading**:
   - Check that `compscreen.mat` exists in the game's resource files
   - Ensure the material name matches exactly (case-sensitive on Linux)

3. **Test with simple pattern first**:
   - Start with solid fill or simple stripes
   - Verify pixels are being modified before adding complex logic

4. **Check mipmap levels**:
   - Most textures have 1-4 mipmap levels
   - Print `texture->num_mipmaps` to see how many exist

## Example: Displaying Game Stats

```c
void statsCallback(rdMaterial* material, rdTexture* texture, int mipLevel,
                   void* pixelData, int width, int height,
                   rdTexFormat format, void* userData)
{
    if (mipLevel != 0) return;

    uint8_t* pixels = (uint8_t*)pixelData;

    // Clear to dark gray
    memset(pixels, 8, width * height);

    // Draw a simple "bar graph" based on player health
    // (Assumes you can access game state from global variables)
    extern float playerHealth; // Example
    int barHeight = (int)(height * (playerHealth / 100.0f));

    for (int y = height - barHeight; y < height; y++) {
        for (int x = 0; x < 16; x++) {
            pixels[y * width + x] = 255; // Bright color
        }
    }
}
```

## Future Enhancements

Potential improvements to the system:

1. **Partial updates**: Allow callbacks to specify dirty regions for minimal GPU uploads
2. **Frame throttling API**: Built-in support for updating every N frames
3. **Texture format helpers**: Utility functions for drawing text, shapes, etc.
4. **Multiple callbacks**: Support multiple callbacks per texture for compositing
5. **Async updates**: Background thread for expensive pixel processing

## Troubleshooting

**Q: My callback isn't being called**
- Verify material name matches exactly (e.g., "compscreen.mat")
- Ensure registration happens before material is loaded
- Check that the material is actually being rendered in the scene

**Q: Texture appears corrupted**
- Verify you're writing within bounds (0 to width*height-1)
- Check that you're not modifying pixel data outside the callback
- Ensure pixel format matches (8-bit vs 16-bit)

**Q: Performance is poor**
- Only update mipLevel 0
- Consider frame throttling
- Profile callback to find expensive operations
- Use memset for fills instead of loops

**Q: Changes don't appear on screen**
- Verify texture_dirty flag is being set correctly
- Check that std3D_AddToTextureCache is being called
- Ensure OpenGL re-upload logic is working

## Credits

Implemented for OpenJKDF2 as a modular system for dynamic texture manipulation.
