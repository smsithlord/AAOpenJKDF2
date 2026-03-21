/*
 * aarcadecore_api.h — Public C API for the AArcade Core DLL
 *
 * This header is shared between the DLL and any host application.
 * It contains NO engine-specific types — only standard C types.
 * Any game engine can use this interface.
 */

#ifndef AARCADECORE_API_H
#define AARCADECORE_API_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* API version — bump when the interface changes */
#define AARCADECORE_API_VERSION 3

/* DLL export/import macros */
#ifdef _WIN32
  #ifdef AARCADECORE_EXPORTS
    #define AARCADECORE_EXPORT __declspec(dllexport)
  #else
    #define AARCADECORE_EXPORT __declspec(dllimport)
  #endif
#else
  #define AARCADECORE_EXPORT __attribute__((visibility("default")))
#endif

/* ========================================================================
 * Host callbacks — provided by the host application at init time
 *
 * These let the DLL interact with the host without knowing engine types.
 * The host OWNS the render callback — the DLL just provides pixels when asked.
 * ======================================================================== */
typedef void (*AACore_PrintfFn)(const char* fmt, ...);
typedef int  (*AACore_GetKeyStateFn)(int key_index);
typedef void (*AACore_GetCurrentMapFn)(char* mapKeyOut, int mapKeySize);

typedef struct AACoreHostCallbacks {
    int api_version;
    AACore_PrintfFn          host_printf;
    AACore_GetKeyStateFn     get_key_state;
    AACore_GetCurrentMapFn   get_current_map;
} AACoreHostCallbacks;

/* ========================================================================
 * Exported DLL functions
 * ======================================================================== */

AARCADECORE_EXPORT int  aarcadecore_get_api_version(void);
AARCADECORE_EXPORT bool aarcadecore_init(const AACoreHostCallbacks* host_callbacks);
AARCADECORE_EXPORT void aarcadecore_shutdown(void);
AARCADECORE_EXPORT void aarcadecore_update(void);
AARCADECORE_EXPORT bool aarcadecore_is_active(void);

/* Get the material name the DLL wants to render to (e.g., "compscreen.mat").
 * The host should register its own engine-native texture callback for this material. */
AARCADECORE_EXPORT const char* aarcadecore_get_material_name(void);

/* Called by the host's texture callback to let the DLL fill a pixel buffer.
 * The host provides the buffer; the DLL writes pixels into it. */
AARCADECORE_EXPORT void aarcadecore_render_texture(
    void* pixelData, int width, int height, int is16bit, int bpp);

/* Get the audio sample rate the DLL needs (e.g., 32040 for SNES).
 * Returns 0 if no audio is available. Host uses this to open its audio device. */
AARCADECORE_EXPORT int aarcadecore_get_audio_sample_rate(void);

/* Pull audio samples from the DLL into the host's buffer.
 * buffer: interleaved stereo int16_t (L,R,L,R,...)
 * max_frames: max frames to read (1 frame = 2 samples)
 * Returns number of frames actually written. */
AARCADECORE_EXPORT int aarcadecore_get_audio_samples(int16_t* buffer, int max_frames);

/* Keyboard input modifier bitmask */
#define AACORE_MOD_ALT   (1 << 0)
#define AACORE_MOD_CTRL  (1 << 1)
#define AACORE_MOD_SHIFT (1 << 2)

/* Forward keyboard events to the active embedded instance.
 * vk_code: Windows virtual key code (VK_*)
 * modifiers: bitmask of AACORE_MOD_* */
AARCADECORE_EXPORT void aarcadecore_key_down(int vk_code, int modifiers);
AARCADECORE_EXPORT void aarcadecore_key_up(int vk_code, int modifiers);

/* Forward a text character to the active embedded instance.
 * unicode_char: UTF-32 codepoint
 * modifiers: bitmask of AACORE_MOD_* */
AARCADECORE_EXPORT void aarcadecore_key_char(unsigned int unicode_char, int modifiers);

/* Mouse button constants */
#define AACORE_MOUSE_LEFT   0
#define AACORE_MOUSE_RIGHT  1
#define AACORE_MOUSE_MIDDLE 2

