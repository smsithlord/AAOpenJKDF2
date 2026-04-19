/*
 * VideoPlayerInstance — plays video files on in-world textures via libmpv.
 *
 * Uses mpv's software render API (MPV_RENDER_API_TYPE_SW) to decode frames
 * into a double-buffered pixel buffer on a background thread. The main thread
 * only copies the ready buffer when the host requests a render, matching the
 * EmbeddedInstance pull model used by LibretroInstance.
 *
 * Audio is handled by mpv internally (wasapi on Windows).
 */

#include "VideoPlayerInstance.h"
#include "aarcadecore_internal.h"
#include <mpv/client.h>
#include <mpv/render.h>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <atomic>
#include <thread>
#include <map>
#include <string>

extern AACoreHostCallbacks g_host;

/* Forward declarations for Ultralight HUD localStorage access */
EmbeddedInstance* UltralightManager_GetHudInstance(void);
void UltralightInstance_EvaluateScript(EmbeddedInstance* inst, const char* script);
std::string UltralightInstance_EvalScriptString(EmbeddedInstance* inst, const char* script);

/* ======================================================================== */
/* Playback position tracking — in-memory + localStorage persistence       */
/* ======================================================================== */
static std::map<std::string, double> s_savedPositions;   /* file path → last position (in-memory) */

static uint32_t djb2Hash(const char* s)
{
    uint32_t h = 5381;
    while (*s) h = ((h << 5) + h) + (uint8_t)*s++;
    return h;
}

static std::string videoStorageKey(const char* filePath)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "vid_%08x", djb2Hash(filePath));
    return buf;
}

/* Save position to in-memory map (called from decode thread) */
static void savePosition(const char* filePath, double pos, double dur)
{
    if (!filePath || pos <= 0) return;
    s_savedPositions[filePath] = pos;
}

/* Flush in-memory position to localStorage (call from main thread only) */
static void flushPositionToStorage(const char* filePath)
{
    auto it = s_savedPositions.find(filePath);
    if (it == s_savedPositions.end() || it->second <= 0) return;
    EmbeddedInstance* hud = UltralightManager_GetHudInstance();
    if (!hud) return;
    char script[256];
    std::string key = videoStorageKey(filePath);
    snprintf(script, sizeof(script),
        "localStorage.setItem('%s','%.1f')", key.c_str(), it->second);
    if (g_host.host_printf)
        g_host.host_printf("VideoPlayer: flushPosition key='%s' pos=%.1f\n",
                          key.c_str(), it->second);
    UltralightInstance_EvaluateScript(hud, script);
}

/* Load saved position from localStorage (call from main thread only) */
static double loadPositionFromStorage(const char* filePath)
{
    if (!filePath) return 0;
    /* Check in-memory first (same session) */
    auto it = s_savedPositions.find(filePath);
    if (it != s_savedPositions.end() && it->second > 0) return it->second;
    /* Fall back to localStorage (cross-session) */
    EmbeddedInstance* hud = UltralightManager_GetHudInstance();
    if (!hud) return 0;
    char script[256];
    std::string key = videoStorageKey(filePath);
    snprintf(script, sizeof(script),
        "localStorage.getItem('%s')||''", key.c_str());
    std::string result = UltralightInstance_EvalScriptString(hud, script);
    if (g_host.host_printf)
        g_host.host_printf("VideoPlayer: loadPosition key='%s' result='%s'\n",
                          key.c_str(), result.c_str());
    if (result.empty()) return 0;
    return atof(result.c_str());
}

/* ======================================================================== */

struct VideoPlayerInstanceData {
    mpv_handle* mpv;
    mpv_render_context* render_ctx;
    char* file_path;

    /* Double buffer: decode thread writes to back, render reads from front */
    uint8_t* front_buffer;       /* BGRA — read by render() on main thread */
    uint8_t* back_buffer;        /* BGRA — written by decode thread */
    int buf_width, buf_height;   /* current buffer dimensions */
    std::mutex swap_mutex;       /* protects front_buffer reads and buffer swaps */

