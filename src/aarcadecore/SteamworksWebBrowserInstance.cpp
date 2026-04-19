/*
 * SteamworksWebBrowserInstance — EmbeddedInstance for Steamworks HTML Surface
 *
 * Loads a URL using the Steamworks HTML Surface API and renders the page
 * to in-game textures via the EmbeddedInstance render callback.
 */

#include "aarcadecore_internal.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include <steam_api.h>
#include <isteamhtmlsurface.h>

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SWB_DEFAULT_WIDTH  1280
#define SWB_DEFAULT_HEIGHT 720

/* Per-instance state */
struct SteamworksData {
    HHTMLBrowser browserHandle;
    bool browserReady;
    const char* url;

    /* BGRA pixel buffer — copied from HTML_NeedsPaint_t */
    uint8_t* pixelBuffer;
    uint32_t bufferWidth;
    uint32_t bufferHeight;
    bool hasNewFrame;

    /* Page title — set by HTML_ChangedTitle_t callback */
    char title[256];

    /* Navigation state — set by HTML_CanGoBackAndForward_t */
    bool canGoBack;
    bool canGoForward;

    /* Callback helpers */
    CCallResult<SteamworksData, HTML_BrowserReady_t> callResultBrowserReady;

    void OnBrowserReady(HTML_BrowserReady_t* pResult, bool bIOFailure);

    STEAM_CALLBACK(SteamworksData, OnNeedsPaint, HTML_NeedsPaint_t);
    STEAM_CALLBACK(SteamworksData, OnStartRequest, HTML_StartRequest_t);
    STEAM_CALLBACK(SteamworksData, OnChangedTitle, HTML_ChangedTitle_t);
    STEAM_CALLBACK(SteamworksData, OnCanGoBackAndForward, HTML_CanGoBackAndForward_t);
    STEAM_CALLBACK(SteamworksData, OnJSAlert, HTML_JSAlert_t);
    STEAM_CALLBACK(SteamworksData, OnJSConfirm, HTML_JSConfirm_t);
    STEAM_CALLBACK(SteamworksData, OnFileOpenDialog, HTML_FileOpenDialog_t);
};

void SteamworksData::OnBrowserReady(HTML_BrowserReady_t* pResult, bool bIOFailure)
{
    if (bIOFailure || !pResult) {
        if (g_host.host_printf) g_host.host_printf("SWB: Browser creation failed\n");
        return;
    }

    browserHandle = pResult->unBrowserHandle;
    browserReady = true;

    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface) {
        surface->SetSize(browserHandle, SWB_DEFAULT_WIDTH, SWB_DEFAULT_HEIGHT);
        surface->LoadURL(browserHandle, url, NULL);
        if (g_host.host_printf) g_host.host_printf("SWB: Browser ready, loading %s\n", url);
    }
}

void SteamworksData::OnNeedsPaint(HTML_NeedsPaint_t* pParam)
{
    if (!pParam || pParam->unBrowserHandle != browserHandle)
        return;

    uint32_t totalSize = pParam->unWide * pParam->unTall * 4;

    /* Reallocate if dimensions changed */
    if (pParam->unWide != bufferWidth || pParam->unTall != bufferHeight) {
        free(pixelBuffer);
        pixelBuffer = (uint8_t*)malloc(totalSize);
        bufferWidth = pParam->unWide;
        bufferHeight = pParam->unTall;
    }

    if (pixelBuffer && pParam->pBGRA) {
        memcpy(pixelBuffer, pParam->pBGRA, totalSize);
        hasNewFrame = true;
    }
}

void SteamworksData::OnStartRequest(HTML_StartRequest_t* pParam)
{
    if (!pParam || pParam->unBrowserHandle != browserHandle)
        return;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface)
        surface->AllowStartRequest(browserHandle, true);
}

void SteamworksData::OnChangedTitle(HTML_ChangedTitle_t* pParam)
{
    if (!pParam || pParam->unBrowserHandle != browserHandle)
        return;
    if (pParam->pchTitle) {
        strncpy(title, pParam->pchTitle, sizeof(title) - 1);
        title[sizeof(title) - 1] = '\0';
        if (g_host.host_printf)
            g_host.host_printf("SWB: Title changed: \"%s\"\n", title);
    }
}

void SteamworksData::OnCanGoBackAndForward(HTML_CanGoBackAndForward_t* pParam)
{
    if (!pParam || pParam->unBrowserHandle != browserHandle)
        return;
    canGoBack = pParam->bCanGoBack;
    canGoForward = pParam->bCanGoForward;
}

