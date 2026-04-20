/*
 * Libretro Host Implementation for OpenJKDF2
 *
 * Loads and runs Libretro cores (emulators) within the game,
 * displaying output on dynamic textures like DynScreen.mat.
 *
 * Phase 1 — SEH protection, per-thread callback dispatch, double-buffered video.
 * Phase 2 — full RETRO_ENVIRONMENT_* coverage, log iface, paths, core options + persistence.
 * Phase 3 — dedicated worker thread per core, 48 kHz audio resampling, save state / SRAM auto-load.
 *
 * Threading model (Phase 3):
 *   Engine thread             Worker thread (per-instance)
 *   ─────────────             ───────────────────────────
 *   create() ──spawn──▶       worker_thread_main()
 *                              ├ register self in per-thread registry
 *                              ├ load DLL, retro_init, set callbacks
 *                              ├ post init_done_sem (succ/fail) ◀─create() returns
 *                              ├ wait on wake_sem
 *   load_game() ──post──▶     ├ retro_load_game, restore SRAM, restore .state
 *                              │  post load_done_sem ◀─load_game() returns
 *                              ├ wait on wake_sem
 *   run_frame() ──post──▶     ├ retro_run; back→front video swap; audio paced
 *                              ├ ... loop ...
 *   destroy() ──flag+post──▶  ├ save .state, save SRAM
 *                              ├ retro_unload_game, retro_deinit, SDL_UnloadObject
 *                              └ exit (engine SDL_WaitThread joins)
 *
 *   Engine→worker is one-way: post wake_sem after writing into request fields under
 *   request_lock. There's a single wake semaphore for all event types — the worker
 *   inspects atomic flags and the request struct on each wake to decide what to do.
 *
 *   Audio: worker writes the post-resample ring (target = 48 kHz); engine reads it.
 *   Indices are std::atomic<size_t> so the SPSC handoff is well-defined.
 *
 *   Video: worker writes the back buffer in cbVideoRefresh, swaps under video_lock
 *   at end of retro_run. Engine reads front buffer pointer (stable until next swap).
 *
 *   Options/option_defs: protected by options_lock — worker reads in env callbacks,
 *   engine writes from the JS UI bridge.
 */

#include "libretro_host.h"
#include "libretro_seh.h"
#include "libretro_archive.h"
#include "aarcadecore_internal.h"
#include "../../libretro_examples/libretro.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define HOST_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define HOST_MKDIR(p) mkdir((p), 0755)
#endif

/* GL proc-address loader.
 *
 * SDL_GL_GetProcAddress is unusable from inside aarcadecore.dll because SDL is
 * statically linked into both the DLL and the engine — each module has its own
 * SDL state, and the DLL's video subsystem is never initialized. The function
 * returns NULL and reports "Video subsystem has not been initialized" even when
 * a real GL context is current via wglMakeCurrent.
 *
 * wglGetProcAddress on Windows talks straight to the GL driver against the
 * thread's current context — no SDL state involved. For GL 1.1 functions
 * (glClear, glViewport, etc.) wglGetProcAddress returns NULL by spec; fall back
 * to GetProcAddress on opengl32.dll. */
static void* load_gl_proc(const char* name)
{
#ifdef _WIN32
    void* p = (void*)wglGetProcAddress(name);
    /* wglGetProcAddress returns 1, 2, 3, or -1 in some buggy drivers — treat
     * those as failure too. Real pointers are always >= 4. */
    if ((uintptr_t)p <= 3 || p == (void*)-1) {
        static HMODULE opengl32 = NULL;
        if (!opengl32) opengl32 = GetModuleHandleA("opengl32.dll");
        if (opengl32) p = (void*)GetProcAddress(opengl32, name);
        else p = NULL;
    }
    return p;
#else
    return SDL_GL_GetProcAddress(name);
#endif
}

/* Fixed output sample rate. The engine opens its SDL audio device at this rate;
 * the resampler converts whatever the core produces to it. */
static constexpr int LIBRETRO_TARGET_AUDIO_RATE = 48000;

/* Function pointer typedefs for loaded core */
typedef void (*retro_init_t)(void);
typedef void (*retro_deinit_t)(void);
typedef unsigned (*retro_api_version_t)(void);
typedef void (*retro_get_system_info_t)(struct retro_system_info *info);
typedef void (*retro_get_system_av_info_t)(struct retro_system_av_info *info);
typedef void (*retro_set_environment_t)(retro_environment_t);
typedef void (*retro_set_video_refresh_t)(retro_video_refresh_t);
typedef void (*retro_set_audio_sample_t)(retro_audio_sample_t);
typedef void (*retro_set_audio_sample_batch_t)(retro_audio_sample_batch_t);
typedef void (*retro_set_input_poll_t)(retro_input_poll_t);
typedef void (*retro_set_input_state_t)(retro_input_state_t);
typedef void (*retro_set_controller_port_device_t)(unsigned port, unsigned device);
typedef void (*retro_reset_t)(void);
typedef void (*retro_run_t)(void);
typedef bool (*retro_load_game_t)(const struct retro_game_info *game);
typedef void (*retro_unload_game_t)(void);
typedef unsigned (*retro_get_region_t)(void);
typedef void* (*retro_get_memory_data_t)(unsigned id);
typedef size_t (*retro_get_memory_size_t)(unsigned id);
/* Phase 3: state save/load */
typedef size_t (*retro_serialize_size_t)(void);
typedef bool (*retro_serialize_t)(void* data, size_t size);
typedef bool (*retro_unserialize_t)(const void* data, size_t size);

/* Internal: schema entry for one core option as declared by the core. */
struct CoreOptionDef {
    std::string key;
    std::string display;
    std::string default_value;
    std::vector<std::string> values;
};

/* Phase 5c: per-extension content-info override declared by the core via
 * RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE. Each entry's `extensions` is the
 * pipe-separated list (lowercased here for simpler matching). */
struct ContentOverrideEntry {
    std::string extensions;
    bool need_fullpath;
    bool persistent_data;
};

/* Host state structure */
struct LibretroHost {
    /* DLL handle */
    void* core_dll = nullptr;

    /* Core function pointers */
    retro_init_t retro_init = nullptr;
    retro_deinit_t retro_deinit = nullptr;
    retro_api_version_t retro_api_version = nullptr;
    retro_get_system_info_t retro_get_system_info = nullptr;
    retro_get_system_av_info_t retro_get_system_av_info = nullptr;
    retro_set_environment_t retro_set_environment = nullptr;
    retro_set_video_refresh_t retro_set_video_refresh = nullptr;
    retro_set_audio_sample_t retro_set_audio_sample = nullptr;
    retro_set_audio_sample_batch_t retro_set_audio_sample_batch = nullptr;
    retro_set_input_poll_t retro_set_input_poll = nullptr;
    retro_set_input_state_t retro_set_input_state = nullptr;
    retro_set_controller_port_device_t retro_set_controller_port_device = nullptr;
    retro_reset_t retro_reset = nullptr;
    retro_run_t retro_run = nullptr;
    retro_load_game_t retro_load_game = nullptr;
    retro_unload_game_t retro_unload_game = nullptr;
    retro_get_region_t retro_get_region = nullptr;
    retro_get_memory_data_t retro_get_memory_data = nullptr;
    retro_get_memory_size_t retro_get_memory_size = nullptr;
    /* Phase 3: optional symbols — cores not supporting save/load will leave these null. */
    retro_serialize_size_t retro_serialize_size = nullptr;
    retro_serialize_t retro_serialize = nullptr;
    retro_unserialize_t retro_unserialize = nullptr;

    /* Video state — double-buffered. */
    SDL_mutex* video_lock = nullptr;
    void* video_frame_front = nullptr;
    void* video_frame_back = nullptr;
    size_t video_frame_front_capacity = 0;
    size_t video_frame_back_capacity = 0;
    unsigned frame_width = 0;
    unsigned frame_height = 0;
    size_t frame_pitch = 0;
    enum retro_pixel_format pixel_format = RETRO_PIXEL_FORMAT_XRGB8888;
    SDL_atomic_t frame_updated;

    /* System info */
    struct retro_system_info system_info{};
    struct retro_system_av_info av_info{};

    SDL_atomic_t game_loaded;
    SDL_atomic_t core_crashed;

    int16_t input_state[2] = {0, 0};
    /* analog_state[port][stick][axis]: stick 0=L,1=R; axis 0=X,1=Y */
    int16_t analog_state[2][2][2] = {{{0,0},{0,0}},{{0,0},{0,0}}};
    uint8_t keyboard_state[512] = {0};

    /* Audio ring at the post-resample target rate (48 kHz). 250 ms of stereo at
     * 48 kHz = 24000 int16 samples; round to 32768 power-of-two. */
    int16_t* audio_ring_buf = nullptr;
    size_t audio_ring_size = 0;
    std::atomic<size_t> audio_ring_write{0};
    std::atomic<size_t> audio_ring_read{0};
    /* Resampler state — fractional source-index carry between batch callbacks. */
    double resample_phase = 0.0;
    /* For the single-sample callback's decimation accumulator. */
    double single_sample_accum = 0.0;

    /* Strings handed to the core via env callbacks (paths, option values). */
    std::vector<char*> allocated_variable_strings;

    /* Phase 5: temp files extracted from archives (zips that core can't read
     * natively). Unlinked at host destroy. */
    std::vector<std::string> extracted_temp_files;

    /* Pending state restore. Some cores (mupen64plus-next) refuse
     * retro_unserialize until their internal emulator is fully initialized,
     * which doesn't happen until the first retro_run. We read the .state file
     * at load_game time, then re-attempt unserialize on each early frame
     * until it succeeds or we give up. */
    std::vector<char> pending_state_buf;
    int               pending_state_attempts_left = 0;  /* counts down per frame */

    /* Phase 5c: content overrides + game metadata for GET_GAME_INFO_EXT. */
    std::vector<ContentOverrideEntry> content_overrides;

    /* Loaded-game metadata. Strings stay alive until libretro_host_destroy
     * because the core may hold pointers into them via retro_game_info_ext. */
    std::string  loaded_full_path;
    std::string  loaded_archive_path;   /* empty if not from archive */
    std::string  loaded_archive_file;   /* entry name within archive */
    std::string  loaded_dir;
    std::string  loaded_name;
    std::string  loaded_ext;
    bool         loaded_file_in_archive = false;
    bool         loaded_persistent_data = false;
    void*        loaded_data = nullptr;       /* for game_info_ext.data */
    size_t       loaded_data_size = 0;        /* for game_info_ext.size */

    /* ROM buffers we own past load_game (for persistent_data=true entries).
     * Without this, retro_load_game would receive a buffer that gets freed on
     * return — cores that keep the pointer alive would crash. */
    std::vector<void*> persistent_rom_buffers;

    /* Allocated retro_game_info_ext structs (one per GET_GAME_INFO_EXT call).
     * The core may hold the pointer for the entire game lifetime; we free at
     * host destroy. */
    std::vector<struct retro_game_info_ext*> allocated_game_info_ext;

    /* Core options.
     *  options_current   — core-tier values (apply across every game on this core)
     *  options_game      — game-tier overrides (per-ROM); empty entry = inherit
     *  Resolution order at GET_VARIABLE time:
     *    options_game[key] → options_current[key] → declared default → values[0]
     */
    SDL_mutex* options_lock = nullptr;
    std::vector<CoreOptionDef> option_defs;
    std::map<std::string, std::string> options_current;
    std::map<std::string, std::string> options_game;
    bool options_have_changed = false;
    /* Tracks which game's overrides are currently loaded into options_game so
     * a hot-swap to a new ROM (Phase 6) can save+swap the right per-game file. */
    std::string options_game_loaded_for;

    /* Identifiers derived from core_path, used to namespace per-core paths. */
    std::string core_basename;
    std::string core_full_path;
    /* Loaded game's basename (without extension) for SRAM/state file names. */
    std::string game_basename;

    /* ---------------- Phase 3: worker plumbing ---------------- */
    SDL_Thread*    worker_thread = nullptr;
    SDL_sem* wake_sem = nullptr;
    SDL_atomic_t   shutdown_flag;
    SDL_atomic_t   pending_frames;

    /* Init handshake */
    SDL_sem* init_done_sem = nullptr;
    bool           init_result = false;

