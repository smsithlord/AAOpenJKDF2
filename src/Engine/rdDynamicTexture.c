#include "rdDynamicTexture.h"
#include "rdMaterial.h"
#include "General/stdString.h"
#include "jk.h"
#include <string.h>

void rdDynamicTexture_Register(const char* matName, rdDynamicTextureCallback callback, void* userData)
{
    rdMaterial_RegisterDynamicTexture(matName, callback, userData);
}

void rdDynamicTexture_ExampleCallback(
    rdMaterial* material,
    rdTexture* texture,
    int mipLevel,
    void* pixelData,
    int width,
    int height,
    rdTexFormat format,
    void* userData
)
{
    static int debugOnce = 0;
    if (!debugOnce++) {
        jk_printf("OpenJKDF2: ExampleCallback invoked! pixelData=%p, %dx%d, mipLevel=%d, format=%d\n",
                  pixelData, width, height, mipLevel, format.is16bit);
    }

    if (!pixelData || width <= 0 || height <= 0)
        return;

    // Use a static frame counter
    static int frameCounter = 0;
    frameCounter++;

    if (format.is16bit) {
        // 16-bit texture (RGB565 or similar)
        uint16_t* pixels = (uint16_t*)pixelData;

        // Cycle through bright colors in RGB565 format
        // RGB565: RRRRRGGGGGGBBBBB
        uint16_t colors[] = {
            0xF800,  // Red
            0x07E0,  // Green
            0x001F,  // Blue
            0xFFE0,  // Yellow
            0xF81F,  // Magenta
            0x07FF,  // Cyan
            0xFFFF,  // White
            0x0000   // Black
        };
        uint16_t color = colors[(frameCounter / 30) % 8];

        if (frameCounter % 30 == 0) {
            jk_printf("OpenJKDF2: ExampleCallback filling 16-bit texture with color 0x%04X (frame %d)\n",
                      color, frameCounter);
        }

        // Fill entire texture with solid color
        for (int i = 0; i < width * height; i++) {
            pixels[i] = color;
        }
    } else {
        // 8-bit paletted texture
        uint8_t* pixels = (uint8_t*)pixelData;

        // Cycle through highly contrasting palette indices
        uint8_t colors[] = {0, 255, 128, 64, 192, 32, 224, 96};
        uint8_t colorIndex = colors[(frameCounter / 30) % 8];

        if (frameCounter % 30 == 0) {
            jk_printf("OpenJKDF2: ExampleCallback filling 8-bit texture with color index %d (frame %d)\n",
                      colorIndex, frameCounter);
        }

        // Fill entire texture with solid color
        memset(pixels, colorIndex, width * height);
    }
}