void SteamworksData::OnJSAlert(HTML_JSAlert_t* pParam)
{
    if (!pParam || pParam->unBrowserHandle != browserHandle)
        return;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface)
        surface->JSDialogResponse(browserHandle, true);
}

void SteamworksData::OnJSConfirm(HTML_JSConfirm_t* pParam)
{
    if (!pParam || pParam->unBrowserHandle != browserHandle)
        return;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface)
        surface->JSDialogResponse(browserHandle, true);
}

void SteamworksData::OnFileOpenDialog(HTML_FileOpenDialog_t* pParam)
{
    if (!pParam || pParam->unBrowserHandle != browserHandle)
        return;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface)
        surface->FileLoadDialogResponse(browserHandle, NULL);
}

/* ========================================================================
 * Global Steam API lifecycle — init once at DLL startup, shutdown at exit
 * ======================================================================== */

static bool g_steamApiReady = false;

bool SteamworksWebBrowser_InitSteamAPI(void)
{
    if (g_steamApiReady) return true;

    if (g_host.host_printf) g_host.host_printf("SWB: Initializing Steam API...\n");

    if (!SteamAPI_Init()) {
        if (g_host.host_printf) g_host.host_printf("SWB: SteamAPI_Init() failed. Is Steam running?\n");
        return false;
    }
    if (g_host.host_printf) g_host.host_printf("SWB: Steam API initialized\n");

    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (!surface || !surface->Init()) {
        if (g_host.host_printf) g_host.host_printf("SWB: ISteamHTMLSurface::Init() failed\n");
        SteamAPI_Shutdown();
        return false;
    }

    g_steamApiReady = true;
    return true;
}

bool SteamworksWebBrowser_IsSteamReady(void) { return g_steamApiReady; }

void SteamworksWebBrowser_ShutdownSteamAPI(void)
{
    if (!g_steamApiReady) return;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface) surface->Shutdown();
    SteamAPI_Shutdown();
    g_steamApiReady = false;
    if (g_host.host_printf) g_host.host_printf("SWB: Steam API shut down\n");
}

/* ========================================================================
 * EmbeddedInstance vtable implementations
 * ======================================================================== */

static bool swb_init(EmbeddedInstance* inst)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;

    if (!g_steamApiReady) {
        if (g_host.host_printf) g_host.host_printf("SWB: Steam API not ready\n");
        return false;
    }

    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (!surface) return false;

    SteamAPICall_t hCall = surface->CreateBrowser("AArcadeCore", NULL);
    data->callResultBrowserReady.Set(hCall, data, &SteamworksData::OnBrowserReady);

    if (g_host.host_printf) g_host.host_printf("SWB: Browser creation requested...\n");
    return true;
}

static void swb_shutdown(EmbeddedInstance* inst)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;

    if (data->browserReady) {
        ISteamHTMLSurface* surface = SteamHTMLSurface();
        if (surface) surface->RemoveBrowser(data->browserHandle);
        data->browserReady = false;
    }

    free(data->pixelBuffer);
    data->pixelBuffer = NULL;
    free((void*)data->url);
    data->url = NULL;

    if (g_host.host_printf) g_host.host_printf("SWB: Browser removed\n");
}

static void swb_update(EmbeddedInstance* inst)
{
    /* SteamAPI_RunCallbacks is pumped globally in aarcadecore_update, not per-instance */
}

static bool swb_is_active(EmbeddedInstance* inst)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    return g_steamApiReady && data->browserReady;
}

