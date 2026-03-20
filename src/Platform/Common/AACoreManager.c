/*
 * AACoreManager — Host-side loader for aarcadecore.dll
 *
 * Loads the DLL dynamically, provides engine callbacks, and wraps the API.
 * The HOST owns the engine texture callback and calls the DLL to fill pixels.
 */

#include "AACoreManager.h"
#include "../../aarcadecore/aarcadecore_api.h"
#include "../../Engine/rdDynamicTexture.h"
#include "../../Platform/std3D.h"
#include "../../Win95/Window.h"
#include "../../stdPlatform.h"
#include "globals.h"

#include "SDL2_helper.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* DLL handle and function pointers */
static void* g_dll = NULL;
static aarcadecore_init_t              g_fn_init = NULL;
static aarcadecore_shutdown_t          g_fn_shutdown = NULL;
static aarcadecore_update_t            g_fn_update = NULL;
static aarcadecore_is_active_t         g_fn_is_active = NULL;
static aarcadecore_get_api_version_t   g_fn_get_api_version = NULL;
static aarcadecore_get_material_name_t g_fn_get_material_name = NULL;
static aarcadecore_render_texture_t    g_fn_render_texture = NULL;
static aarcadecore_get_audio_sample_rate_t g_fn_get_audio_sample_rate = NULL;
static aarcadecore_get_audio_samples_t     g_fn_get_audio_samples = NULL;
static aarcadecore_key_down_t              g_fn_key_down = NULL;
static aarcadecore_key_up_t                g_fn_key_up = NULL;
static aarcadecore_key_char_t              g_fn_key_char = NULL;
static aarcadecore_mouse_move_t             g_fn_mouse_move = NULL;
static aarcadecore_mouse_down_t             g_fn_mouse_down = NULL;
static aarcadecore_mouse_up_t               g_fn_mouse_up = NULL;
static aarcadecore_mouse_wheel_t            g_fn_mouse_wheel = NULL;
static aarcadecore_toggle_main_menu_t       g_fn_toggle_main_menu = NULL;
static aarcadecore_is_main_menu_open_t     g_fn_is_main_menu_open = NULL;
static aarcadecore_should_open_engine_menu_t g_fn_should_open_engine_menu = NULL;
static aarcadecore_clear_engine_menu_flag_t  g_fn_clear_engine_menu_flag = NULL;
static aarcadecore_should_start_libretro_t   g_fn_should_start_libretro = NULL;
static aarcadecore_clear_start_libretro_flag_t g_fn_clear_start_libretro_flag = NULL;
static aarcadecore_start_libretro_t          g_fn_start_libretro = NULL;
static aarcadecore_get_task_count_t         g_fn_get_task_count = NULL;
static aarcadecore_render_task_texture_t   g_fn_render_task_texture = NULL;
static aarcadecore_render_overlay_t        g_fn_render_overlay = NULL;

/* Cursor state (in screen coords) */
static int g_cursorX = 0;
static int g_cursorY = 0;
static GLuint g_cursorTexture = 0;

/* Per-task GL textures (host-managed) */
#define MAX_TASKS 16
#define TASK_TEX_WIDTH 1024
#define TASK_TEX_HEIGHT 1024
static GLuint g_taskTextures[MAX_TASKS] = {0};
static uint16_t* g_taskPixels = NULL; /* shared pixel buffer for task rendering */
static int g_taskTexturesReady = 0;

/* Thing-to-task mapping */
#define MAX_THING_MAPPINGS 64
typedef struct {
    void* thing;      /* sithThing* */
    int taskIndex;
} ThingTaskMapping;
static ThingTaskMapping g_thingTaskMap[MAX_THING_MAPPINGS] = {0};
static int g_thingTaskCount = 0;

/* The compscreen material's rdDDrawSurface we need to swap texture_id on */
static int g_savedTextureId = -1;

/* Fullscreen overlay state */
static GLuint g_overlayTexture = 0;
static uint32_t* g_overlayPixels = NULL;
#define OVERLAY_WIDTH  1920
#define OVERLAY_HEIGHT 1080

/* SDL audio device for playing DLL audio */
static SDL_AudioDeviceID g_audio_dev = 0;

/* ========================================================================
 * Host callback implementations
 * ======================================================================== */

static void host_printf(const char* fmt, ...)
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    stdPlatform_Printf("%s", buf);
}

static int host_get_key_state(int key_index)
{
    if (key_index < 0 || key_index >= JK_NUM_KEYS)
        return 0;
    return stdControl_aKeyInfo[key_index];
}

/* ========================================================================
 * Engine texture callback — host owns this, calls DLL to fill pixels
 * ======================================================================== */