    /* Decode thread */
    std::thread decode_thread;
    std::atomic<bool> shutting_down;
    std::atomic<bool> has_new_frame;  /* back buffer has been swapped to front */
    std::atomic<bool> active;
    bool paused;

    /* Render size requested by host (set in render(), read by decode thread) */
    std::atomic<int> requested_width;
    std::atomic<int> requested_height;
};

/* ======================================================================== */
/* Decode thread                                                            */
/* ======================================================================== */

static void decode_thread_func(VideoPlayerInstanceData* data)
{
    if (g_host.host_printf)
        g_host.host_printf("VideoPlayer: Decode thread started\n");

    double lastSavedPos = -1;

    while (!data->shutting_down.load()) {
        if (!data->mpv || !data->render_ctx) break;

        /* Process mpv events (non-blocking) */
        while (true) {
            mpv_event* event = mpv_wait_event(data->mpv, 0);
            if (event->event_id == MPV_EVENT_NONE) break;
            if (event->event_id == MPV_EVENT_SHUTDOWN) {
                data->active.store(false);
            }
            if (event->event_id == MPV_EVENT_END_FILE) {
                mpv_event_end_file* ef = (mpv_event_end_file*)event->data;
                if (ef && ef->reason == MPV_END_FILE_REASON_ERROR) {
                    if (g_host.host_printf)
                        g_host.host_printf("VideoPlayer: Playback error\n");
                    data->active.store(false);
                }
            }
        }

        if (!data->active.load()) break;

        /* Check if mpv has a new frame to render */
        uint64_t flags = mpv_render_context_update(data->render_ctx);
        if (!(flags & MPV_RENDER_UPDATE_FRAME)) {
            /* No new frame — sleep briefly to avoid spinning */
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        /* Get the requested render size */
        int w = data->requested_width.load();
        int h = data->requested_height.load();
        if (w <= 0 || h <= 0) {
            /* Host hasn't requested a render yet — use a default */
            w = 512;
            h = 512;
        }

        /* Ensure back buffer is the right size */
        if (data->buf_width != w || data->buf_height != h) {
            uint8_t* new_back = (uint8_t*)realloc(data->back_buffer, w * h * 4);
            if (!new_back) continue;
            data->back_buffer = new_back;

            /* Also resize front buffer under lock */
            {
                std::lock_guard<std::mutex> lock(data->swap_mutex);
                uint8_t* new_front = (uint8_t*)realloc(data->front_buffer, w * h * 4);
                if (new_front) {
                    data->front_buffer = new_front;
                    memset(data->front_buffer, 0, w * h * 4);
                }
            }
            data->buf_width = w;
            data->buf_height = h;
        }

        /* Render frame into back buffer */
        int stride = w * 4;
        int size[2] = {w, h};
        mpv_render_param render_params[] = {
            {MPV_RENDER_PARAM_SW_SIZE, size},
            {MPV_RENDER_PARAM_SW_FORMAT, (void*)"bgr0"}, /* B,G,R,pad byte order */
            {MPV_RENDER_PARAM_SW_STRIDE, &stride},
            {MPV_RENDER_PARAM_SW_POINTER, data->back_buffer},
            {(mpv_render_param_type)0, nullptr}
        };

        int err = mpv_render_context_render(data->render_ctx, render_params);
        if (err < 0) continue;

        /* Swap: copy back → front under lock */
        {
            std::lock_guard<std::mutex> lock(data->swap_mutex);
            memcpy(data->front_buffer, data->back_buffer, w * h * 4);
            data->has_new_frame.store(true);
        }

        /* Save playback position every ~8 seconds of video time */
        double pos = 0, dur = 0;
        mpv_get_property(data->mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos);
        mpv_get_property(data->mpv, "duration", MPV_FORMAT_DOUBLE, &dur);
        if (pos > 0 && (lastSavedPos < 0 || pos - lastSavedPos >= 8 || pos < lastSavedPos)) {
            lastSavedPos = pos;
            savePosition(data->file_path, pos, dur);
        }
    }

    /* Final save on thread exit */
    if (data->mpv) {
        double pos = 0, dur = 0;
        mpv_get_property(data->mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos);
        mpv_get_property(data->mpv, "duration", MPV_FORMAT_DOUBLE, &dur);
        savePosition(data->file_path, pos, dur);
    }

    if (g_host.host_printf)
        g_host.host_printf("VideoPlayer: Decode thread exiting\n");
}

/* ======================================================================== */
/* Vtable implementations                                                   */
/* ======================================================================== */

static bool video_init(EmbeddedInstance* inst)
{
    VideoPlayerInstanceData* data = (VideoPlayerInstanceData*)inst->user_data;

    data->mpv = mpv_create();
    if (!data->mpv) {
        if (g_host.host_printf) g_host.host_printf("VideoPlayer: mpv_create() failed\n");
        return false;
    }

    /* Core options */
    mpv_set_option_string(data->mpv, "vo", "libmpv");
    mpv_set_option_string(data->mpv, "hwdec", "no");
    mpv_set_option_string(data->mpv, "loop-file", "inf");
    mpv_set_option_string(data->mpv, "audio-display", "no");
    mpv_set_option_string(data->mpv, "terminal", "no");
    mpv_set_option_string(data->mpv, "osc", "no");
    mpv_set_option_string(data->mpv, "input-default-bindings", "no");
    mpv_set_option_string(data->mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(data->mpv, "keep-open", "yes");
    mpv_set_option_string(data->mpv, "video-aspect-override", "no");
    mpv_set_option_string(data->mpv, "keepaspect", "no");

    /* Resume from saved position (if any) — must be set before mpv_initialize */
    double resumePos = loadPositionFromStorage(data->file_path);
    if (resumePos > 0) {
        char startOpt[64];
        snprintf(startOpt, sizeof(startOpt), "%.1f", resumePos);
        mpv_set_option_string(data->mpv, "start", startOpt);
        if (g_host.host_printf)
            g_host.host_printf("VideoPlayer: Resuming from %.1fs\n", resumePos);
    }

    if (mpv_initialize(data->mpv) < 0) {
        if (g_host.host_printf) g_host.host_printf("VideoPlayer: mpv_initialize() failed\n");
        mpv_destroy(data->mpv);
        data->mpv = nullptr;
        return false;
    }

    /* Create software render context */
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, (void*)MPV_RENDER_API_TYPE_SW},
        {(mpv_render_param_type)0, nullptr}
    };

    if (mpv_render_context_create(&data->render_ctx, data->mpv, params) < 0) {
        if (g_host.host_printf) g_host.host_printf("VideoPlayer: mpv_render_context_create() failed\n");
        mpv_terminate_destroy(data->mpv);
        data->mpv = nullptr;
        return false;
    }

    /* Load the file */
    const char* cmd[] = {"loadfile", data->file_path, NULL};
    mpv_command(data->mpv, cmd);

    data->active.store(true);
    data->shutting_down.store(false);
    data->paused = false;

    /* Start decode thread */
    data->decode_thread = std::thread(decode_thread_func, data);

    if (g_host.host_printf)
        g_host.host_printf("VideoPlayer: Initialized — playing '%s'\n", data->file_path);

    return true;
}

