#ifndef _RDDYNAMICTEXTURE_H
#define _RDDYNAMICTEXTURE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Register a dynamic texture callback for a specific material.
 *
 * When a material with the specified name is loaded and rendered, the callback
 * will be invoked every frame, allowing the application to modify the pixel data
 * before it is uploaded to the GPU.
 *
 * @param matName The name of the material file (e.g., "compscreen.mat")
 * @param callback The callback function to invoke when the texture is rendered
 * @param userData Optional user data pointer passed to the callback
 *
 * Example:
 *   rdDynamicTexture_Register("compscreen.mat", myCallback, NULL);
 */
void rdDynamicTexture_Register(const char* matName, rdDynamicTextureCallback callback, void* userData);

/**
 * Example callback implementation that demonstrates pixel manipulation.
 * This draws a simple pattern on the texture.
 *
 * @param material The material being rendered
 * @param texture The texture within the material
 * @param mipLevel The mipmap level being used
 * @param pixelData Pointer to the pixel buffer (can be modified)
 * @param width Width of the texture at this mip level
 * @param height Height of the texture at this mip level
 * @param format The pixel format information
 * @param userData User data pointer (passed during registration)
 */
void rdDynamicTexture_ExampleCallback(
    rdMaterial* material,
    rdTexture* texture,
    int mipLevel,
    void* pixelData,
    int width,
    int height,
    rdTexFormat format,
    void* userData
);

#ifdef __cplusplus
}
#endif

#endif // _RDDYNAMICTEXTURE_H