static void aacore_texture_callback(
    rdMaterial* material, rdTexture* texture, int mipLevel,
    void* pixelData, int width, int height, rdTexFormat format, void* userData)
{
    if (mipLevel != 0)
        return;

    /* Try to get pixels from the DLL */
    if (g_fn_render_texture) {
        g_fn_render_texture(pixelData, width, height, format.is16bit, format.bpp);
        return;
    }

    /* No instance active — fill with solid green */
    if (format.is16bit) {
        uint16_t* dest = (uint16_t*)pixelData;
        uint16_t green565 = (0 << 11) | (63 << 5) | 0; /* bright green in RGB565 */
        for (int i = 0; i < width * height; i++)
            dest[i] = green565;
    }
}

/* ========================================================================
 * SDL audio callback — pulls samples from the DLL
 * ======================================================================== */

static void aacore_audio_callback(void* userdata, Uint8* stream, int len)
{
    int16_t* out = (int16_t*)stream;
    int samples_needed = len / (int)sizeof(int16_t);
    int frames_needed = samples_needed / 2;
    int frames_read = 0;

    if (g_fn_get_audio_samples)
        frames_read = g_fn_get_audio_samples(out, frames_needed);

    /* Fill remainder with silence */
    int samples_read = frames_read * 2;
    if (samples_read < samples_needed)
        memset(out + samples_read, 0, (samples_needed - samples_read) * sizeof(int16_t));
}

/* ========================================================================
 * Public API
 * ======================================================================== */

