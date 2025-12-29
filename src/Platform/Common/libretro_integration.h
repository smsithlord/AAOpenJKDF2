#ifndef LIBRETRO_INTEGRATION_H
#define LIBRETRO_INTEGRATION_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize Libretro integration
 *
 * This will:
 * - Load the hard-coded Libretro core (bsnes_libretro.dll)
 * - Load the hard-coded game (testgame.zip)
 * - Register the compscreen.mat dynamic texture callback
 *
 * Call this during game startup after rendering is initialized
 */
void libretro_integration_init(void);

/**
 * Update Libretro emulation
 *
 * Runs one frame of emulation. Call this every frame or every N frames
 * from the main game loop.
 */
void libretro_integration_update(void);

/**
 * Shutdown Libretro integration
 *
 * Unloads the core and cleans up resources.
 * Call this during game shutdown.
 */
void libretro_integration_shutdown(void);

/**
 * Check if Libretro is active
 *
 * @return true if libretro is initialized and running
 */
bool libretro_integration_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBRETRO_INTEGRATION_H */