static void video_shutdown(EmbeddedInstance* inst)
{
    VideoPlayerInstanceData* data = (VideoPlayerInstanceData*)inst->user_data;
    if (!data) return;

    /* Signal thread to stop and wait */
    data->active.store(false);
    data->shutting_down.store(true);

    if (data->decode_thread.joinable())
        data->decode_thread.join();

    /* Flush position to localStorage (main thread, thread is stopped) */
    if (data->file_path)
        flushPositionToStorage(data->file_path);

    /* Now safe to tear down mpv (thread is stopped) */
    if (data->render_ctx) {
        mpv_render_context_free(data->render_ctx);
        data->render_ctx = nullptr;
    }
    if (data->mpv) {
        mpv_terminate_destroy(data->mpv);
        data->mpv = nullptr;
    }
    if (data->front_buffer) {
        free(data->front_buffer);
        data->front_buffer = nullptr;
    }
    if (data->back_buffer) {
        free(data->back_buffer);
        data->back_buffer = nullptr;
    }
    if (data->file_path) {
        free(data->file_path);
        data->file_path = nullptr;
    }

    if (g_host.host_printf)
        g_host.host_printf("VideoPlayer: Shutdown complete\n");
}

static void video_update(EmbeddedInstance* inst)
{
    /* Decode thread handles everything — nothing to do on main thread */
}