void AACoreManager_Init(void)
{
    int api_version;
    AACoreHostCallbacks callbacks;

    stdPlatform_Printf("AACoreManager: Loading aarcadecore.dll...\n");

    g_dll = SDL_LoadObject("aarcadecore.dll");
    if (!g_dll) {
        stdPlatform_Printf("AACoreManager: Failed to load DLL: %s\n", SDL_GetError());
        return;
    }

    /* Load exported functions */
    #define LOAD_FN(name) \
        g_fn_##name = (aarcadecore_##name##_t)SDL_LoadFunction(g_dll, "aarcadecore_" #name); \
        if (!g_fn_##name) { \
            stdPlatform_Printf("AACoreManager: Missing symbol aarcadecore_%s\n", #name); \
            SDL_UnloadObject(g_dll); g_dll = NULL; return; \
        }

    LOAD_FN(get_api_version)
    LOAD_FN(init)
    LOAD_FN(shutdown)
    LOAD_FN(update)
    LOAD_FN(is_active)
    LOAD_FN(get_material_name)
    LOAD_FN(render_texture)
    LOAD_FN(get_audio_sample_rate)
    LOAD_FN(get_audio_samples)
    LOAD_FN(key_down)
    LOAD_FN(key_up)
    LOAD_FN(key_char)
    LOAD_FN(mouse_move)
    LOAD_FN(mouse_down)
    LOAD_FN(mouse_up)
    LOAD_FN(mouse_wheel)
    LOAD_FN(toggle_main_menu)
    LOAD_FN(is_main_menu_open)
    LOAD_FN(should_open_engine_menu)
    LOAD_FN(clear_engine_menu_flag)
    LOAD_FN(should_start_libretro)
    LOAD_FN(clear_start_libretro_flag)
    LOAD_FN(start_libretro)
    LOAD_FN(get_task_count)
    LOAD_FN(render_task_texture)
    LOAD_FN(render_overlay)
    #undef LOAD_FN

    /* Verify API version */
    api_version = g_fn_get_api_version();
    if (api_version != AARCADECORE_API_VERSION) {
        stdPlatform_Printf("AACoreManager: API version mismatch (dll=%d, host=%d)\n",
                          api_version, AARCADECORE_API_VERSION);
        SDL_UnloadObject(g_dll);
        g_dll = NULL;
        return;
    }

    /* Provide host callbacks */
    callbacks.api_version = AARCADECORE_API_VERSION;
    callbacks.host_printf = host_printf;
    callbacks.get_key_state = host_get_key_state;

    if (!g_fn_init(&callbacks)) {
        stdPlatform_Printf("AACoreManager: DLL init failed\n");
        SDL_UnloadObject(g_dll);
        g_dll = NULL;
        return;
    }

    /* Dynamic texture hooks disabled for now — will re-enable for per-thing rendering later.
     * rdDynamicTexture_Register("compscreen.mat", aacore_texture_callback, NULL); */

    /* Open SDL audio device to play DLL audio */
    {
        int sample_rate = g_fn_get_audio_sample_rate();
        if (sample_rate > 0) {
            SDL_AudioSpec want, have;
            memset(&want, 0, sizeof(want));
            want.freq = sample_rate;
            want.format = AUDIO_S16SYS;
            want.channels = 2;
            want.samples = 2048;
            want.callback = aacore_audio_callback;
            want.userdata = NULL;

            g_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
            if (g_audio_dev > 0) {
                SDL_PauseAudioDevice(g_audio_dev, 0);
                stdPlatform_Printf("AACoreManager: Audio initialized - %d Hz\n", have.freq);
            } else {
                stdPlatform_Printf("AACoreManager: Failed to open audio: %s\n", SDL_GetError());
            }
        }
    }

    stdPlatform_Printf("AACoreManager: Initialized successfully\n");
}

void AACoreManager_Shutdown(void)
{
    if (g_audio_dev > 0) {
        SDL_CloseAudioDevice(g_audio_dev);
        g_audio_dev = 0;
    }

    if (g_fn_shutdown)
        g_fn_shutdown();

    if (g_dll) {
        SDL_UnloadObject(g_dll);
        g_dll = NULL;
    }

    g_fn_init = NULL;
    g_fn_shutdown = NULL;
    g_fn_update = NULL;
    g_fn_is_active = NULL;
    g_fn_get_api_version = NULL;
    g_fn_get_material_name = NULL;
    g_fn_render_texture = NULL;
    g_fn_get_audio_sample_rate = NULL;
    g_fn_get_audio_samples = NULL;
    g_fn_key_down = NULL;
    g_fn_key_up = NULL;
    g_fn_key_char = NULL;
    g_fn_mouse_move = NULL;
    g_fn_mouse_down = NULL;
    g_fn_mouse_up = NULL;
    g_fn_mouse_wheel = NULL;
    g_fn_toggle_main_menu = NULL;

    if (g_cursorTexture) { glDeleteTextures(1, &g_cursorTexture); g_cursorTexture = 0; }
    g_fn_is_main_menu_open = NULL;
    g_fn_should_open_engine_menu = NULL;
    g_fn_clear_engine_menu_flag = NULL;
    g_fn_should_start_libretro = NULL;
    g_fn_clear_start_libretro_flag = NULL;
    g_fn_start_libretro = NULL;
    g_fn_get_task_count = NULL;
    g_fn_render_task_texture = NULL;
    g_fn_render_overlay = NULL;

    for (int i = 0; i < MAX_TASKS; i++) {
        if (g_taskTextures[i]) { glDeleteTextures(1, &g_taskTextures[i]); g_taskTextures[i] = 0; }
    }
    if (g_taskPixels) { free(g_taskPixels); g_taskPixels = NULL; }
    g_taskTexturesReady = 0;
    g_thingTaskCount = 0;

    if (g_overlayTexture) { glDeleteTextures(1, &g_overlayTexture); g_overlayTexture = 0; }
    if (g_overlayPixels) { free(g_overlayPixels); g_overlayPixels = NULL; }

    stdPlatform_Printf("AACoreManager: Shutdown complete\n");
}

void AACoreManager_Update(void)
{
    if (g_fn_update)
        g_fn_update();

    /* Pre-render all task textures — each task gets its own GL texture */
    if (g_fn_get_task_count && g_fn_render_task_texture) {
        int count = g_fn_get_task_count();
        if (count > MAX_TASKS) count = MAX_TASKS;

        if (!g_taskPixels)
            g_taskPixels = (uint16_t*)malloc(TASK_TEX_WIDTH * TASK_TEX_HEIGHT * 2);

        for (int i = 0; i < count; i++) {
            /* Fill with green by default */
            for (int j = 0; j < TASK_TEX_WIDTH * TASK_TEX_HEIGHT; j++)
                g_taskPixels[j] = (0 << 11) | (63 << 5) | 0;

            g_fn_render_task_texture(i, g_taskPixels, TASK_TEX_WIDTH, TASK_TEX_HEIGHT, 1, 16);

            if (!g_taskTextures[i]) {
                glGenTextures(1, &g_taskTextures[i]);
            }
            glBindTexture(GL_TEXTURE_2D, g_taskTextures[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, TASK_TEX_WIDTH, TASK_TEX_HEIGHT, 0,
                         GL_RGB, GL_UNSIGNED_SHORT_5_6_5_REV, g_taskPixels);
        }
        g_taskTexturesReady = count;
    }
}

bool AACoreManager_IsActive(void)
{
    if (g_fn_is_active)
        return g_fn_is_active();
    return false;
}

void AACoreManager_KeyDown(int vk_code, int modifiers)
{
    if (g_fn_key_down)
        g_fn_key_down(vk_code, modifiers);
}

void AACoreManager_KeyUp(int vk_code, int modifiers)
{
    if (g_fn_key_up)
        g_fn_key_up(vk_code, modifiers);
}

void AACoreManager_KeyChar(unsigned int unicode_char, int modifiers)
{
    if (g_fn_key_char)
        g_fn_key_char(unicode_char, modifiers);
}

void AACoreManager_MouseMove(int x, int y)
{
    g_cursorX = x;
    g_cursorY = y;
    if (g_fn_mouse_move) {
        /* Map screen coords to overlay coords */
        int ox = (g_cursorX * OVERLAY_WIDTH) / (Window_xSize > 0 ? Window_xSize : 1);
        int oy = (g_cursorY * OVERLAY_HEIGHT) / (Window_ySize > 0 ? Window_ySize : 1);
        g_fn_mouse_move(ox, oy);
    }
}

void AACoreManager_MouseDown(int button)
{
    if (g_fn_mouse_down)
        g_fn_mouse_down(button);
}

void AACoreManager_MouseUp(int button)
{
    if (g_fn_mouse_up)
        g_fn_mouse_up(button);
}

void AACoreManager_MouseWheel(int delta)
{
    if (g_fn_mouse_wheel)
        g_fn_mouse_wheel(delta);
}

void AACoreManager_RegisterThingTask(void* pSithThing, int taskIndex)
{
    if (g_thingTaskCount >= MAX_THING_MAPPINGS) return;
    g_thingTaskMap[g_thingTaskCount].thing = pSithThing;
    g_thingTaskMap[g_thingTaskCount].taskIndex = taskIndex;
    g_thingTaskCount++;
    stdPlatform_Printf("AACoreManager: Registered thing %p → task %d\n", pSithThing, taskIndex);
}

void AACoreManager_ToggleMainMenu(void)
{
    if (g_fn_toggle_main_menu)
        g_fn_toggle_main_menu();
}

bool AACoreManager_IsMainMenuOpen(void)
{
    if (g_fn_is_main_menu_open)
        return g_fn_is_main_menu_open();
    return false;
}

bool AACoreManager_ShouldOpenEngineMenu(void)
{
    if (g_fn_should_open_engine_menu)
        return g_fn_should_open_engine_menu();
    return false;
}

void AACoreManager_ClearEngineMenuFlag(void)
{
    if (g_fn_clear_engine_menu_flag)
        g_fn_clear_engine_menu_flag();
}

bool AACoreManager_ShouldStartLibretro(void)
{
    if (g_fn_should_start_libretro)
        return g_fn_should_start_libretro();
    return false;
}

void AACoreManager_ClearStartLibretroFlag(void)
{
    if (g_fn_clear_start_libretro_flag)
        g_fn_clear_start_libretro_flag();
}

void AACoreManager_StartLibretro(void)
{
    if (g_fn_start_libretro)
        g_fn_start_libretro();
}

void AACoreManager_DrawOverlay(int screenWidth, int screenHeight)
{
    if (!g_fn_is_main_menu_open || !g_fn_is_main_menu_open())
        return;
    if (!g_fn_render_overlay)
        return;

    /* Allocate pixel buffer on first use */
    if (!g_overlayPixels) {
        g_overlayPixels = (uint32_t*)malloc(OVERLAY_WIDTH * OVERLAY_HEIGHT * 4);
        if (!g_overlayPixels) return;
    }

    /* Get pixels from DLL */
    if (!g_fn_render_overlay(g_overlayPixels, OVERLAY_WIDTH, OVERLAY_HEIGHT))
        return;

    /* Create GL texture on first use */
    if (!g_overlayTexture)
        glGenTextures(1, &g_overlayTexture);

    /* Upload BGRA pixels to GL texture */
    glBindTexture(GL_TEXTURE_2D, g_overlayTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, OVERLAY_WIDTH, OVERLAY_HEIGHT, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, g_overlayPixels);

    /* Add fullscreen quad to the engine's UI render list.
     * This gets drawn by std3D_DrawUIRenderList() using the engine's shader pipeline. */
    std3D_DrawUITexturedQuad(g_overlayTexture, 0.0f, 0.0f, (float)screenWidth, (float)screenHeight);

    /* Draw cursor wedge */
    if (!g_cursorTexture) {
        uint32_t white = 0xFFFFFFFF;
        glGenTextures(1, &g_cursorTexture);
        glBindTexture(GL_TEXTURE_2D, g_cursorTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    }
    /* Draw a small wedge/triangle as cursor using a tiny quad */
    {
        float cx = (float)g_cursorX;
        float cy = (float)g_cursorY;
        float cw = 16.0f;
        float ch = 20.0f;
        std3D_DrawUITexturedQuad(g_cursorTexture, cx, cy, cw, ch);
    }
}