    /* Load-game handshake */
    SDL_mutex*     request_lock = nullptr;
    bool           load_request_pending = false;
    std::string    pending_game_path;
    SDL_sem* load_done_sem = nullptr;
    bool           load_result = false;

    /* Reset request — flag set by engine, cleared by worker after retro_reset. */
    SDL_atomic_t   reset_request;

    /* ---------------- Phase 4: HW-accelerated rendering ---------------- */
    /* Filled by SET_HW_RENDER. The core writes context_reset/context_destroy and
     * its requested context_type/version/depth/stencil; we fill in
     * get_current_framebuffer + get_proc_address so cores can target our FBO. */
    bool                           hw_render_pending = false;   /* core requested HW; context not yet created */
    bool                           hw_render_active  = false;   /* context+FBO live, retro_run is rendering through them */
    bool                           hw_context_reset_called = false; /* one-shot guard for context_reset */
    struct retro_hw_render_callback hw_cb{};

    /* Cross-thread GL-context creation request: worker sets these and waits on
     * hw_create_done_sem; the engine pump (running inside libretro_host_create
     * or libretro_host_load_game while we block on init/load) services it. */
    SDL_atomic_t  hw_create_request;       /* worker→engine: please create the GL context */
    SDL_sem*      hw_create_done_sem = nullptr;
    void*         hw_gl_context = nullptr; /* SDL_GLContext returned by engine, opaque on this side */

    /* FBO + attachments. Recreated when av_info.geometry.max_width/height grows. */
    GLuint        fbo = 0;
    GLuint        fbo_color_tex = 0;
    GLuint        fbo_depth_rb = 0;
    int           fbo_w = 0;
    int           fbo_h = 0;

    /* Cached pixel buffer for glReadPixels — sized fbo_w*fbo_h*4 BGRA. We fill
     * this on the worker thread inside cbVideoRefresh, then mutex-swap it into
     * video_frame_front so the engine's render path sees a stable readback. */

    /* GL function pointers loaded from the worker's context after activation.
     * Anything past GL 1.1 (which is all FBO ops) needs runtime loading. */
    PFNGLGENFRAMEBUFFERSPROC      glGenFramebuffersFn = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC   glDeleteFramebuffersFn = nullptr;
    PFNGLBINDFRAMEBUFFERPROC      glBindFramebufferFn = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2DFn = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatusFn = nullptr;
    PFNGLGENRENDERBUFFERSPROC     glGenRenderbuffersFn = nullptr;
    PFNGLDELETERENDERBUFFERSPROC  glDeleteRenderbuffersFn = nullptr;
    PFNGLBINDRENDERBUFFERPROC     glBindRenderbufferFn = nullptr;
    PFNGLRENDERBUFFERSTORAGEPROC  glRenderbufferStorageFn = nullptr;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbufferFn = nullptr;
};

/* ========================================================================
 * Path helpers
 * ======================================================================== */

static void ensure_dir(const std::string& path)
{
    if (path.empty()) return;
    std::string tmp;
    tmp.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        tmp.push_back((c == '/') ? '\\' : c);
        if ((c == '/' || c == '\\') && !tmp.empty())
            HOST_MKDIR(tmp.c_str());
    }
    HOST_MKDIR(tmp.c_str());
}

static std::string sys_dir_for(const LibretroHost* host)
{
    return std::string("aarcadecore/libretro/system/") + host->core_basename;
}
static std::string save_dir_for(const LibretroHost* host)
{
    return std::string("aarcadecore/libretro/saves/") + host->core_basename;
}
static std::string content_dir_for(const LibretroHost* host)
{
    return std::string("aarcadecore/libretro/content/") + host->core_basename;
}
static std::string options_file_for(const LibretroHost* host)
{
    return std::string("aarcadecore/libretro/config/") + host->core_basename + ".opt";
}
static std::string game_options_dir_for(const LibretroHost* host)
{
    return std::string("aarcadecore/libretro/config/games/") + host->core_basename;
}
static std::string game_options_file_for(const LibretroHost* host, const std::string& game_basename)
{
    return game_options_dir_for(host) + "/" + game_basename + ".opt";
}
static std::string sram_file_for(const LibretroHost* host)
{
    return save_dir_for(host) + "/" + host->game_basename + ".srm";
}
static std::string state_file_for(const LibretroHost* host)
{
    return save_dir_for(host) + "/" + host->game_basename + ".state";
}

static std::string derive_basename(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    return name;
}

static const char* publish_string(LibretroHost* host, const std::string& s)
{
    char* buf = (char*)malloc(s.size() + 1);
    if (!buf) return nullptr;
    memcpy(buf, s.c_str(), s.size() + 1);
    host->allocated_variable_strings.push_back(buf);
    return buf;
}

/* ========================================================================
 * Option persistence
 * ======================================================================== */

static void load_persisted_options(LibretroHost* host)
{
    std::ifstream f(options_file_for(host));
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (!val.empty() && val.back() == '\r') val.pop_back();
        host->options_current[key] = val;
    }
}

static void save_persisted_options(LibretroHost* host)
{
    if (host->options_current.empty()) return;
    ensure_dir("aarcadecore/libretro/config");
    std::ofstream f(options_file_for(host));
    if (!f.is_open()) return;
    for (const auto& kv : host->options_current)
        f << kv.first << '=' << kv.second << '\n';
}

/* Per-game overrides — same key=value format as the core .opt, lives at
 * aarcadecore/libretro/config/games/<core>/<game>.opt. Loaded after a game's
 * basename is known (post-load_game). Saved on host destroy + on every UI
 * write so an ungraceful shutdown still preserves changes. */
static void load_game_options(LibretroHost* host, const std::string& game_basename)
{
    host->options_game.clear();
    host->options_game_loaded_for = game_basename;
    if (game_basename.empty()) return;
    std::ifstream f(game_options_file_for(host, game_basename));
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (!val.empty() && val.back() == '\r') val.pop_back();
        host->options_game[key] = val;
    }
    printf("Libretro: loaded %zu per-game overrides for '%s'\n",
           host->options_game.size(), game_basename.c_str());
}

static void save_game_options(LibretroHost* host)
{
    if (host->options_game_loaded_for.empty()) return;
    ensure_dir(game_options_dir_for(host));
    std::string path = game_options_file_for(host, host->options_game_loaded_for);
    if (host->options_game.empty()) {
        /* Nothing to persist — leave any stale file alone. (Removing it would
         * silently discard inherits-only configs that the user might want
         * back later.) */
        return;
    }
    std::ofstream f(path);
    if (!f.is_open()) return;
    for (const auto& kv : host->options_game)
        f << kv.first << '=' << kv.second << '\n';
}

/* ========================================================================
 * SEH-wrapped helpers
 * ======================================================================== */

namespace {

void* SafeLoadModule(const char* path)
{ void* h = NULL; LIBRETRO_TRY { h = SDL_LoadObject(path); } LIBRETRO_EXCEPT(h = NULL); return h; }

unsigned SafeApiVersion(retro_api_version_t fn)
{ unsigned v = 0; LIBRETRO_TRY { v = fn(); } LIBRETRO_EXCEPT(v = 0); return v; }

bool SafeCallVoid(void (*fn)(void))
{ bool ok = false; LIBRETRO_TRY { fn(); ok = true; } LIBRETRO_EXCEPT(ok = false); return ok; }

bool SafeSetEnv(retro_set_environment_t fn, retro_environment_t cb)
{ bool ok = false; LIBRETRO_TRY { fn(cb); ok = true; } LIBRETRO_EXCEPT(ok = false); return ok; }

bool SafeSetVideo(retro_set_video_refresh_t fn, retro_video_refresh_t cb)
{ bool ok = false; LIBRETRO_TRY { fn(cb); ok = true; } LIBRETRO_EXCEPT(ok = false); return ok; }

bool SafeSetAudioSample(retro_set_audio_sample_t fn, retro_audio_sample_t cb)
{ bool ok = false; LIBRETRO_TRY { fn(cb); ok = true; } LIBRETRO_EXCEPT(ok = false); return ok; }

bool SafeSetAudioBatch(retro_set_audio_sample_batch_t fn, retro_audio_sample_batch_t cb)
{ bool ok = false; LIBRETRO_TRY { fn(cb); ok = true; } LIBRETRO_EXCEPT(ok = false); return ok; }

bool SafeSetInputPoll(retro_set_input_poll_t fn, retro_input_poll_t cb)
{ bool ok = false; LIBRETRO_TRY { fn(cb); ok = true; } LIBRETRO_EXCEPT(ok = false); return ok; }

bool SafeSetInputState(retro_set_input_state_t fn, retro_input_state_t cb)
{ bool ok = false; LIBRETRO_TRY { fn(cb); ok = true; } LIBRETRO_EXCEPT(ok = false); return ok; }

bool SafeGetSystemInfo(retro_get_system_info_t fn, struct retro_system_info* info)
{ bool ok = false; LIBRETRO_TRY { fn(info); ok = true; } LIBRETRO_EXCEPT(ok = false); return ok; }

bool SafeGetSystemAvInfo(retro_get_system_av_info_t fn, struct retro_system_av_info* info)
{ bool ok = false; LIBRETRO_TRY { fn(info); ok = true; } LIBRETRO_EXCEPT(ok = false); return ok; }

bool SafeLoadGame(retro_load_game_t fn, const struct retro_game_info* game)
{ bool ok = false; LIBRETRO_TRY { ok = fn(game); } LIBRETRO_EXCEPT(ok = false); return ok; }

bool SafeRunCore(retro_run_t fn)
{ bool ok = false; LIBRETRO_TRY { fn(); ok = true; } LIBRETRO_EXCEPT(ok = false); return ok; }

size_t SafeSerializeSize(retro_serialize_size_t fn)
{ size_t s = 0; LIBRETRO_TRY { s = fn(); } LIBRETRO_EXCEPT(s = 0); return s; }

bool SafeSerialize(retro_serialize_t fn, void* data, size_t size)
{ bool ok = false; LIBRETRO_TRY { ok = fn(data, size); } LIBRETRO_EXCEPT(ok = false); return ok; }

bool SafeUnserialize(retro_unserialize_t fn, const void* data, size_t size)
{ bool ok = false; LIBRETRO_TRY { ok = fn(data, size); } LIBRETRO_EXCEPT(ok = false); return ok; }

/* SEH-wrapped invocations of the core's HW context lifecycle callbacks.
 * Kept in standalone functions because callers (worker_do_load_game,
 * worker_thread_main shutdown) have C++ locals with destructors that __try
 * cannot coexist with under MSVC. */
bool SafeContextReset(retro_hw_context_reset_t fn)
{ bool ok = false; LIBRETRO_TRY { fn(); ok = true; } LIBRETRO_EXCEPT(ok = false); return ok; }

bool SafeContextDestroy(retro_hw_context_reset_t fn)
{ bool ok = false; LIBRETRO_TRY { fn(); ok = true; } LIBRETRO_EXCEPT(ok = false); return ok; }

void* SafeGetMemoryData(retro_get_memory_data_t fn, unsigned id)
{ void* p = NULL; LIBRETRO_TRY { p = fn(id); } LIBRETRO_EXCEPT(p = NULL); return p; }

size_t SafeGetMemorySize(retro_get_memory_size_t fn, unsigned id)
{ size_t s = 0; LIBRETRO_TRY { s = fn(id); } LIBRETRO_EXCEPT(s = 0); return s; }

} /* anonymous namespace */

/* ========================================================================
 * HW render — GL function loading, FBO management, callbacks for the core
 *
 * Lifecycle (worker thread, except where noted):
 *   SET_HW_RENDER (env)        → stash callback, mark hw_render_pending
 *   request_gl_context_blocking → ask engine pump to create context, block
 *   activate context           → make current, load GL fn pointers
 *   (... retro_init / retro_load_game ...)
 *   ensure_fbo at av_info dims → create / resize backing FBO
 *   hw_cb.context_reset()      → core sets up its GL state
 *   retro_run loops            → core writes FBO; cbVideoRefresh glReadPixels
 *   shutdown                   → context_destroy, free FBO, destroy context
 * ======================================================================== */