/* Forward mouse events to the active embedded instance.
 * Coordinates are in overlay pixel space (e.g., 0-1920, 0-1080). */
AARCADECORE_EXPORT void aarcadecore_mouse_move(int x, int y);
AARCADECORE_EXPORT void aarcadecore_mouse_down(int button);
AARCADECORE_EXPORT void aarcadecore_mouse_up(int button);
AARCADECORE_EXPORT void aarcadecore_mouse_wheel(int delta);

/* Toggle the main menu HUD overlay on/off */
AARCADECORE_EXPORT void aarcadecore_toggle_main_menu(void);

/* Check if the main menu is currently open */
AARCADECORE_EXPORT bool aarcadecore_is_main_menu_open(void);

/* Check if the DLL wants the host to open the engine's native menu */
AARCADECORE_EXPORT bool aarcadecore_should_open_engine_menu(void);

/* Clear the engine menu request flag */
AARCADECORE_EXPORT void aarcadecore_clear_engine_menu_flag(void);

/* Check if the DLL wants the host to start a Libretro instance */
AARCADECORE_EXPORT bool aarcadecore_should_start_libretro(void);
AARCADECORE_EXPORT void aarcadecore_clear_start_libretro_flag(void);

/* Start the Libretro manager (called by host after clearing flag) */
AARCADECORE_EXPORT void aarcadecore_start_libretro(void);

/* Get the number of running tasks (embedded instances, excluding HUD) */
AARCADECORE_EXPORT int aarcadecore_get_task_count(void);

/* Render a specific task's pixels into a host-provided buffer.
 * taskIndex: 0-based index into running tasks.
 * Returns true if pixels were written. */
AARCADECORE_EXPORT bool aarcadecore_render_task_texture(
    int taskIndex, void* pixelData, int width, int height, int is16bit, int bpp);

/* Render the overlay (main menu) into a host-provided BGRA pixel buffer.
 * Returns true if pixels were written, false if no overlay is active.
 * The host should draw this as a fullscreen quad. */
AARCADECORE_EXPORT bool aarcadecore_render_overlay(
    void* pixelData, int width, int height);

/* Instance Manager — spawn objects from library browser */
AARCADECORE_EXPORT bool aarcadecore_has_pending_spawn(void);
AARCADECORE_EXPORT void aarcadecore_pop_pending_spawn(void);
AARCADECORE_EXPORT void aarcadecore_confirm_spawn(int thingIdx);
AARCADECORE_EXPORT bool aarcadecore_spawn_has_position(float* px, float* py, float* pz, int* sectorId, float* rx, float* ry, float* rz);
AARCADECORE_EXPORT void aarcadecore_on_map_loaded(void);
AARCADECORE_EXPORT void aarcadecore_on_map_unloaded(void);
AARCADECORE_EXPORT void aarcadecore_report_thing_transform(int thingIdx,
    float px, float py, float pz, int sectorId, float pitch, float yaw, float roll);
AARCADECORE_EXPORT int  aarcadecore_get_thing_task_index(int thingIdx);
AARCADECORE_EXPORT bool aarcadecore_get_thing_screen_path(int thingIdx, char* pathOut, int pathSize);
/* Load screen image pixels (BGRA, caller must free with aarcadecore_free_pixels) */
AARCADECORE_EXPORT bool aarcadecore_load_thing_screen_pixels(int thingIdx, void** pixelsOut, int* widthOut, int* heightOut);
AARCADECORE_EXPORT bool aarcadecore_load_thing_marquee_pixels(int thingIdx, void** pixelsOut, int* widthOut, int* heightOut);
AARCADECORE_EXPORT void aarcadecore_free_pixels(void* pixels);

/* Notify DLL that the player "used" (activated) an object in the game world.
 * Selects the object and activates its embedded instance (e.g. starts video). */
AARCADECORE_EXPORT void aarcadecore_object_used(int thingIdx);

/* Selector ray — notify DLL which AArcade thing the player is aiming at (-1 = none) */
AARCADECORE_EXPORT void aarcadecore_set_aimed_thing(int thingIdx);

