#include "aarcadecore_internal.h"
#include "libretro_host.h"
#include <SDL.h>
#include <stdlib.h>
#include <unordered_map>

/* Forward declarations (internal to DLL) */
EmbeddedInstance* LibretroInstance_Create(const char* core_path, const char* game_path, const char* material_name);
void LibretroInstance_Destroy(EmbeddedInstance* inst);

#define LIBRETRO_CORE_PATH "bsnes_libretro.dll"
#define LIBRETRO_GAME_PATH "testgame.zip"
#define LIBRETRO_MATERIAL  "DynScreen.mat"

static EmbeddedInstance* g_activeInstance = NULL;

/* ========================================================================
 * Per-thread host registry
 *
 * Libretro callbacks (env, video, audio, input) are invoked by the core
 * during retro_*() calls and have no user_data parameter. The host that
 * "owns" the calling thread for the duration of those calls is found by
 * looking up SDL_ThreadID() in this registry. In Phase 1 the engine thread
 * is the only caller; Phase 3 moves this to a per-instance worker thread.
 * ======================================================================== */

namespace {
    SDL_mutex* g_registry_mutex = NULL;
    std::unordered_map<SDL_threadID, LibretroHost*>* g_thread_to_host = NULL;

    void ensure_registry(void)
    {
        if (!g_registry_mutex) g_registry_mutex = SDL_CreateMutex();
        if (!g_thread_to_host) g_thread_to_host = new std::unordered_map<SDL_threadID, LibretroHost*>();
    }
}

extern "C" void LibretroManager_RegisterThreadOwner(LibretroHost* host)
{
    ensure_registry();
    SDL_threadID tid = SDL_ThreadID();
    SDL_LockMutex(g_registry_mutex);
    (*g_thread_to_host)[tid] = host;
    SDL_UnlockMutex(g_registry_mutex);
}

extern "C" void LibretroManager_UnregisterThreadOwner(LibretroHost* host)
{
    if (!g_registry_mutex || !g_thread_to_host) return;
    SDL_threadID tid = SDL_ThreadID();
    SDL_LockMutex(g_registry_mutex);
    auto it = g_thread_to_host->find(tid);
    if (it != g_thread_to_host->end() && it->second == host)
        g_thread_to_host->erase(it);
    SDL_UnlockMutex(g_registry_mutex);
}

extern "C" LibretroHost* LibretroManager_FindByThread(void)
{
    if (!g_registry_mutex || !g_thread_to_host) return NULL;
    SDL_threadID tid = SDL_ThreadID();
    SDL_LockMutex(g_registry_mutex);
    auto it = g_thread_to_host->find(tid);
    LibretroHost* host = (it != g_thread_to_host->end()) ? it->second : NULL;
    SDL_UnlockMutex(g_registry_mutex);
    return host;
}

void LibretroManager_Init(void)
{
    if (g_host.host_printf) g_host.host_printf("LibretroManager: Initializing...\n");

    g_activeInstance = LibretroInstance_Create(LIBRETRO_CORE_PATH, LIBRETRO_GAME_PATH, LIBRETRO_MATERIAL);
    if (!g_activeInstance) {
        if (g_host.host_printf) g_host.host_printf("LibretroManager: Failed to create instance\n");
        return;
    }

    if (!g_activeInstance->vtable->init(g_activeInstance)) {
        if (g_host.host_printf) g_host.host_printf("LibretroManager: Failed to init instance\n");
        LibretroInstance_Destroy(g_activeInstance);
        g_activeInstance = NULL;
    }
}

void LibretroManager_Shutdown(void)
{
    if (g_activeInstance) {
        LibretroInstance_Destroy(g_activeInstance);
        g_activeInstance = NULL;
    }
}

void LibretroManager_Update(void)
{
    if (g_activeInstance && g_activeInstance->vtable->update)
        g_activeInstance->vtable->update(g_activeInstance);
}

EmbeddedInstance* LibretroManager_GetActive(void)
{
    return g_activeInstance;
}