/* Note: cores that go through the libretro 'glsm' GL state manager (e.g.
 * parallel-n64) only call this once during their context_reset; thereafter
 * they rely on glsm intercepting glBindFramebuffer(target, 0) and redirecting
 * to the cached fbo. Cores whose GFX plugins never bind glBindFramebuffer at
 * all (parallel-n64's Glide64) won't actually draw to our fbo even though we
 * returned its id correctly — there's no glsm-equivalent shim on our side. */
static uintptr_t RETRO_CALLCONV hw_get_current_framebuffer(void)
{
    LibretroHost* host = LibretroManager_FindByThread();
    return host ? (uintptr_t)host->fbo : 0;
}

static retro_proc_address_t RETRO_CALLCONV hw_get_proc_address(const char* sym)
{
    return (retro_proc_address_t)load_gl_proc(sym);
}

static void load_fbo_gl_functions(LibretroHost* host)
{
    /* These exist on any GL 3.0 context, or any 2.x with EXT_framebuffer_object;
     * load_gl_proc returns the appropriate impl. NULL here would mean the
     * active context can't do FBOs at all — let the loaded pointers stay null
     * and the create_fbo path will refuse. */
    host->glGenFramebuffersFn        = (PFNGLGENFRAMEBUFFERSPROC)        load_gl_proc("glGenFramebuffers");
    host->glDeleteFramebuffersFn     = (PFNGLDELETEFRAMEBUFFERSPROC)     load_gl_proc("glDeleteFramebuffers");
    host->glBindFramebufferFn        = (PFNGLBINDFRAMEBUFFERPROC)        load_gl_proc("glBindFramebuffer");
    host->glFramebufferTexture2DFn   = (PFNGLFRAMEBUFFERTEXTURE2DPROC)   load_gl_proc("glFramebufferTexture2D");
    host->glCheckFramebufferStatusFn = (PFNGLCHECKFRAMEBUFFERSTATUSPROC) load_gl_proc("glCheckFramebufferStatus");
    host->glGenRenderbuffersFn       = (PFNGLGENRENDERBUFFERSPROC)       load_gl_proc("glGenRenderbuffers");
    host->glDeleteRenderbuffersFn    = (PFNGLDELETERENDERBUFFERSPROC)    load_gl_proc("glDeleteRenderbuffers");
    host->glBindRenderbufferFn       = (PFNGLBINDRENDERBUFFERPROC)       load_gl_proc("glBindRenderbuffer");
    host->glRenderbufferStorageFn    = (PFNGLRENDERBUFFERSTORAGEPROC)    load_gl_proc("glRenderbufferStorage");
    host->glFramebufferRenderbufferFn= (PFNGLFRAMEBUFFERRENDERBUFFERPROC)load_gl_proc("glFramebufferRenderbuffer");
}

static void destroy_fbo(LibretroHost* host)
{
    if (host->fbo_depth_rb && host->glDeleteRenderbuffersFn) {
        host->glDeleteRenderbuffersFn(1, &host->fbo_depth_rb);
        host->fbo_depth_rb = 0;
    }
    if (host->fbo_color_tex) {
        glDeleteTextures(1, &host->fbo_color_tex);
        host->fbo_color_tex = 0;
    }
    if (host->fbo && host->glDeleteFramebuffersFn) {
        host->glDeleteFramebuffersFn(1, &host->fbo);
        host->fbo = 0;
    }
    host->fbo_w = host->fbo_h = 0;
}