static void swb_render(EmbeddedInstance* inst,
    void* pixelData, int width, int height, int is16bit, int bpp)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;

    if (!data->pixelBuffer || data->bufferWidth == 0)
        return;

    /* Skip duplicate 16-bit renders in same frame (per-thing texture cached by host).
     * 32-bit fullscreen renders always proceed — different buffer/format. */
    extern uint32_t aarcadecore_getEngineFrame(void);
    uint32_t frame = aarcadecore_getEngineFrame();
    if (is16bit && inst->lastRenderedFrame == frame)
        return;
    if (is16bit)
        inst->lastRenderedFrame = frame;

    int scale_x = ((int)data->bufferWidth << 16) / width;
    int scale_y = ((int)data->bufferHeight << 16) / height;

    if (is16bit) {
        /* Convert BGRA -> RGB565 with scaling */
        uint16_t* dest = (uint16_t*)pixelData;

        for (int y = 0; y < height; y++) {
            int src_y = (y * scale_y) >> 16;
            if (src_y >= (int)data->bufferHeight) src_y = data->bufferHeight - 1;

            for (int x = 0; x < width; x++) {
                int src_x = (x * scale_x) >> 16;
                if (src_x >= (int)data->bufferWidth) src_x = data->bufferWidth - 1;

                int idx = (src_y * data->bufferWidth + src_x) * 4;
                uint8_t b = data->pixelBuffer[idx + 0];
                uint8_t g = data->pixelBuffer[idx + 1];
                uint8_t r = data->pixelBuffer[idx + 2];

                dest[y * width + x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            }
        }
    } else {
        /* 32-bit BGRA output with scaling */
        uint8_t* dest = (uint8_t*)pixelData;

        for (int y = 0; y < height; y++) {
            int src_y = (y * scale_y) >> 16;
            if (src_y >= (int)data->bufferHeight) src_y = data->bufferHeight - 1;

            for (int x = 0; x < width; x++) {
                int src_x = (x * scale_x) >> 16;
                if (src_x >= (int)data->bufferWidth) src_x = data->bufferWidth - 1;

                int src_idx = (src_y * data->bufferWidth + src_x) * 4;
                int dst_idx = (y * width + x) * 4;
                dest[dst_idx + 0] = data->pixelBuffer[src_idx + 0];
                dest[dst_idx + 1] = data->pixelBuffer[src_idx + 1];
                dest[dst_idx + 2] = data->pixelBuffer[src_idx + 2];
                dest[dst_idx + 3] = data->pixelBuffer[src_idx + 3];
            }
        }
    }

}

static void swb_key_down(EmbeddedInstance* inst, int vk_code, int modifiers)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    if (!data->browserReady) return;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface)
        surface->KeyDown(data->browserHandle, (uint32)vk_code, (ISteamHTMLSurface::EHTMLKeyModifiers)modifiers, false);
}

static void swb_key_up(EmbeddedInstance* inst, int vk_code, int modifiers)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    if (!data->browserReady) return;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface)
        surface->KeyUp(data->browserHandle, (uint32)vk_code, (ISteamHTMLSurface::EHTMLKeyModifiers)modifiers);
}

static void swb_key_char(EmbeddedInstance* inst, unsigned int unicode_char, int modifiers)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    if (!data->browserReady) return;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface)
        surface->KeyChar(data->browserHandle, (uint32)unicode_char, (ISteamHTMLSurface::EHTMLKeyModifiers)modifiers);
}

static void swb_mouse_move(EmbeddedInstance* inst, int x, int y)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    if (!data->browserReady) return;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface) surface->MouseMove(data->browserHandle, x, y);
}

static void swb_mouse_down(EmbeddedInstance* inst, int button)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    if (!data->browserReady) return;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface) surface->MouseDown(data->browserHandle, (ISteamHTMLSurface::EHTMLMouseButton)button);
}

static void swb_mouse_up(EmbeddedInstance* inst, int button)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    if (!data->browserReady) return;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface) surface->MouseUp(data->browserHandle, (ISteamHTMLSurface::EHTMLMouseButton)button);
}

static void swb_mouse_wheel(EmbeddedInstance* inst, int delta)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    if (!data->browserReady) return;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface) surface->MouseWheel(data->browserHandle, delta);
}

static const char* swb_get_title(EmbeddedInstance* inst)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    return data->title[0] ? data->title : NULL;
}

static int swb_get_width(EmbeddedInstance* inst) { (void)inst; return SWB_DEFAULT_WIDTH; }
static int swb_get_height(EmbeddedInstance* inst) { (void)inst; return SWB_DEFAULT_HEIGHT; }

static void swb_navigate(EmbeddedInstance* inst, const char* url)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface && data->browserHandle != INVALID_HTMLBROWSER) {
        surface->LoadURL(data->browserHandle, url, NULL);
        if (g_host.host_printf)
            g_host.host_printf("SWB: Navigating to %s\n", url);
    }
}

static void swb_go_back(EmbeddedInstance* inst)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface && data->browserHandle != INVALID_HTMLBROWSER)
        surface->GoBack(data->browserHandle);
}

static void swb_go_forward(EmbeddedInstance* inst)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface && data->browserHandle != INVALID_HTMLBROWSER)
        surface->GoForward(data->browserHandle);
}

