#ifndef LIBRETRO_INSTANCE_H
#define LIBRETRO_INSTANCE_H

#include "EmbeddedInstance.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create a LibretroInstance that wraps an EmbeddedInstance.
 * core_path: path to the Libretro core DLL (e.g., "bsnes_libretro.dll")
 * game_path: path to the game ROM (e.g., "testgame.zip")
 * material_name: in-game material to render to (e.g., "compscreen.mat")
 * Returns NULL on failure. */
EmbeddedInstance* LibretroInstance_Create(const char* core_path, const char* game_path, const char* material_name);

/* Destroy a LibretroInstance and free resources. */
void LibretroInstance_Destroy(EmbeddedInstance* inst);

#ifdef __cplusplus
}
#endif

#endif /* LIBRETRO_INSTANCE_H */