/* Create or resize the backing FBO. width/height must be > 0. */
static bool ensure_fbo(LibretroHost* host, int w, int h)
{
    if (w <= 0 || h <= 0) return false;
    if (!host->glGenFramebuffersFn || !host->glBindFramebufferFn ||
        !host->glFramebufferTexture2DFn || !host->glCheckFramebufferStatusFn) {
        printf("Libretro: HW render FBO functions unavailable\n");
        return false;
    }
    if (host->fbo && host->fbo_w == w && host->fbo_h == h) return true;

    destroy_fbo(host);

    host->glGenFramebuffersFn(1, &host->fbo);
    host->glBindFramebufferFn(GL_FRAMEBUFFER, host->fbo);

    glGenTextures(1, &host->fbo_color_tex);
    glBindTexture(GL_TEXTURE_2D, host->fbo_color_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    host->glFramebufferTexture2DFn(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, host->fbo_color_tex, 0);

    if ((host->hw_cb.depth || host->hw_cb.stencil) && host->glGenRenderbuffersFn) {
        host->glGenRenderbuffersFn(1, &host->fbo_depth_rb);
        host->glBindRenderbufferFn(GL_RENDERBUFFER, host->fbo_depth_rb);
        if (host->hw_cb.depth && host->hw_cb.stencil) {
            host->glRenderbufferStorageFn(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
            host->glFramebufferRenderbufferFn(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, host->fbo_depth_rb);
        } else {
            host->glRenderbufferStorageFn(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
            host->glFramebufferRenderbufferFn(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, host->fbo_depth_rb);
        }
    }

    GLenum status = host->glCheckFramebufferStatusFn(GL_FRAMEBUFFER);
    host->glBindFramebufferFn(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        printf("Libretro: FBO incomplete (0x%x) at %dx%d\n", status, w, h);
        destroy_fbo(host);
        return false;
    }
    host->fbo_w = w;
    host->fbo_h = h;
    printf("Libretro: HW FBO ready (%dx%d, depth=%d, stencil=%d, fbo_id=%u)\n",
           w, h, host->hw_cb.depth ? 1 : 0, host->hw_cb.stencil ? 1 : 0, host->fbo);
    return true;
}

/* Worker → engine handshake to create the shared GL context. Blocks the worker
 * until the engine's pump (running in libretro_host_create / load_game) services
 * the request and posts hw_create_done_sem. */
static bool request_gl_context_blocking(LibretroHost* host)
{
    if (!g_host.libretro_create_gl_context) {
        printf("Libretro: host has no GL-context callback — HW rendering unavailable\n");
        return false;
    }
    SDL_AtomicSet(&host->hw_create_request, 1);
    /* The engine thread sees the flag from inside its blocking-wait pump. */
    SDL_SemWait(host->hw_create_done_sem);
    return host->hw_gl_context != nullptr;
}

/* Engine-thread pump: services any pending GL-context creation request from
 * the worker. Called repeatedly while libretro_host_create / load_game wait
 * for the corresponding init_done_sem / load_done_sem. */
static void pump_pending_gl_request(LibretroHost* host)
{
    if (!SDL_AtomicGet(&host->hw_create_request)) return;
    SDL_AtomicSet(&host->hw_create_request, 0);

    int profile_mask = 0;
    int major = (int)host->hw_cb.version_major;
    int minor = (int)host->hw_cb.version_minor;
    if (host->hw_cb.context_type == RETRO_HW_CONTEXT_OPENGL_CORE) {
        profile_mask = SDL_GL_CONTEXT_PROFILE_CORE;
    } else if (host->hw_cb.context_type == RETRO_HW_CONTEXT_OPENGL) {
        /* Compat profile. Many cores (mupen64plus-next/GLideN64, etc.) report
         * ver=0.0 but actually need 3.3+ — default high so they don't fail
         * during context_reset on extension queries. */
        if (major == 0) { major = 3; minor = 3; }
    } else if (host->hw_cb.context_type == RETRO_HW_CONTEXT_OPENGLES2) {
        profile_mask = SDL_GL_CONTEXT_PROFILE_ES;
        if (major == 0) { major = 2; minor = 0; }
    } else if (host->hw_cb.context_type == RETRO_HW_CONTEXT_OPENGLES3) {
        profile_mask = SDL_GL_CONTEXT_PROFILE_ES;
        if (major == 0) { major = 3; minor = 0; }
    } else if (host->hw_cb.context_type == RETRO_HW_CONTEXT_OPENGLES_VERSION) {
        profile_mask = SDL_GL_CONTEXT_PROFILE_ES;
    }

    host->hw_gl_context = g_host.libretro_create_gl_context(major, minor, profile_mask,
                                                            host->hw_cb.depth ? 1 : 0,
                                                            host->hw_cb.stencil ? 1 : 0);
    SDL_SemPost(host->hw_create_done_sem);
}

/* ========================================================================
 * Logging callback
 * ======================================================================== */

static const char* log_level_str(enum retro_log_level lvl)
{
    switch (lvl) {
        case RETRO_LOG_DEBUG: return "DEBUG";
        case RETRO_LOG_INFO:  return "INFO";
        case RETRO_LOG_WARN:  return "WARN";
        case RETRO_LOG_ERROR: return "ERROR";
        default:              return "?";
    }
}

static void RETRO_CALLCONV libretro_log_callback(enum retro_log_level level, const char* fmt, ...)
{
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    size_t n = strlen(msg);
    while (n && (msg[n - 1] == '\n' || msg[n - 1] == '\r')) msg[--n] = '\0';
    printf("[libretro:%s] %s\n", log_level_str(level), msg);
}

/* ========================================================================
 * Core option ingestion
 * ======================================================================== */

static void ingest_options_v0(LibretroHost* host, const struct retro_variable* vars)
{
    SDL_LockMutex(host->options_lock);
    host->option_defs.clear();
    if (vars) {
        for (; vars->key; ++vars) {
            CoreOptionDef def;
            def.key = vars->key;
            if (vars->value) {
                std::string value_field = vars->value;
                size_t semi = value_field.find("; ");
                if (semi != std::string::npos) {
                    def.display = value_field.substr(0, semi);
                    std::string vlist = value_field.substr(semi + 2);
                    std::stringstream ss(vlist);
                    std::string item;
                    while (std::getline(ss, item, '|')) def.values.push_back(item);
                } else {
                    def.display = value_field;
                }
            }
            if (!def.values.empty()) def.default_value = def.values[0];
            host->option_defs.push_back(std::move(def));
        }
    }
    size_t n = host->option_defs.size();
    SDL_UnlockMutex(host->options_lock);
    printf("Libretro: ingested %zu options (v0/SET_VARIABLES)\n", n);
}

static void ingest_options_v1(LibretroHost* host, const struct retro_core_option_definition* defs)
{
    SDL_LockMutex(host->options_lock);
    host->option_defs.clear();
    if (defs) {
        for (; defs->key; ++defs) {
            CoreOptionDef def;
            def.key = defs->key;
            def.display = defs->desc ? defs->desc : "";
            def.default_value = defs->default_value ? defs->default_value : "";
            for (unsigned i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX; ++i) {
                if (!defs->values[i].value) break;
                def.values.push_back(defs->values[i].value);
            }
            host->option_defs.push_back(std::move(def));
        }
    }
    size_t n = host->option_defs.size();
    SDL_UnlockMutex(host->options_lock);
    printf("Libretro: ingested %zu options (v1/SET_CORE_OPTIONS)\n", n);
}

static void ingest_options_v2(LibretroHost* host, const struct retro_core_options_v2* opts_v2)
{
    SDL_LockMutex(host->options_lock);
    host->option_defs.clear();
    if (opts_v2 && opts_v2->definitions) {
        const struct retro_core_option_v2_definition* defs = opts_v2->definitions;
        for (; defs->key; ++defs) {
            CoreOptionDef def;
            def.key = defs->key;
            def.display = defs->desc ? defs->desc : "";
            def.default_value = defs->default_value ? defs->default_value : "";
            for (unsigned i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX; ++i) {
                if (!defs->values[i].value) break;
                def.values.push_back(defs->values[i].value);
            }
            host->option_defs.push_back(std::move(def));
        }
    }
    size_t n = host->option_defs.size();
    SDL_UnlockMutex(host->options_lock);
    printf("Libretro: ingested %zu options (v2/SET_CORE_OPTIONS_V2)\n", n);
}

/* Caller must hold options_lock. */
static std::string resolve_option_value_locked(LibretroHost* host, const std::string& key)
{
    /* Game-tier override wins if set. */
    auto gi = host->options_game.find(key);
    if (gi != host->options_game.end() && !gi->second.empty()) return gi->second;
    /* Then core-tier user value. */
    auto ci = host->options_current.find(key);
    if (ci != host->options_current.end()) return ci->second;
    /* Fall back to the core-declared default; final fallback is first declared value. */
    for (const auto& def : host->option_defs) {
        if (def.key == key) {
            if (!def.default_value.empty()) return def.default_value;
            if (!def.values.empty()) return def.values[0];
            return "";
        }
    }
    return "";
}

/* Find a content override matching `ext` (lowercase, no dot).
 * On match, fills out_need_fullpath/out_persistent and returns true. */
static bool apply_content_override(LibretroHost* host, const std::string& ext,
                                   bool& out_need_fullpath, bool& out_persistent)
{
    if (ext.empty()) return false;
    for (const auto& ov : host->content_overrides) {
        /* ov.extensions is "md|gg|sg" — walk pipe-separated tokens. */
        const std::string& list = ov.extensions;
        size_t pos = 0;
        while (pos < list.size()) {
            size_t bar = list.find('|', pos);
            std::string tok = (bar == std::string::npos) ? list.substr(pos)
                                                          : list.substr(pos, bar - pos);
            if (tok == ext) {
                out_need_fullpath = ov.need_fullpath;
                out_persistent    = ov.persistent_data;
                return true;
            }
            if (bar == std::string::npos) break;
            pos = bar + 1;
        }
    }
    return false;
}

/* ========================================================================
 * Environment callback
 * ======================================================================== */

static bool host_environment_callback(unsigned cmd, void* data)
{
    LibretroHost* host = LibretroManager_FindByThread();
    if (!host) return false;

    unsigned base = cmd & ~RETRO_ENVIRONMENT_EXPERIMENTAL;

    switch (base) {
        case RETRO_ENVIRONMENT_GET_OVERSCAN:
            if (data) *(bool*)data = false;
            return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            if (data) *(bool*)data = true;
            return true;

        case RETRO_ENVIRONMENT_SET_MESSAGE: {
            const struct retro_message* m = (const struct retro_message*)data;
            if (m && m->msg) printf("Libretro: [msg] %s\n", m->msg);
            return true;
        }
        case 60: {
            const struct retro_message_ext* m = (const struct retro_message_ext*)data;
            if (m && m->msg) printf("Libretro: [msg-ext] %s\n", m->msg);
            return true;
        }

        case RETRO_ENVIRONMENT_SHUTDOWN:
            printf("Libretro: core requested SHUTDOWN\n");
            SDL_AtomicSet(&host->core_crashed, 1);
            return true;

        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
            return true;

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: {
            std::string p = sys_dir_for(host);
            ensure_dir(p);
            *(const char**)data = publish_string(host, p);
            return true;
        }
        case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH:
            *(const char**)data = publish_string(host, host->core_full_path);
            return true;
        case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY: {
            std::string p = content_dir_for(host);
            ensure_dir(p);
            *(const char**)data = publish_string(host, p);
            return true;
        }
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: {
            std::string p = save_dir_for(host);
            ensure_dir(p);
            *(const char**)data = publish_string(host, p);
            return true;
        }

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            enum retro_pixel_format fmt = *(enum retro_pixel_format*)data;
            if (fmt == RETRO_PIXEL_FORMAT_RGB565 ||
                fmt == RETRO_PIXEL_FORMAT_XRGB8888 ||
                fmt == RETRO_PIXEL_FORMAT_0RGB1555) {
                host->pixel_format = fmt;
                const char* name = (fmt == RETRO_PIXEL_FORMAT_RGB565) ? "RGB565"
                                 : (fmt == RETRO_PIXEL_FORMAT_XRGB8888) ? "XRGB8888"
                                 : "0RGB1555";
                printf("Libretro: Pixel format set to %s\n", name);
                return true;
            }
            printf("Libretro: Unsupported pixel format %d\n", fmt);
            return false;
        }

        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
            return true;
        case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:
            return false;
        case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
            return false;
        case RETRO_ENVIRONMENT_SET_HW_RENDER: {
            struct retro_hw_render_callback* cb = (struct retro_hw_render_callback*)data;
            if (!cb) return false;
            if (cb->context_type == RETRO_HW_CONTEXT_NONE ||
                cb->context_type == RETRO_HW_CONTEXT_VULKAN) {
                printf("Libretro: HW render context_type=%d unsupported\n", (int)cb->context_type);
                return false;
            }
            if (!g_host.libretro_create_gl_context) {
                printf("Libretro: host has no GL-context callback — refusing HW render\n");
                return false;
            }
            host->hw_cb = *cb;
            /* Fill in the frontend-provided callbacks the core will use. */
            host->hw_cb.get_current_framebuffer = hw_get_current_framebuffer;
            host->hw_cb.get_proc_address = hw_get_proc_address;
            cb->get_current_framebuffer = hw_get_current_framebuffer;
            cb->get_proc_address = hw_get_proc_address;
            host->hw_render_pending = true;
            printf("Libretro: SET_HW_RENDER accepted (type=%d ver=%u.%u depth=%d stencil=%d)\n",
                   (int)cb->context_type, cb->version_major, cb->version_minor,
                   cb->depth ? 1 : 0, cb->stencil ? 1 : 0);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: {
            const struct retro_controller_info* p = (const struct retro_controller_info*)data;
            if (!p) return true;
            unsigned port = 0;
            for (; p[port].types; ++port) { }
            printf("Libretro: SET_CONTROLLER_INFO ports=%u\n", port);
            return true;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            struct retro_variable* v = (struct retro_variable*)data;
            if (!v || !v->key) return false;
            std::string val;
            {
                SDL_LockMutex(host->options_lock);
                val = resolve_option_value_locked(host, v->key);
                host->options_have_changed = false;
                SDL_UnlockMutex(host->options_lock);
            }
            if (val.empty()) {
                v->value = nullptr;
                printf("Libretro: GET_VARIABLE: key '%s' has no value\n", v->key);
                return true;
            }
            v->value = publish_string(host, val);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_VARIABLES:
            ingest_options_v0(host, (const struct retro_variable*)data);
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: {
            SDL_LockMutex(host->options_lock);
            *(bool*)data = host->options_have_changed;
            host->options_have_changed = false;
            SDL_UnlockMutex(host->options_lock);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_VARIABLE: {
            const struct retro_variable* v = (const struct retro_variable*)data;
            if (!v || !v->key || !v->value) return false;
            SDL_LockMutex(host->options_lock);
            host->options_current[v->key] = v->value;
            host->options_have_changed = true;
            SDL_UnlockMutex(host->options_lock);
            return true;
        }
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            *(unsigned*)data = 2;
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
            ingest_options_v1(host, (const struct retro_core_option_definition*)data);
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
            const struct retro_core_options_intl* i = (const struct retro_core_options_intl*)data;
            ingest_options_v1(host, i ? i->us : nullptr);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
            ingest_options_v2(host, (const struct retro_core_options_v2*)data);
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
            const struct retro_core_options_v2_intl* i = (const struct retro_core_options_v2_intl*)data;
            ingest_options_v2(host, i ? i->us : nullptr);
            return true;
        }
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_UPDATE_DISPLAY_CALLBACK:
            return false;

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: {
            struct retro_log_callback* cb = (struct retro_log_callback*)data;
            cb->log = libretro_log_callback;
            return true;
        }
        case RETRO_ENVIRONMENT_GET_PERF_INTERFACE: {
            struct retro_perf_callback* cb = (struct retro_perf_callback*)data;
            memset(cb, 0, sizeof(*cb));
            return false;
        }
        case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE: {
            struct retro_rumble_interface* iface = (struct retro_rumble_interface*)data;
            iface->set_rumble_state = nullptr;
            return false;
        }
        case 59:
            *(unsigned*)data = 1;
            return true;

        case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
            const struct retro_system_av_info* avi = (const struct retro_system_av_info*)data;
            if (!avi) return false;
            host->av_info = *avi;
            host->resample_phase = 0.0;
            host->single_sample_accum = 0.0;
            printf("Libretro: SET_SYSTEM_AV_INFO fps=%.2f rate=%.0f base=%ux%u\n",
                avi->timing.fps, avi->timing.sample_rate,
                avi->geometry.base_width, avi->geometry.base_height);
            /* Phase 5d: resize FBO if max geometry now exceeds current. */
            if (host->hw_render_active && host->fbo) {
                unsigned new_w = avi->geometry.max_width;
                unsigned new_h = avi->geometry.max_height;
                if (new_w > 0 && new_h > 0 &&
                    ((int)new_w > host->fbo_w || (int)new_h > host->fbo_h)) {
                    printf("Libretro: HW path — resizing FBO %dx%d -> %ux%u (SET_SYSTEM_AV_INFO)\n",
                           host->fbo_w, host->fbo_h, new_w, new_h);
                    if (ensure_fbo(host, (int)new_w, (int)new_h) && host->hw_cb.context_reset) {
                        (void)SafeContextReset(host->hw_cb.context_reset);
                    }
                }
            }
            return true;
        }
        case RETRO_ENVIRONMENT_SET_GEOMETRY: {
            const struct retro_game_geometry* g = (const struct retro_game_geometry*)data;
            if (!g) return false;
            host->av_info.geometry = *g;
            /* Phase 5d: resize FBO if max geometry now exceeds current. */
            if (host->hw_render_active && host->fbo) {
                unsigned new_w = g->max_width;
                unsigned new_h = g->max_height;
                if (new_w > 0 && new_h > 0 &&
                    ((int)new_w > host->fbo_w || (int)new_h > host->fbo_h)) {
                    printf("Libretro: HW path — resizing FBO %dx%d -> %ux%u (SET_GEOMETRY)\n",
                           host->fbo_w, host->fbo_h, new_w, new_h);
                    if (ensure_fbo(host, (int)new_w, (int)new_h) && host->hw_cb.context_reset) {
                        (void)SafeContextReset(host->hw_cb.context_reset);
                    }
                }
            }
            return true;
        }

        case 47:
            if (data) *(int*)data = (1 << 0) | (1 << 1);
            return true;
        case 49:
            if (data) *(bool*)data = false;
            return true;

        case 62:
            return false;
        case 63:
            return true;

        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            return true;

        case 56:
            *(unsigned*)data = RETRO_HW_CONTEXT_OPENGL;
            return true;

        case 36: {
            const struct retro_memory_map* mm = (const struct retro_memory_map*)data;
            if (mm) printf("Libretro: SET_MEMORY_MAPS descriptors=%u\n", mm->num_descriptors);
            return true;
        }

        case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE: {       /* 65 */
            host->content_overrides.clear();
            const struct retro_system_content_info_override* arr =
                (const struct retro_system_content_info_override*)data;
            if (!arr) {
                /* NULL data is a support probe — true means we'll honor overrides
                 * AND game_info_ext when the core sends them later. */
                return true;
            }
            for (; arr->extensions; ++arr) {
                ContentOverrideEntry e;
                /* Lowercase extensions string for case-insensitive matching. */
                e.extensions.reserve(strlen(arr->extensions));
                for (const char* p = arr->extensions; *p; ++p)
                    e.extensions.push_back((char)std::tolower((unsigned char)*p));
                e.need_fullpath   = arr->need_fullpath;
                e.persistent_data = arr->persistent_data;
                host->content_overrides.push_back(std::move(e));
                printf("Libretro: content override: ext='%s' need_fullpath=%d persistent_data=%d\n",
                       host->content_overrides.back().extensions.c_str(),
                       e.need_fullpath ? 1 : 0, e.persistent_data ? 1 : 0);
            }
            return true;
        }

        case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT: {              /* 66 */
            if (!data) {
                /* Probe — same true-means-supported rule. */
                return true;
            }
            /* If called before load_game, we have no metadata to return. */
            if (host->loaded_full_path.empty()) {
                *(const struct retro_game_info_ext**)data = nullptr;
                return false;
            }
            struct retro_game_info_ext* ext = new (std::nothrow) retro_game_info_ext();
            if (!ext) return false;
            memset(ext, 0, sizeof(*ext));
            ext->full_path       = host->loaded_full_path.empty() ? nullptr : host->loaded_full_path.c_str();
            ext->archive_path    = host->loaded_archive_path.empty() ? nullptr : host->loaded_archive_path.c_str();
            ext->archive_file    = host->loaded_archive_file.empty() ? nullptr : host->loaded_archive_file.c_str();
            ext->dir             = host->loaded_dir.empty() ? nullptr : host->loaded_dir.c_str();
            ext->name            = host->loaded_name.empty() ? nullptr : host->loaded_name.c_str();
            ext->ext             = host->loaded_ext.empty() ? nullptr : host->loaded_ext.c_str();
            ext->meta            = nullptr;
            ext->data            = host->loaded_data;
            ext->size            = host->loaded_data_size;
            ext->file_in_archive = host->loaded_file_in_archive;
            ext->persistent_data = host->loaded_persistent_data;
            host->allocated_game_info_ext.push_back(ext);
            *(const struct retro_game_info_ext**)data = ext;
            printf("Libretro: GET_GAME_INFO_EXT — full=%s archive=%s entry=%s in_archive=%d persistent=%d\n",
                   ext->full_path ? ext->full_path : "(null)",
                   ext->archive_path ? ext->archive_path : "(null)",
                   ext->archive_file ? ext->archive_file : "(null)",
                   ext->file_in_archive ? 1 : 0, ext->persistent_data ? 1 : 0);
            return true;
        }

        default:
            printf("Libretro: ignored env command #%u (base #%u)\n", cmd, base);
            return false;
    }
}

/* ========================================================================
 * Video callback
 * ======================================================================== */

static void host_video_refresh_callback(const void* data, unsigned width,
                                        unsigned height, size_t pitch)
{
    LibretroHost* host = LibretroManager_FindByThread();
    if (!host) return;
    if (!data) return;

    /* HW path: data == RETRO_HW_FRAME_BUFFER_VALID means the core just rendered
     * to our FBO. Read it back as BGRA into the back buffer; LibretroInstance's
     * 32 bpp branch already consumes XRGB8888-shaped data correctly via the
     * existing pixel_format = XRGB8888 hint. */
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        if (!host->fbo || !host->glBindFramebufferFn) return;
        size_t bgra_pitch = (size_t)width * 4;
        size_t needed = (size_t)height * bgra_pitch;
        if (host->video_frame_back_capacity < needed) {
            void* nb = realloc(host->video_frame_back, needed);
            if (!nb) return;
            host->video_frame_back = nb;
            host->video_frame_back_capacity = needed;
        }
        host->glBindFramebufferFn(GL_READ_FRAMEBUFFER, host->fbo);
        glReadPixels(0, 0, (GLsizei)width, (GLsizei)height,
                     GL_BGRA, GL_UNSIGNED_BYTE, host->video_frame_back);
        host->glBindFramebufferFn(GL_READ_FRAMEBUFFER, 0);

        /* If the core uses bottom-left origin (typical OpenGL convention) we
         * need to flip vertically so the engine renders right-side-up. */
        if (host->hw_cb.bottom_left_origin && height > 1) {
            std::vector<uint8_t> tmp(bgra_pitch);
            uint8_t* p = (uint8_t*)host->video_frame_back;
            for (unsigned y = 0; y < height / 2; ++y) {
                uint8_t* a = p + y * bgra_pitch;
                uint8_t* b = p + (height - 1 - y) * bgra_pitch;
                memcpy(tmp.data(), a, bgra_pitch);
                memcpy(a, b, bgra_pitch);
                memcpy(b, tmp.data(), bgra_pitch);
            }
        }
        host->frame_width  = width;
        host->frame_height = height;
        host->frame_pitch  = bgra_pitch;
        /* Force the engine to interpret these pixels as XRGB8888. The existing
         * 32 bpp render branch swaps R/B back to BGRA for the overlay path; for
         * the in-world path it converts to RGB565. */
        host->pixel_format = RETRO_PIXEL_FORMAT_XRGB8888;
        SDL_AtomicSet(&host->frame_updated, 1);
        return;
    }

    /* Software path. */
    size_t needed_size = height * pitch;
    if (needed_size == 0) return;

    if (host->video_frame_back_capacity < needed_size) {
        void* nb = realloc(host->video_frame_back, needed_size);
        if (!nb) return;
        host->video_frame_back = nb;
        host->video_frame_back_capacity = needed_size;
    }

    memcpy(host->video_frame_back, data, needed_size);
    host->frame_width = width;
    host->frame_height = height;
    host->frame_pitch = pitch;
    SDL_AtomicSet(&host->frame_updated, 1);
}

/* ========================================================================
 * Audio: ring buffer + resampler
 * ======================================================================== */

static size_t audio_ring_available(LibretroHost* host)
{
    if (!host->audio_ring_buf) return 0;
    size_t w = host->audio_ring_write.load(std::memory_order_acquire);
    size_t r = host->audio_ring_read.load(std::memory_order_acquire);
    if (w >= r) return w - r;
    return host->audio_ring_size - r + w;
}

static size_t audio_ring_free(LibretroHost* host)
{
    if (!host->audio_ring_buf) return 0;
    return host->audio_ring_size - 1 - audio_ring_available(host);
}

/* Push one (left, right) interleaved pair to the ring. Returns false if no room. */
static bool ring_push_pair(LibretroHost* host, int16_t l, int16_t r)
{
    if (audio_ring_free(host) < 2) return false;
    size_t w = host->audio_ring_write.load(std::memory_order_relaxed);
    host->audio_ring_buf[w] = l;
    w = (w + 1) % host->audio_ring_size;
    host->audio_ring_buf[w] = r;
    w = (w + 1) % host->audio_ring_size;
    host->audio_ring_write.store(w, std::memory_order_release);
    return true;
}

/* Single-sample callback: source rate may be any rate; we decimate or duplicate
 * with a fractional accumulator.
 *
 * For source_rate <= target_rate: emit current sample plus enough duplicates so
 * that on average target/source samples are produced per input.
 * For source_rate > target_rate: only emit when accumulator carries a whole step.
 */
static void host_audio_sample_callback(int16_t left, int16_t right)
{
    LibretroHost* host = LibretroManager_FindByThread();
    if (!host || !host->audio_ring_buf) return;

    double src_rate = host->av_info.timing.sample_rate;
    if (src_rate <= 0.0) src_rate = LIBRETRO_TARGET_AUDIO_RATE;
    /* step = how many output samples this input sample contributes. */
    double step = (double)LIBRETRO_TARGET_AUDIO_RATE / src_rate;
    host->single_sample_accum += step;
    while (host->single_sample_accum >= 1.0) {
        if (!ring_push_pair(host, left, right)) {
            host->single_sample_accum = 0.0;
            return;
        }
        host->single_sample_accum -= 1.0;
    }
}

/* Batch callback: linear-interpolation resampler. Source rate → 48 kHz.
 * Carries fractional source position between calls so SameBoy at 2 MHz works
 * across batch boundaries with no audible seams. */
static size_t host_audio_sample_batch_callback(const int16_t* data, size_t frames)
{
    LibretroHost* host = LibretroManager_FindByThread();
    if (!host || !host->audio_ring_buf || !data || frames == 0) return frames;

    double src_rate = host->av_info.timing.sample_rate;
    if (src_rate <= 0.0) src_rate = LIBRETRO_TARGET_AUDIO_RATE;

    if (src_rate == (double)LIBRETRO_TARGET_AUDIO_RATE) {
        /* Pass-through. */
        size_t pairs = frames;
        for (size_t i = 0; i < pairs; ++i) {
            if (!ring_push_pair(host, data[i*2], data[i*2 + 1])) break;
        }
        return frames;
    }

    /* Linear interpolation, mirroring aalibretro c_libretroinstance.cpp:4217–4274.
     * srcPos walks through the input buffer in fractional steps of (src/dst).
     * For each output sample we read floor(srcPos) and floor(srcPos)+1 and
     * blend by the fractional part. */
    double step = src_rate / (double)LIBRETRO_TARGET_AUDIO_RATE;
    double srcPos = host->resample_phase;
    /* We can read up to index (frames - 1) safely (since we read +1 inside). */
    while (srcPos < (double)frames - 1.0) {
        size_t idx = (size_t)srcPos;
        double frac = srcPos - (double)idx;
        double l0 = (double)data[idx * 2 + 0];
        double r0 = (double)data[idx * 2 + 1];
        double l1 = (double)data[(idx + 1) * 2 + 0];
        double r1 = (double)data[(idx + 1) * 2 + 1];
        double lf = l0 * (1.0 - frac) + l1 * frac;
        double rf = r0 * (1.0 - frac) + r1 * frac;
        if (lf > 32767.0) lf = 32767.0; else if (lf < -32768.0) lf = -32768.0;
        if (rf > 32767.0) rf = 32767.0; else if (rf < -32768.0) rf = -32768.0;
        if (!ring_push_pair(host, (int16_t)lf, (int16_t)rf)) {
            host->resample_phase = 0.0;
            return frames;
        }
        srcPos += step;
    }
    /* Carry the leftover fractional offset relative to the start of the next batch. */
    double phase = srcPos - (double)frames;
    if (phase < 0.0) phase = 0.0;
    host->resample_phase = phase;
    return frames;
}

/* ========================================================================
 * Input callbacks
 * ======================================================================== */

static void host_input_poll_callback(void) {}

static int16_t host_input_state_callback(unsigned port, unsigned device,
                                         unsigned index, unsigned id)
{
    LibretroHost* host = LibretroManager_FindByThread();
    if (!host || port > 1) return 0;
    if (device == RETRO_DEVICE_JOYPAD && index == 0)
        return (host->input_state[port] >> id) & 1;
    if (device == RETRO_DEVICE_ANALOG && index < 2 && id < 2)
        return host->analog_state[port][index][id];
    if (device == RETRO_DEVICE_KEYBOARD && id < 512)
        return host->keyboard_state[id] ? 1 : 0;
    return 0;
}

/* ========================================================================
 * Symbol resolution
 * ======================================================================== */

static bool load_core_symbols(LibretroHost* host)
{
#define LOAD_SYM(name) \
    host->name = (name##_t)SDL_LoadFunction(host->core_dll, #name); \
    if (!host->name) { \
        printf("Libretro: Failed to load symbol %s: %s\n", #name, SDL_GetError()); \
        return false; \
    }
#define LOAD_SYM_OPT(name) \
    host->name = (name##_t)SDL_LoadFunction(host->core_dll, #name);

    LOAD_SYM(retro_init);
    LOAD_SYM(retro_deinit);
    LOAD_SYM(retro_api_version);
    LOAD_SYM(retro_get_system_info);
    LOAD_SYM(retro_get_system_av_info);
    LOAD_SYM(retro_set_environment);
    LOAD_SYM(retro_set_video_refresh);
    LOAD_SYM(retro_set_audio_sample);
    LOAD_SYM(retro_set_audio_sample_batch);
    LOAD_SYM(retro_set_input_poll);
    LOAD_SYM(retro_set_input_state);
    LOAD_SYM(retro_set_controller_port_device);
    LOAD_SYM(retro_reset);
    LOAD_SYM(retro_run);
    LOAD_SYM(retro_load_game);
    LOAD_SYM(retro_unload_game);
    LOAD_SYM(retro_get_region);
    LOAD_SYM(retro_get_memory_data);
    LOAD_SYM(retro_get_memory_size);

    /* Optional — many cores omit these or implement them as no-ops. */
    LOAD_SYM_OPT(retro_serialize_size);
    LOAD_SYM_OPT(retro_serialize);
    LOAD_SYM_OPT(retro_unserialize);

#undef LOAD_SYM
#undef LOAD_SYM_OPT
    return true;
}

/* ========================================================================
 * Save state / SRAM helpers (worker-thread only)
 * ======================================================================== */

static void try_load_sram(LibretroHost* host)
{
    if (!host->retro_get_memory_data || !host->retro_get_memory_size) return;
    void* mem = SafeGetMemoryData(host->retro_get_memory_data, RETRO_MEMORY_SAVE_RAM);
    size_t sz = SafeGetMemorySize(host->retro_get_memory_size, RETRO_MEMORY_SAVE_RAM);
    if (!mem || sz == 0) return;

    std::ifstream f(sram_file_for(host), std::ios::binary | std::ios::ate);
    if (!f.is_open()) return;
    std::streamsize file_size = f.tellg();
    f.seekg(0, std::ios::beg);
    if ((size_t)file_size != sz) {
        printf("Libretro: SRAM size mismatch (file=%lld, mem=%zu) — skipping load\n",
               (long long)file_size, sz);
        return;
    }
    if (!f.read((char*)mem, sz)) {
        printf("Libretro: SRAM read failed\n");
        return;
    }
    printf("Libretro: SRAM loaded (%zu bytes)\n", sz);
}

static void try_save_sram(LibretroHost* host)
{
    if (!host->retro_get_memory_data || !host->retro_get_memory_size) return;
    void* mem = SafeGetMemoryData(host->retro_get_memory_data, RETRO_MEMORY_SAVE_RAM);
    size_t sz = SafeGetMemorySize(host->retro_get_memory_size, RETRO_MEMORY_SAVE_RAM);
    if (!mem || sz == 0) return;

    ensure_dir(save_dir_for(host));
    std::ofstream f(sram_file_for(host), std::ios::binary);
    if (!f.is_open()) {
        printf("Libretro: SRAM open-for-write failed\n");
        return;
    }
    f.write((const char*)mem, sz);
    printf("Libretro: SRAM saved (%zu bytes)\n", sz);
}

/* Read the .state file into host->pending_state_buf and queue an unserialize
 * attempt for the early frames. Some cores (mupen64plus-next) require the
 * first retro_run to fully initialize their internals (R4300 dynarec / RDRAM)
 * before retro_unserialize will accept the state — so we don't unserialize
 * here, just stage. The actual attempt happens in worker_thread_main's frame
 * loop. */
static void try_load_state(LibretroHost* host)
{
    if (!host->retro_unserialize) {
        printf("Libretro: state load skipped — core has no retro_unserialize\n");
        return;
    }
    std::string path = state_file_for(host);
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f.is_open()) {
        /* Normal on first run — no .state yet. */
        return;
    }
    std::streamsize file_size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (file_size <= 0) {
        printf("Libretro: state file '%s' is empty — skipping\n", path.c_str());
        return;
    }
    host->pending_state_buf.resize((size_t)file_size);
    if (!f.read(host->pending_state_buf.data(), file_size)) {
        printf("Libretro: state read failed for '%s'\n", path.c_str());
        host->pending_state_buf.clear();
        return;
    }
    /* Try ~10 frames of retries — covers cores that need 1-N frames of
     * warm-up before unserialize accepts. After 10 we log and give up. */
    host->pending_state_attempts_left = 10;
    printf("Libretro: state staged (%lld bytes from '%s') — will unserialize after warm-up\n",
           (long long)file_size, path.c_str());
}

static void try_save_state(LibretroHost* host)
{
    if (!host->retro_serialize_size || !host->retro_serialize) {
        printf("Libretro: state save skipped — core has no retro_serialize/serialize_size\n");
        return;
    }
    size_t sz = SafeSerializeSize(host->retro_serialize_size);
    if (sz == 0) {
        printf("Libretro: retro_serialize_size returned 0 — skipping state save\n");
        return;
    }
    std::vector<char> buf(sz);
    if (!SafeSerialize(host->retro_serialize, buf.data(), sz)) {
        printf("Libretro: retro_serialize crashed/refused (size=%zu)\n", sz);
        return;
    }
    ensure_dir(save_dir_for(host));
    std::string path = state_file_for(host);
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) {
        printf("Libretro: state open-for-write failed at '%s'\n", path.c_str());
        return;
    }
    f.write(buf.data(), sz);
    if (!f) {
        printf("Libretro: state write failed at '%s' (%zu bytes intended)\n", path.c_str(), sz);
        return;
    }
    printf("Libretro: state saved (%zu bytes -> '%s')\n", sz, path.c_str());
}

/* ========================================================================
 * Worker thread
 * ======================================================================== */

/* Idempotent: if SET_HW_RENDER fired and no context yet, create one via the
 * engine pump and activate it on this (worker) thread. Returns true if the
 * context is ready, OR if no HW render was requested (nothing to do).
 *
 * Some cores (mupen64plus-next) call SET_HW_RENDER from inside retro_load_game
 * rather than during retro_init, so we run this both before and after load_game.
 */
static bool ensure_hw_gl_context(LibretroHost* host)
{
    if (!host->hw_render_pending) return true;
    if (host->hw_gl_context)      return true; /* already done */

    printf("Libretro: HW path — requesting GL context from engine\n");
    if (!request_gl_context_blocking(host)) {
        printf("Libretro: HW context creation failed\n");
        return false;
    }
    printf("Libretro: HW context obtained (%p), activating on worker\n", host->hw_gl_context);
    if (!g_host.libretro_set_current_gl_context) {
        printf("Libretro: g_host.libretro_set_current_gl_context is NULL — host API mismatch\n");
        return false;
    }
    g_host.libretro_set_current_gl_context(host->hw_gl_context);

    /* Sanity probe: glGetString is GL 1.1 (resolved at link time from opengl32),
     * so it works whether or not extension loading is happy. NULL here = the
     * worker thread has no current context (MakeCurrent silently failed). */
    const GLubyte* gl_ver = glGetString(GL_VERSION);
    const GLubyte* gl_ren = glGetString(GL_RENDERER);
    printf("Libretro: worker GL context — version=%s renderer=%s\n",
           gl_ver ? (const char*)gl_ver : "(null)",
           gl_ren ? (const char*)gl_ren : "(null)");

    load_fbo_gl_functions(host);
    printf("Libretro: GL FBO functions loaded (genFB=%p bindFB=%p fbTex2D=%p)\n",
           (void*)host->glGenFramebuffersFn, (void*)host->glBindFramebufferFn,
           (void*)host->glFramebufferTexture2DFn);

    /* If FBO funcs all came back NULL despite glGetString working, our proc
     * loader is broken — diagnose via a baseline 1.1 function. */
    if (!host->glGenFramebuffersFn) {
        void* probe = load_gl_proc("glGetError");
        printf("Libretro: load_gl_proc diagnostic — glGetError=%p\n", probe);
    }
    return true;
}

/* Phase 6: tear down everything game-specific WITHOUT touching the DLL or HW
 * GL context. Used by load_game when it's called on an already-loaded host
 * (hot-swap path). Saves state/SRAM for the outgoing game so progress isn't lost,
 * then resets all per-game state so the next worker_do_load_game can populate
 * fresh fields. The HW FBO + context stay alive — the core's context_reset
 * fires again on the new game's load, so it can rebuild its GL state. */
static void worker_do_swap_unload(LibretroHost* host)
{
    if (!SDL_AtomicGet(&host->game_loaded)) return;
    if (SDL_AtomicGet(&host->core_crashed)) return;

    printf("Libretro: hot-swap — saving + unloading '%s'\n", host->loaded_full_path.c_str());
    /* Persist any per-game option edits the user made on the outgoing game
     * before we drop the map and load the new game's overrides. */
    {
        SDL_LockMutex(host->options_lock);
        save_game_options(host);
        host->options_game.clear();
        host->options_game_loaded_for.clear();
        SDL_UnlockMutex(host->options_lock);
    }
    try_save_state(host);
    try_save_sram(host);
    if (host->retro_unload_game)
        (void)SafeCallVoid(host->retro_unload_game);

    SDL_AtomicSet(&host->game_loaded, 0);

    /* Audio ring + resampler — drop any leftover samples from the outgoing game. */
    host->audio_ring_write.store(0);
    host->audio_ring_read.store(0);
    host->resample_phase = 0.0;
    host->single_sample_accum = 0.0;

    /* Force context_reset to fire on the new load so the core rebuilds its GL state. */
    host->hw_context_reset_called = false;

    /* Drop any state-restore that was pending for the outgoing game. */
    host->pending_state_buf.clear();
    host->pending_state_attempts_left = 0;

    /* Clear loaded-game metadata so GET_GAME_INFO_EXT doesn't return stale data
     * if the new game hasn't populated yet. */
    host->loaded_full_path.clear();
    host->loaded_archive_path.clear();
    host->loaded_archive_file.clear();
    host->loaded_dir.clear();
    host->loaded_name.clear();
    host->loaded_ext.clear();
    host->loaded_file_in_archive = false;
    host->loaded_persistent_data = false;
    host->loaded_data = nullptr;
    host->loaded_data_size = 0;

    /* ROM buffers + game_info_ext structs from the outgoing game can't be
     * referenced anymore. */
    for (void* p : host->persistent_rom_buffers) free(p);
    host->persistent_rom_buffers.clear();
    for (auto* e : host->allocated_game_info_ext) delete e;
    host->allocated_game_info_ext.clear();
}

/* Synchronous load_game on worker. Reads ROM file if !need_fullpath. */
static bool worker_do_load_game(LibretroHost* host, const std::string& path)
{
    /* Phase 6 hot-swap: if a game is already loaded, treat this as a swap.
     * We retain the DLL, HW GL context, and FBO; only the per-game state cycles. */
    if (SDL_AtomicGet(&host->game_loaded)) {
        worker_do_swap_unload(host);
    }

    /* Path 1: core declared HW render during retro_init (e.g. some PSP cores).
     * Set up the GL context before load_game so the core can touch GL resources. */
    if (!ensure_hw_gl_context(host)) {
        printf("Libretro: HW context setup failed pre-load — refusing\n");
        return false;
    }

    /* Phase 5: if the path is a .zip/.7z and the core doesn't list it in its
     * valid_extensions, transparently extract the first acceptable entry to a
     * temp file and use that path instead. Cores that handle archives natively
     * (e.g. FBNeo, MAME with .7z) pass through. */
    ArchiveResolveOut arch_meta;
    std::string effective_path = libretro_archive_resolve(
        path, host->system_info.valid_extensions, host->extracted_temp_files, arch_meta);
    if (effective_path.empty()) {
        printf("Libretro: archive resolution failed — refusing load\n");
        return false;
    }

    /* Phase 5c: figure out effective need_fullpath / persistent_data using
     * per-extension overrides if any matched. */
    std::string ext = derive_basename(effective_path); /* reuse helper to strip path components first */
    /* derive_basename strips ext too; redo just to get the lowercase ext. */
    {
        std::string name_only;
        size_t slash = effective_path.find_last_of("/\\");
        name_only = (slash == std::string::npos) ? effective_path : effective_path.substr(slash + 1);
        size_t dot = name_only.find_last_of('.');
        ext = (dot == std::string::npos) ? "" : name_only.substr(dot + 1);
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    }

    bool need_fullpath = host->system_info.need_fullpath;
    bool persistent    = false;
    if (apply_content_override(host, ext, need_fullpath, persistent)) {
        printf("Libretro: applying content override for ext '.%s' — need_fullpath=%d persistent_data=%d\n",
               ext.c_str(), need_fullpath ? 1 : 0, persistent ? 1 : 0);
    }

    /* Populate game metadata that GET_GAME_INFO_EXT will read. Strings live on
     * the host struct so the core can hold pointers safely. */
    host->loaded_full_path     = effective_path;
    host->loaded_archive_path  = arch_meta.archive_path;
    host->loaded_archive_file  = arch_meta.archive_file;
    host->loaded_file_in_archive = arch_meta.file_in_archive;
    host->loaded_persistent_data = persistent;
    {
        size_t slash = effective_path.find_last_of("/\\");
        host->loaded_dir = (slash == std::string::npos) ? "" : effective_path.substr(0, slash);
        std::string name_only = (slash == std::string::npos) ? effective_path : effective_path.substr(slash + 1);
        size_t dot = name_only.find_last_of('.');
        host->loaded_name = (dot == std::string::npos) ? name_only : name_only.substr(0, dot);
        host->loaded_ext  = ext;
    }

    struct retro_game_info game{};
    void* rom_data = nullptr;
    size_t rom_size = 0;

    if (need_fullpath) {
        game.path = effective_path.c_str();
    } else {
        FILE* f = fopen(effective_path.c_str(), "rb");
        if (!f) {
            printf("Libretro: Failed to open game file: %s\n", effective_path.c_str());
            return false;
        }
        fseek(f, 0, SEEK_END);
        rom_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        rom_data = malloc(rom_size);
        if (!rom_data) {
            fclose(f);
            return false;
        }
        fread(rom_data, 1, rom_size, f);
        fclose(f);
        game.path = effective_path.c_str();
        game.data = rom_data;
        game.size = rom_size;
        host->loaded_data      = rom_data;
        host->loaded_data_size = rom_size;
    }

    bool ok = SafeLoadGame(host->retro_load_game, &game);
    /* Phase 5c: with persistent_data, the core may keep the buffer past
     * load_game return — transfer ownership to the host instead of freeing. */
    if (rom_data) {
        if (ok && persistent) {
            host->persistent_rom_buffers.push_back(rom_data);
        } else {
            free(rom_data);
            host->loaded_data      = nullptr;
            host->loaded_data_size = 0;
        }
    }
    if (!ok) {
        printf("Libretro: retro_load_game failed\n");
        return false;
    }

    if (!SafeGetSystemAvInfo(host->retro_get_system_av_info, &host->av_info)) {
        printf("Libretro: retro_get_system_av_info crashed\n");
        return false;
    }
    printf("Libretro: Game loaded - source rate %u Hz, %ux%u @ %.2f fps\n",
           (unsigned)host->av_info.timing.sample_rate,
           host->av_info.geometry.base_width, host->av_info.geometry.base_height,
           host->av_info.timing.fps);

    /* Reset resampler before any audio callbacks fire from the next frame. */
    host->resample_phase = 0.0;
    host->single_sample_accum = 0.0;

    /* Allocate post-resample audio ring at target rate (250 ms latency cap). */
    host->audio_ring_size = LIBRETRO_TARGET_AUDIO_RATE / 4;
    if (host->audio_ring_size < 16384) host->audio_ring_size = 16384;
    host->audio_ring_buf = (int16_t*)calloc(host->audio_ring_size, sizeof(int16_t));
    host->audio_ring_write.store(0);
    host->audio_ring_read.store(0);

    host->game_basename = derive_basename(path);
    /* Per-game option overrides — must be loaded before any GET_VARIABLE calls
     * during subsequent retro_run frames. */
    {
        SDL_LockMutex(host->options_lock);
        load_game_options(host, host->game_basename);
        SDL_UnlockMutex(host->options_lock);
    }

    /* Path 2: core declared HW render *during* load_game (mupen64plus-next does
     * this). Set up the GL context now if it hasn't been already. */
    if (!ensure_hw_gl_context(host)) {
        printf("Libretro: HW context setup failed post-load — refusing\n");
        return false;
    }

    /* If a HW context exists, size the FBO to the core's max geometry and call
     * context_reset so the core can build its GL state. context_reset is the
     * spec-mandated trigger for the core to (re)create its GL resources. */
    if (host->hw_render_pending && host->hw_gl_context) {
        unsigned mw = host->av_info.geometry.max_width  ? host->av_info.geometry.max_width  : 1024;
        unsigned mh = host->av_info.geometry.max_height ? host->av_info.geometry.max_height : 1024;
        printf("Libretro: HW path — creating FBO at %ux%u\n", mw, mh);
        if (!ensure_fbo(host, (int)mw, (int)mh)) {
            printf("Libretro: HW FBO setup failed — running without HW render\n");
        } else {
            host->hw_render_active = true;
            printf("Libretro: HW path — calling core context_reset (cb=%p)\n", (void*)host->hw_cb.context_reset);
            if (host->hw_cb.context_reset && !host->hw_context_reset_called) {
                if (!SafeContextReset(host->hw_cb.context_reset))
                    printf("Libretro: hw_cb.context_reset crashed\n");
                else
                    printf("Libretro: HW path — context_reset returned cleanly\n");
                host->hw_context_reset_called = true;
            }
        }
    }

    /* Auto-load SRAM and save state (Phase 3 — save state is auto-only per plan). */
    try_load_sram(host);
    try_load_state(host);

    SDL_AtomicSet(&host->game_loaded, 1);
    return true;
}

static void worker_do_unload(LibretroHost* host)
{
    if (!SDL_AtomicGet(&host->game_loaded)) return;
    if (!SDL_AtomicGet(&host->core_crashed)) {
        try_save_state(host);
        try_save_sram(host);
        if (host->retro_unload_game)
            (void)SafeCallVoid(host->retro_unload_game);
    }
    SDL_AtomicSet(&host->game_loaded, 0);
}

static int SDLCALL worker_thread_main(void* userdata)
{
    LibretroHost* host = (LibretroHost*)userdata;

    /* Register the worker as the host's thread owner — all subsequent retro_*()
     * calls happen here, so callbacks resolve to this host via the registry. */
    LibretroManager_RegisterThreadOwner(host);

    /* Init sequence: open DLL, set env, retro_init, set callbacks, get system info. */
    host->init_result = false;
    host->core_dll = SafeLoadModule(host->core_full_path.c_str());
    if (!host->core_dll) {
        printf("Libretro: Failed to load core DLL: %s\n", SDL_GetError());
        SDL_SemPost(host->init_done_sem);
        LibretroManager_UnregisterThreadOwner(host);
        return 1;
    }
    if (!load_core_symbols(host)) {
        SDL_SemPost(host->init_done_sem);
        LibretroManager_UnregisterThreadOwner(host);
        return 1;
    }
    unsigned ver = SafeApiVersion(host->retro_api_version);
    if (ver != RETRO_API_VERSION) {
        printf("Libretro: API version mismatch (core=%u, expected=%u)\n", ver, RETRO_API_VERSION);
        SDL_SemPost(host->init_done_sem);
        LibretroManager_UnregisterThreadOwner(host);
        return 1;
    }
    printf("Libretro: Core API version: %u\n", ver);

    if (!SafeSetEnv(host->retro_set_environment, host_environment_callback)) {
        printf("Libretro: retro_set_environment crashed\n");
        SDL_AtomicSet(&host->core_crashed, 1);
    }
    if (!SafeCallVoid(host->retro_init)) {
        printf("Libretro: retro_init crashed\n");
        SDL_AtomicSet(&host->core_crashed, 1);
        SDL_SemPost(host->init_done_sem);
        LibretroManager_UnregisterThreadOwner(host);
        return 1;
    }

    SafeSetVideo(host->retro_set_video_refresh, host_video_refresh_callback);
    SafeSetAudioSample(host->retro_set_audio_sample, host_audio_sample_callback);
    SafeSetAudioBatch(host->retro_set_audio_sample_batch, host_audio_sample_batch_callback);
    SafeSetInputPoll(host->retro_set_input_poll, host_input_poll_callback);
    SafeSetInputState(host->retro_set_input_state, host_input_state_callback);

    if (!SafeGetSystemInfo(host->retro_get_system_info, &host->system_info))
        printf("Libretro: retro_get_system_info crashed\n");
    printf("Libretro: Loaded core: %s v%s\n",
           host->system_info.library_name ? host->system_info.library_name : "(unknown)",
           host->system_info.library_version ? host->system_info.library_version : "(unknown)");

    host->init_result = true;
    SDL_SemPost(host->init_done_sem);
    printf("Libretro: Worker init complete\n");

    /* Main loop: wait on wake_sem. Each wake processes any pending request,
     * then drains the frame counter. */
    while (!SDL_AtomicGet(&host->shutdown_flag)) {
        SDL_SemWait(host->wake_sem);
        if (SDL_AtomicGet(&host->shutdown_flag)) break;

        /* --- Load game request --- */
        bool do_load = false;
        std::string path;
        SDL_LockMutex(host->request_lock);
        if (host->load_request_pending) {
            do_load = true;
            path = host->pending_game_path;
            host->load_request_pending = false;
        }
        SDL_UnlockMutex(host->request_lock);
        if (do_load) {
            host->load_result = worker_do_load_game(host, path);
            SDL_SemPost(host->load_done_sem);
            /* fall through to also check pending_frames in case engine raced */
        }

        /* --- Reset request --- */
        if (SDL_AtomicGet(&host->reset_request) && SDL_AtomicGet(&host->game_loaded) && !SDL_AtomicGet(&host->core_crashed)) {
            SDL_AtomicSet(&host->reset_request, 0);
            if (host->retro_reset && !SafeCallVoid(host->retro_reset)) {
                printf("Libretro: retro_reset crashed\n");
                SDL_AtomicSet(&host->core_crashed, 1);
            }
        }

        /* --- Frame request(s) --- */
        int frames = SDL_AtomicSet(&host->pending_frames, 0);
        if (frames > 0 && SDL_AtomicGet(&host->game_loaded) && !SDL_AtomicGet(&host->core_crashed)) {
            /* Run a single frame per wake even if multiple were posted — the engine
             * is the pacemaker, and burning through a backlog defeats audio pacing.
             * If frames > 1, we just discard the surplus. */
            (void)frames;
            if (!SafeRunCore(host->retro_run)) {
                printf("Libretro: retro_run crashed — disabling core\n");
                SDL_AtomicSet(&host->core_crashed, 1);
                continue;
            }

            /* Pending state-restore retry: cores like mupen64plus-next reject
             * unserialize until the first retro_run has fully initialized the
             * emulator. We retry post-run for several frames before giving up. */
            if (host->pending_state_attempts_left > 0 && !host->pending_state_buf.empty()) {
                if (SafeUnserialize(host->retro_unserialize,
                                    host->pending_state_buf.data(),
                                    host->pending_state_buf.size())) {
                    printf("Libretro: state restored (%zu bytes, %d retries remaining)\n",
                           host->pending_state_buf.size(),
                           host->pending_state_attempts_left - 1);
                    host->pending_state_buf.clear();
                    host->pending_state_attempts_left = 0;
                } else {
                    host->pending_state_attempts_left--;
                    if (host->pending_state_attempts_left == 0) {
                        printf("Libretro: gave up on state restore after warm-up retries (size=%zu)\n",
                               host->pending_state_buf.size());
                        host->pending_state_buf.clear();
                    }
                }
            }

            if (SDL_AtomicGet(&host->frame_updated)) {
                SDL_LockMutex(host->video_lock);
                std::swap(host->video_frame_front, host->video_frame_back);
                std::swap(host->video_frame_front_capacity, host->video_frame_back_capacity);
                SDL_UnlockMutex(host->video_lock);
                SDL_AtomicSet(&host->frame_updated, 0);
            }

            /* Audio pacing: if the post-resample ring is more than ~40 ms full,
             * sleep a tick before accepting the next frame so we don't overflow
             * and throw away audio. 40 ms @ 48 kHz stereo = 3840 samples. */
            if (audio_ring_available(host) > 3840) {
                SDL_Delay(1);
            }
        }
    }

    /* Shutdown: persist game state, then release core. */
    worker_do_unload(host);

    /* HW render teardown: notify core, drop FBO, release+destroy GL context. */
    if (host->hw_render_active) {
        if (host->hw_cb.context_destroy && !SDL_AtomicGet(&host->core_crashed)) {
            if (!SafeContextDestroy(host->hw_cb.context_destroy))
                printf("Libretro: hw_cb.context_destroy crashed\n");
        }
        destroy_fbo(host);
        host->hw_render_active = false;
    }

    if (!SDL_AtomicGet(&host->core_crashed) && host->retro_deinit) {
        if (!SafeCallVoid(host->retro_deinit))
            printf("Libretro: retro_deinit crashed\n");
    }
    if (host->core_dll) {
        SDL_UnloadObject(host->core_dll);
        host->core_dll = nullptr;
    }

    if (host->hw_gl_context) {
        if (g_host.libretro_set_current_gl_context)
            g_host.libretro_set_current_gl_context(nullptr); /* release on this thread */
        if (g_host.libretro_destroy_gl_context)
            g_host.libretro_destroy_gl_context(host->hw_gl_context);
        host->hw_gl_context = nullptr;
    }

    LibretroManager_UnregisterThreadOwner(host);
    return 0;
}

/* ========================================================================
 * Public API
 * ======================================================================== */

LibretroHost* libretro_host_create(const char* core_path)
{
    if (!core_path) {
        printf("Libretro: NULL core path provided\n");
        return NULL;
    }

    LibretroHost* host = new (std::nothrow) LibretroHost();
    if (!host) {
        printf("Libretro: Failed to allocate host structure\n");
        return NULL;
    }

    SDL_AtomicSet(&host->frame_updated, 0);
    SDL_AtomicSet(&host->game_loaded, 0);
    SDL_AtomicSet(&host->core_crashed, 0);
    SDL_AtomicSet(&host->shutdown_flag, 0);
    SDL_AtomicSet(&host->pending_frames, 0);
    SDL_AtomicSet(&host->reset_request, 0);
    SDL_AtomicSet(&host->hw_create_request, 0);

    host->video_lock        = SDL_CreateMutex();
    host->options_lock      = SDL_CreateMutex();
    host->request_lock      = SDL_CreateMutex();
    host->wake_sem          = SDL_CreateSemaphore(0);
    host->init_done_sem     = SDL_CreateSemaphore(0);
    host->load_done_sem     = SDL_CreateSemaphore(0);
    host->hw_create_done_sem = SDL_CreateSemaphore(0);

    host->core_full_path = core_path;
    host->core_basename  = derive_basename(core_path);
    load_persisted_options(host);

    printf("Libretro: Spawning worker for %s\n", core_path);
    host->worker_thread = SDL_CreateThread(worker_thread_main, "libretro_worker", host);
    if (!host->worker_thread) {
        printf("Libretro: SDL_CreateThread failed: %s\n", SDL_GetError());
        SDL_DestroyMutex(host->video_lock);
        SDL_DestroyMutex(host->options_lock);
        SDL_DestroyMutex(host->request_lock);
        SDL_DestroySemaphore(host->wake_sem);
        SDL_DestroySemaphore(host->init_done_sem);
        SDL_DestroySemaphore(host->load_done_sem);
        SDL_DestroySemaphore(host->hw_create_done_sem);
        delete host;
        return NULL;
    }

    /* Wait for worker to finish init (success or failure). The worker may
     * block on hw_create_done_sem during SET_HW_RENDER; we service that here
     * via the engine-thread pump so the engine's GL context is current at the
     * moment of SDL_GL_CreateContext. */
    while (SDL_SemWaitTimeout(host->init_done_sem, 5) == SDL_MUTEX_TIMEDOUT) {
        pump_pending_gl_request(host);
    }
    if (!host->init_result) {
        printf("Libretro: Worker reported init failure\n");
        SDL_AtomicSet(&host->shutdown_flag, 1);
        SDL_SemPost(host->wake_sem);
        SDL_WaitThread(host->worker_thread, NULL);
        host->worker_thread = nullptr;
        SDL_DestroyMutex(host->video_lock);
        SDL_DestroyMutex(host->options_lock);
        SDL_DestroyMutex(host->request_lock);
        SDL_DestroySemaphore(host->wake_sem);
        SDL_DestroySemaphore(host->init_done_sem);
        SDL_DestroySemaphore(host->load_done_sem);
        SDL_DestroySemaphore(host->hw_create_done_sem);
        delete host;
        return NULL;
    }

    return host;
}

bool libretro_host_load_game(LibretroHost* host, const char* game_path)
{
    if (!host || !game_path) return false;
    if (SDL_AtomicGet(&host->core_crashed)) return false;

    SDL_LockMutex(host->request_lock);
    host->pending_game_path = game_path;
    host->load_request_pending = true;
    SDL_UnlockMutex(host->request_lock);
    SDL_SemPost(host->wake_sem);

    /* Same pump pattern as create — worker may request GL context creation
     * if SET_HW_RENDER deferred to here. */
    while (SDL_SemWaitTimeout(host->load_done_sem, 5) == SDL_MUTEX_TIMEDOUT) {
        pump_pending_gl_request(host);
    }
    return host->load_result;
}

bool libretro_host_swap_game(LibretroHost* host, const char* game_path)
{
    /* Implementation reuses load_game's worker request path — worker_do_load_game
     * detects the already-loaded state and runs worker_do_swap_unload first. */
    return libretro_host_load_game(host, game_path);
}

const char* libretro_host_get_core_path(LibretroHost* host)
{
    return host ? host->core_full_path.c_str() : nullptr;
}

void libretro_host_set_input(LibretroHost* host, unsigned port, int16_t buttons)
{
    if (host && port <= 1) host->input_state[port] = buttons;
}

void libretro_host_set_analog(LibretroHost* host, unsigned port, unsigned stick, int16_t x, int16_t y)
{
    if (!host || port > 1 || stick > 1) return;
    host->analog_state[port][stick][0] = x;
    host->analog_state[port][stick][1] = y;
}

void libretro_host_run_frame(LibretroHost* host)
{
    if (!host) return;
    if (SDL_AtomicGet(&host->core_crashed)) return;
    SDL_AtomicAdd(&host->pending_frames, 1);
    SDL_SemPost(host->wake_sem);
}

const void* libretro_host_get_frame(LibretroHost* host,
                                     unsigned* out_width,
                                     unsigned* out_height,
                                     size_t* out_pitch,
                                     bool* out_is_xrgb8888)
{
    if (!host || !host->video_frame_front) return NULL;
    if (out_width) *out_width = host->frame_width;
    if (out_height) *out_height = host->frame_height;
    if (out_pitch) *out_pitch = host->frame_pitch;
    if (out_is_xrgb8888) *out_is_xrgb8888 = (host->pixel_format == RETRO_PIXEL_FORMAT_XRGB8888);
    return host->video_frame_front;
}

void libretro_host_reset(LibretroHost* host)
{
    if (!host) return;
    SDL_AtomicSet(&host->reset_request, 1);
    SDL_SemPost(host->wake_sem);
}

void libretro_host_destroy(LibretroHost* host)
{
    if (!host) return;

    /* Persist user-edited options before tearing down the worker. */
    save_persisted_options(host);
    save_game_options(host);

    /* Signal shutdown and wake worker so it observes the flag. */
    SDL_AtomicSet(&host->shutdown_flag, 1);
    SDL_SemPost(host->wake_sem);
    if (host->worker_thread) {
        SDL_WaitThread(host->worker_thread, NULL);
        host->worker_thread = nullptr;
    }

    /* Worker has unloaded the core and unregistered itself by this point. */

    if (host->audio_ring_buf) free(host->audio_ring_buf);
    if (host->video_frame_front) free(host->video_frame_front);
    if (host->video_frame_back) free(host->video_frame_back);
    if (host->video_lock) SDL_DestroyMutex(host->video_lock);
    if (host->options_lock) SDL_DestroyMutex(host->options_lock);
    if (host->request_lock) SDL_DestroyMutex(host->request_lock);
    if (host->wake_sem) SDL_DestroySemaphore(host->wake_sem);
    if (host->init_done_sem) SDL_DestroySemaphore(host->init_done_sem);
    if (host->load_done_sem) SDL_DestroySemaphore(host->load_done_sem);
    if (host->hw_create_done_sem) SDL_DestroySemaphore(host->hw_create_done_sem);

    for (char* s : host->allocated_variable_strings) free(s);

    /* Phase 5: clean up extracted-archive temp files. Best-effort — if a file
     * was already gone (manual deletion / disk full earlier), remove() returns
     * non-zero and we just move on. */
    for (const std::string& p : host->extracted_temp_files) {
        if (std::remove(p.c_str()) == 0)
            printf("Libretro: removed temp '%s'\n", p.c_str());
    }

    /* Phase 5c: free ROM buffers we kept alive past load_game (persistent_data). */
    for (void* p : host->persistent_rom_buffers) free(p);

    /* Free retro_game_info_ext structs we returned to the core. The strings
     * inside point at host->loaded_* members which are about to be destroyed
     * along with the host — safe order. */
    for (struct retro_game_info_ext* e : host->allocated_game_info_ext) delete e;

    delete host;
    printf("Libretro: Host destroyed\n");
}

void libretro_host_get_system_info(LibretroHost* host,
                                    const char** out_name,
                                    const char** out_version)
{
    if (!host) return;
    if (out_name) *out_name = host->system_info.library_name;
    if (out_version) *out_version = host->system_info.library_version;
}

int libretro_host_get_sample_rate(LibretroHost* host)
{
    /* Phase 3: we always output at LIBRETRO_TARGET_AUDIO_RATE regardless of the
     * core's native rate — the resampler handles the conversion. The engine
     * opens its SDL audio device at this rate. Returns 0 only when no game has
     * loaded yet so the engine can defer audio device init. */
    if (!host || !SDL_AtomicGet(&host->game_loaded)) return 0;
    return LIBRETRO_TARGET_AUDIO_RATE;
}

int libretro_host_read_audio(LibretroHost* host, int16_t* buffer, int max_frames)
{
    if (!host || !host->audio_ring_buf || !buffer || max_frames <= 0) return 0;

    size_t avail = audio_ring_available(host);
    size_t to_copy = (size_t)(max_frames * 2);
    if (to_copy > avail) to_copy = avail;

    size_t r = host->audio_ring_read.load(std::memory_order_relaxed);
    for (size_t i = 0; i < to_copy; i++) {
        buffer[i] = host->audio_ring_buf[r];
        r = (r + 1) % host->audio_ring_size;
    }
    host->audio_ring_read.store(r, std::memory_order_release);
    return (int)(to_copy / 2);
}

void libretro_host_set_key_state(LibretroHost* host, unsigned retrok_id, int pressed)
{
    if (host && retrok_id < 512)
        host->keyboard_state[retrok_id] = pressed ? 1 : 0;
}

/* ========================================================================
 * Options-bridge ABI
 * ======================================================================== */

static std::string json_escape(const std::string& s)
{
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char tmp[8];
                    snprintf(tmp, sizeof(tmp), "\\u%04x", (unsigned)c);
                    o += tmp;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

int libretro_host_get_options_json(LibretroHost* host, char* out, int max_len)
{
    if (!host) return 0;
    /* Two-call protocol: pass out=NULL or max_len<=0 to query the size of the
     * JSON the host would produce (excluding the NUL terminator). Caller then
     * allocates exactly that and calls again with a real buffer. Prevents the
     * silent truncation bug that bit cores with many options (mupen64plus-next
     * declares 85 — ~30KB of JSON overflowed an old 16KB buffer). */
    bool size_query = (!out || max_len <= 0);
    std::ostringstream ss;
    ss << '[';
    bool first = true;
    SDL_LockMutex(host->options_lock);
    for (const auto& def : host->option_defs) {
        if (!first) ss << ',';
        first = false;
        std::string effective = resolve_option_value_locked(host, def.key);
        /* core_value: empty string if user hasn't set a core-tier value (UI
         * should show declared default in that case). game_value: empty string
         * == "Inherit" in the UI (no override). */
        std::string core_v;
        auto ci = host->options_current.find(def.key);
        if (ci != host->options_current.end()) core_v = ci->second;
        std::string game_v;
        auto gi = host->options_game.find(def.key);
        if (gi != host->options_game.end()) game_v = gi->second;

        ss << "{\"key\":\""        << json_escape(def.key)           << "\","
           << "\"display\":\""     << json_escape(def.display)       << "\","
           << "\"default\":\""     << json_escape(def.default_value) << "\","
           << "\"core_value\":\""  << json_escape(core_v)            << "\","
           << "\"game_value\":\""  << json_escape(game_v)            << "\","
           << "\"current\":\""     << json_escape(effective)         << "\","
           << "\"values\":[";
        bool fv = true;
        for (const auto& v : def.values) {
            if (!fv) ss << ',';
            fv = false;
            ss << '"' << json_escape(v) << '"';
        }
        ss << "]}";
    }
    SDL_UnlockMutex(host->options_lock);
    ss << ']';
    std::string js = ss.str();
    int n = (int)js.size();
    /* Size-query path: caller passed no buffer — return needed size only. */
    if (size_query) return n;
    if (n >= max_len) n = max_len - 1;
    memcpy(out, js.data(), n);
    out[n] = '\0';
    return n;
}

/* tier: "core" → write to options_current and persist to <core>.opt
 *       "game" → write to options_game and persist to games/<core>/<game>.opt
 *                (passing empty `value` clears the override = inherit)
 * Defaults to "core" if tier is null. */
bool libretro_host_set_option(LibretroHost* host, const char* key, const char* value, const char* tier)
{
    if (!host || !key || !value) return false;
    bool is_game = tier && strcmp(tier, "game") == 0;

    SDL_LockMutex(host->options_lock);

    /* Validate against the declared value set, except when clearing a game
     * override (value == ""), which is always legal. */
    if (!(is_game && value[0] == '\0')) {
        bool valid = true;
        for (const auto& def : host->option_defs) {
            if (def.key != key) continue;
            valid = def.values.empty();
            for (const auto& v : def.values) {
                if (v == value) { valid = true; break; }
            }
            break;
        }
        if (!valid) {
            SDL_UnlockMutex(host->options_lock);
            printf("Libretro: rejected option %s='%s' (not in declared values)\n", key, value);
            return false;
        }
    }

    if (is_game) {
        if (value[0] == '\0') host->options_game.erase(key);
        else                  host->options_game[key] = value;
    } else {
        host->options_current[key] = value;
    }
    host->options_have_changed = true;
    SDL_UnlockMutex(host->options_lock);
    if (is_game) save_game_options(host);
    else         save_persisted_options(host);
    return true;
}