/* Check if an embedded instance is being displayed fullscreen in the overlay */
AARCADECORE_EXPORT bool aarcadecore_is_fullscreen_active(void);

/* Exit fullscreen overlay mode (instance stays active on its sithThing screen) */
AARCADECORE_EXPORT void aarcadecore_exit_fullscreen(void);

/* ========================================================================
 * Function pointer typedefs for dynamic loading
 * ======================================================================== */
typedef int   (*aarcadecore_get_api_version_t)(void);
typedef bool  (*aarcadecore_init_t)(const AACoreHostCallbacks* host_callbacks);
typedef void  (*aarcadecore_shutdown_t)(void);
typedef void  (*aarcadecore_update_t)(void);
typedef bool  (*aarcadecore_is_active_t)(void);
typedef const char* (*aarcadecore_get_material_name_t)(void);
typedef void  (*aarcadecore_render_texture_t)(void* pixelData, int width, int height, int is16bit, int bpp);
typedef int   (*aarcadecore_get_audio_sample_rate_t)(void);
typedef int   (*aarcadecore_get_audio_samples_t)(int16_t* buffer, int max_frames);
typedef void  (*aarcadecore_key_down_t)(int vk_code, int modifiers);
typedef void  (*aarcadecore_key_up_t)(int vk_code, int modifiers);
typedef void  (*aarcadecore_key_char_t)(unsigned int unicode_char, int modifiers);
typedef void  (*aarcadecore_mouse_move_t)(int x, int y);
typedef void  (*aarcadecore_mouse_down_t)(int button);
typedef void  (*aarcadecore_mouse_up_t)(int button);
typedef void  (*aarcadecore_mouse_wheel_t)(int delta);
typedef void  (*aarcadecore_toggle_main_menu_t)(void);
typedef bool  (*aarcadecore_is_main_menu_open_t)(void);
typedef bool  (*aarcadecore_should_open_engine_menu_t)(void);
typedef void  (*aarcadecore_clear_engine_menu_flag_t)(void);
typedef bool  (*aarcadecore_should_start_libretro_t)(void);
typedef void  (*aarcadecore_clear_start_libretro_flag_t)(void);
typedef void  (*aarcadecore_start_libretro_t)(void);
typedef int   (*aarcadecore_get_task_count_t)(void);
typedef bool  (*aarcadecore_render_task_texture_t)(int taskIndex, void* pixelData, int width, int height, int is16bit, int bpp);
typedef bool  (*aarcadecore_render_overlay_t)(void* pixelData, int width, int height);
typedef bool  (*aarcadecore_has_pending_spawn_t)(void);
typedef void  (*aarcadecore_pop_pending_spawn_t)(void);
typedef void  (*aarcadecore_confirm_spawn_t)(int thingIdx);
typedef bool  (*aarcadecore_spawn_has_position_t)(float* px, float* py, float* pz, int* sectorId, float* rx, float* ry, float* rz);
typedef void  (*aarcadecore_on_map_loaded_t)(void);
typedef void  (*aarcadecore_on_map_unloaded_t)(void);
typedef void  (*aarcadecore_report_thing_transform_t)(int thingIdx,
    float px, float py, float pz, int sectorId, float pitch, float yaw, float roll);
typedef int   (*aarcadecore_get_thing_task_index_t)(int thingIdx);
typedef bool  (*aarcadecore_get_thing_screen_path_t)(int thingIdx, char* pathOut, int pathSize);
typedef bool  (*aarcadecore_load_thing_screen_pixels_t)(int thingIdx, void** pixelsOut, int* widthOut, int* heightOut);
typedef bool  (*aarcadecore_load_thing_marquee_pixels_t)(int thingIdx, void** pixelsOut, int* widthOut, int* heightOut);
typedef void  (*aarcadecore_free_pixels_t)(void* pixels);
typedef void  (*aarcadecore_object_used_t)(int thingIdx);
typedef void  (*aarcadecore_set_aimed_thing_t)(int thingIdx);
typedef bool  (*aarcadecore_is_fullscreen_active_t)(void);
typedef void  (*aarcadecore_exit_fullscreen_t)(void);

#ifdef __cplusplus
}
#endif

#endif /* AARCADECORE_API_H */