static bool video_is_active(EmbeddedInstance* inst)
{
    VideoPlayerInstanceData* data = (VideoPlayerInstanceData*)inst->user_data;
    return data && data->active.load();
}

static void video_render(EmbeddedInstance* inst, void* pixelData,
                         int width, int height, int is16bit, int bpp)
{
    VideoPlayerInstanceData* data = (VideoPlayerInstanceData*)inst->user_data;
    if (!data) return;

    /* Tell decode thread what size we want */
    data->requested_width.store(width);
    data->requested_height.store(height);

    /* Skip duplicate 16-bit renders within the same engine frame (same
     * dedup pattern as LibretroInstance). 32-bit fullscreen always proceeds. */
    extern uint32_t aarcadecore_getEngineFrame(void);
    uint32_t frame = aarcadecore_getEngineFrame();
    if (is16bit && inst->lastRenderedFrame == frame)
        return;
    if (is16bit)
        inst->lastRenderedFrame = frame;

    std::lock_guard<std::mutex> lock(data->swap_mutex);

    /* Check dimensions match */
    if (!data->front_buffer || data->buf_width != width || data->buf_height != height)
        return;

    /* Convert front buffer (bgr0: B,G,R,pad) to host format */
    if (is16bit) {
        /* BGR0 → RGB565 */
        uint16_t* dst = (uint16_t*)pixelData;
        const uint8_t* src = data->front_buffer;
        for (int i = 0; i < width * height; i++) {
            uint8_t b = src[i * 4 + 0];
            uint8_t g = src[i * 4 + 1];
            uint8_t r = src[i * 4 + 2];
            dst[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        }
    } else {
        /* 32-bit fullscreen: host expects BGRA with alpha=255.
         * Our buffer is bgr0 (B,G,R,pad) — just set alpha. */
        uint8_t* dst = (uint8_t*)pixelData;
        const uint8_t* src = data->front_buffer;
        memcpy(dst, src, width * height * 4);
        /* Set alpha channel to 255 (every 4th byte starting at offset 3) */
        for (int i = 0; i < width * height; i++)
            dst[i * 4 + 3] = 255;
    }

    data->has_new_frame.store(false);
}

static void video_key_down(EmbeddedInstance* inst, int vk_code, int modifiers)
{
    VideoPlayerInstanceData* data = (VideoPlayerInstanceData*)inst->user_data;
    if (!data || !data->mpv) return;

    switch (vk_code) {
        case 0x20: /* VK_SPACE — toggle pause */
        {
            data->paused = !data->paused;
            int pause = data->paused ? 1 : 0;
            mpv_set_property(data->mpv, "pause", MPV_FORMAT_FLAG, &pause);
            break;
        }
        case 0x25: /* VK_LEFT — seek backward 5s */
        {
            const char* cmd[] = {"seek", "-5", "relative", NULL};
            mpv_command_async(data->mpv, 0, cmd);
            break;
        }
        case 0x27: /* VK_RIGHT — seek forward 5s */
        {
            const char* cmd[] = {"seek", "5", "relative", NULL};
            mpv_command_async(data->mpv, 0, cmd);
            break;
        }
    }
}

static void video_key_up(EmbeddedInstance* inst, int vk_code, int modifiers)
{
    /* No key-up handling needed */
}

static void video_key_char(EmbeddedInstance* inst, unsigned int unicode_char, int modifiers)
{
    /* No char handling needed */
}

static const char* video_get_title(EmbeddedInstance* inst)
{
    VideoPlayerInstanceData* data = (VideoPlayerInstanceData*)inst->user_data;
    return data ? data->file_path : "Video";
}

/* ======================================================================== */

static const EmbeddedInstanceVtable g_videoVtable = {
    video_init,
    video_shutdown,
    video_update,
    video_is_active,
    video_render,
    video_key_down,
    video_key_up,
    video_key_char,
    NULL, NULL, NULL, NULL,  /* mouse handlers */
    video_get_title,
    NULL, NULL,              /* get_width, get_height */
    NULL,                    /* navigate */
    NULL, NULL, NULL, NULL, NULL /* go_back, go_forward, reload, can_go_back, can_go_forward */
};

/* ======================================================================== */

EmbeddedInstance* VideoPlayerInstance_Create(const char* file_path, const char* material_name)
{
    EmbeddedInstance* inst = (EmbeddedInstance*)calloc(1, sizeof(EmbeddedInstance));
    VideoPlayerInstanceData* data = new (std::nothrow) VideoPlayerInstanceData();
    if (!inst || !data) { free(inst); delete data; return NULL; }

    data->file_path = _strdup(file_path);
    data->mpv = nullptr;
    data->render_ctx = nullptr;
    data->front_buffer = nullptr;
    data->back_buffer = nullptr;
    data->buf_width = 0;
    data->buf_height = 0;
    data->shutting_down.store(false);
    data->has_new_frame.store(false);
    data->active.store(false);
    data->paused = false;
    data->requested_width.store(0);
    data->requested_height.store(0);

    inst->type = EMBEDDED_VIDEO_PLAYER;
    inst->vtable = &g_videoVtable;
    inst->target_material = material_name;
    inst->user_data = data;
    return inst;
}

bool VideoPlayerInstance_GetTimeInfo(EmbeddedInstance* inst, double* posOut, double* durOut)
{
    if (!inst || inst->type != EMBEDDED_VIDEO_PLAYER) return false;
    VideoPlayerInstanceData* data = (VideoPlayerInstanceData*)inst->user_data;
    if (!data || !data->mpv) return false;

    double pos = 0, dur = 0;
    mpv_get_property(data->mpv, "time-pos", MPV_FORMAT_DOUBLE, &pos);
    mpv_get_property(data->mpv, "duration", MPV_FORMAT_DOUBLE, &dur);
    if (posOut) *posOut = pos;
    if (durOut) *durOut = dur;
    return true;
}

void VideoPlayerInstance_Seek(EmbeddedInstance* inst, double position)
{
    if (!inst || inst->type != EMBEDDED_VIDEO_PLAYER) return;
    VideoPlayerInstanceData* data = (VideoPlayerInstanceData*)inst->user_data;
    if (!data || !data->mpv) return;

    char posStr[64];
    snprintf(posStr, sizeof(posStr), "%.2f", position);
    const char* cmd[] = {"seek", posStr, "absolute", NULL};
    mpv_command_async(data->mpv, 0, cmd);
}

bool VideoPlayerInstance_CaptureSnapshot(EmbeddedInstance* inst,
                                         unsigned char** bgraOut,
                                         int* widthOut, int* heightOut)
{
    if (!inst || inst->type != EMBEDDED_VIDEO_PLAYER) return false;
    if (!bgraOut || !widthOut || !heightOut) return false;
    VideoPlayerInstanceData* data = (VideoPlayerInstanceData*)inst->user_data;
    if (!data) return false;

    /* mpv renders into a square game texture with letterbox/pillarbox padding
     * to preserve the video's display aspect. Query those display dimensions
     * so we can crop the padding off the saved snapshot. */
    int64_t dwidth = 0, dheight = 0;
    if (data->mpv) {
        mpv_get_property(data->mpv, "dwidth", MPV_FORMAT_INT64, &dwidth);
        mpv_get_property(data->mpv, "dheight", MPV_FORMAT_INT64, &dheight);
    }

    /* Snapshot of the current front buffer under the swap lock. */
    std::lock_guard<std::mutex> lock(data->swap_mutex);
    if (!data->front_buffer || data->buf_width <= 0 || data->buf_height <= 0)
        return false;
    int bw = data->buf_width;
    int bh = data->buf_height;

    /* Compute the content rect inside the square buffer. */
    int cx = 0, cy = 0, cw = bw, ch = bh;
    if (dwidth > 0 && dheight > 0) {
        double videoAspect = (double)dwidth / (double)dheight;
        double bufAspect   = (double)bw     / (double)bh;
        if (videoAspect > bufAspect) {
            /* Letterbox (video wider than buffer) — content fills width. */
            ch = (int)((double)bw / videoAspect + 0.5);
            if (ch < 1) ch = 1;
            if (ch > bh) ch = bh;
            cy = (bh - ch) / 2;
        } else if (videoAspect < bufAspect) {
            /* Pillarbox (video taller than buffer) — content fills height. */
            cw = (int)((double)bh * videoAspect + 0.5);
            if (cw < 1) cw = 1;
            if (cw > bw) cw = bw;
            cx = (bw - cw) / 2;
        }
    }

    /* Scale the content rect down so the longest side is ≤ 512, preserving
     * aspect. Smaller-than-512 sources pass through unchanged. Same sizing
     * policy as ImageLoader::saveThumbnail. */
    const int maxDim = 512;
    int targetW = cw, targetH = ch;
    if (cw > maxDim || ch > maxDim) {
        if (cw >= ch) {
            targetW = maxDim;
            targetH = (int)((double)ch * maxDim / cw + 0.5);
        } else {
            targetH = maxDim;
            targetW = (int)((double)cw * maxDim / ch + 0.5);
        }
        if (targetW < 1) targetW = 1;
        if (targetH < 1) targetH = 1;
    }

    /* Nearest-neighbour sample from the cropped content rect into a fresh
     * BGRA buffer with alpha=255. Combines crop + downsample in one pass. */
    size_t outBytes = (size_t)targetW * targetH * 4;
    unsigned char* out = (unsigned char*)malloc(outBytes);
    if (!out) return false;
    int srcStride = bw * 4;
    for (int ty = 0; ty < targetH; ty++) {
        int sy = cy + ty * ch / targetH;
        for (int tx = 0; tx < targetW; tx++) {
            int sx = cx + tx * cw / targetW;
            const unsigned char* srcPx = data->front_buffer + sy * srcStride + sx * 4;
            unsigned char* dstPx = out + ((size_t)ty * targetW + tx) * 4;
            dstPx[0] = srcPx[0];
            dstPx[1] = srcPx[1];
            dstPx[2] = srcPx[2];
            dstPx[3] = 255;
        }
    }

    *bgraOut = out;
    *widthOut = targetW;
    *heightOut = targetH;
    return true;
}

void VideoPlayerInstance_Destroy(EmbeddedInstance* inst)
{
    if (!inst) return;
    if (inst->vtable && inst->vtable->shutdown)
        inst->vtable->shutdown(inst);
    if (inst->user_data)
        delete (VideoPlayerInstanceData*)inst->user_data;
    free(inst);
}