static void swb_reload(EmbeddedInstance* inst)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (surface && data->browserHandle != INVALID_HTMLBROWSER)
        surface->Reload(data->browserHandle);
}

static bool swb_can_go_back(EmbeddedInstance* inst)
{
    return ((SteamworksData*)inst->user_data)->canGoBack;
}

static bool swb_can_go_forward(EmbeddedInstance* inst)
{
    return ((SteamworksData*)inst->user_data)->canGoForward;
}

static const EmbeddedInstanceVtable g_swbVtable = {
    swb_init,
    swb_shutdown,
    swb_update,
    swb_is_active,
    swb_render,
    swb_key_down,
    swb_key_up,
    swb_key_char,
    swb_mouse_move,
    swb_mouse_down,
    swb_mouse_up,
    swb_mouse_wheel,
    swb_get_title,
    swb_get_width,
    swb_get_height,
    swb_navigate,
    swb_go_back,
    swb_go_forward,
    swb_reload,
    swb_can_go_back,
    swb_can_go_forward
};

/* ========================================================================
 * Public API (internal to DLL)
 * ======================================================================== */

EmbeddedInstance* SteamworksWebBrowserInstance_Create(const char* url, const char* material_name)
{
    EmbeddedInstance* inst = (EmbeddedInstance*)calloc(1, sizeof(EmbeddedInstance));
    SteamworksData* data = new SteamworksData();
    if (!inst || !data) { free(inst); delete data; return NULL; }

    data->browserHandle = INVALID_HTMLBROWSER;
    data->browserReady = false;
    data->url = _strdup(url);
    data->pixelBuffer = NULL;
    data->bufferWidth = 0;
    data->bufferHeight = 0;
    data->hasNewFrame = false;
    data->canGoBack = false;
    data->canGoForward = false;
    data->title[0] = '\0';

    inst->type = EMBEDDED_STEAMWORKS_BROWSER;
    inst->vtable = &g_swbVtable;
    inst->target_material = material_name;
    inst->user_data = data;

    return inst;
}

void SteamworksWebBrowserInstance_Destroy(EmbeddedInstance* inst)
{
    if (!inst) return;
    if (inst->vtable && inst->vtable->shutdown)
        inst->vtable->shutdown(inst);
    if (inst->user_data)
        delete (SteamworksData*)inst->user_data;
    free(inst);
}

/* Capture a snapshot of the current browser pixels, preserving aspect ratio
 * and resized so the longest side is ≤ 512. The SWB pixelBuffer is always at
 * the browser's native size (no letterbox) so we just nearest-neighbour
 * downsample into a fresh BGRA buffer with alpha = 255. */
extern "C" bool SteamworksWebBrowserInstance_CaptureSnapshot(EmbeddedInstance* inst,
                                                             unsigned char** bgraOut,
                                                             int* widthOut, int* heightOut)
{
    if (!inst || inst->type != EMBEDDED_STEAMWORKS_BROWSER) return false;
    if (!bgraOut || !widthOut || !heightOut) return false;
    SteamworksData* data = (SteamworksData*)inst->user_data;
    if (!data || !data->pixelBuffer || data->bufferWidth == 0 || data->bufferHeight == 0)
        return false;

    int bw = (int)data->bufferWidth;
    int bh = (int)data->bufferHeight;

    /* Longest-side ≤ 512, aspect preserved. Same rule as saveThumbnail. */
    const int maxDim = 512;
    int targetW = bw, targetH = bh;
    if (bw > maxDim || bh > maxDim) {
        if (bw >= bh) {
            targetW = maxDim;
            targetH = (int)((double)bh * maxDim / bw + 0.5);
        } else {
            targetH = maxDim;
            targetW = (int)((double)bw * maxDim / bh + 0.5);
        }
        if (targetW < 1) targetW = 1;
        if (targetH < 1) targetH = 1;
    }

    size_t outBytes = (size_t)targetW * targetH * 4;
    unsigned char* out = (unsigned char*)malloc(outBytes);
    if (!out) return false;

    int srcStride = bw * 4;
    for (int ty = 0; ty < targetH; ty++) {
        int sy = ty * bh / targetH;
        if (sy >= bh) sy = bh - 1;
        for (int tx = 0; tx < targetW; tx++) {
            int sx = tx * bw / targetW;
            if (sx >= bw) sx = bw - 1;
            const unsigned char* srcPx = data->pixelBuffer + sy * srcStride + sx * 4;
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
