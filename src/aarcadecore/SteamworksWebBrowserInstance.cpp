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

#define SWB_DEFAULT_WIDTH  1024
#define SWB_DEFAULT_HEIGHT 1024

/* Per-instance state */
struct SteamworksData {
    HHTMLBrowser browserHandle;
    bool browserReady;
    bool steamInitialized;
    const char* url;

    /* BGRA pixel buffer — copied from HTML_NeedsPaint_t */
    uint8_t* pixelBuffer;
    uint32_t bufferWidth;
    uint32_t bufferHeight;
    bool hasNewFrame;

    /* Callback helpers */
    CCallResult<SteamworksData, HTML_BrowserReady_t> callResultBrowserReady;

    void OnBrowserReady(HTML_BrowserReady_t* pResult, bool bIOFailure);

    STEAM_CALLBACK(SteamworksData, OnNeedsPaint, HTML_NeedsPaint_t);
    STEAM_CALLBACK(SteamworksData, OnStartRequest, HTML_StartRequest_t);
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
 * EmbeddedInstance vtable implementations
 * ======================================================================== */

static bool swb_init(EmbeddedInstance* inst)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;

    if (g_host.host_printf) g_host.host_printf("SWB: Initializing Steam API...\n");

    if (!SteamAPI_Init()) {
        if (g_host.host_printf) g_host.host_printf("SWB: SteamAPI_Init() failed. Is Steam running?\n");
        return false;
    }
    data->steamInitialized = true;
    if (g_host.host_printf) g_host.host_printf("SWB: Steam API initialized\n");

    ISteamHTMLSurface* surface = SteamHTMLSurface();
    if (!surface) {
        if (g_host.host_printf) g_host.host_printf("SWB: Failed to get ISteamHTMLSurface\n");
        return false;
    }

    if (!surface->Init()) {
        if (g_host.host_printf) g_host.host_printf("SWB: ISteamHTMLSurface::Init() failed\n");
        return false;
    }

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
        if (surface) {
            surface->RemoveBrowser(data->browserHandle);
            surface->Shutdown();
        }
        data->browserReady = false;
    }

    free(data->pixelBuffer);
    data->pixelBuffer = NULL;

    if (data->steamInitialized) {
        SteamAPI_Shutdown();
        data->steamInitialized = false;
    }

    if (g_host.host_printf) g_host.host_printf("SWB: Shutdown complete\n");
}

static void swb_update(EmbeddedInstance* inst)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    if (data->steamInitialized)
        SteamAPI_RunCallbacks();
}

static bool swb_is_active(EmbeddedInstance* inst)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;
    return data->steamInitialized;
}

static void swb_render(EmbeddedInstance* inst,
    void* pixelData, int width, int height, int is16bit, int bpp)
{
    SteamworksData* data = (SteamworksData*)inst->user_data;

    if (!data->pixelBuffer || data->bufferWidth == 0)
        return;

    if (is16bit) {
        /* Convert BGRA -> RGB565 with scaling */
        uint16_t* dest = (uint16_t*)pixelData;
        int scale_x = ((int)data->bufferWidth << 16) / width;
        int scale_y = ((int)data->bufferHeight << 16) / height;

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

static const EmbeddedInstanceVtable g_swbVtable = {
    swb_init,
    swb_shutdown,
    swb_update,
    swb_is_active,
    swb_render,
    swb_key_down,
    swb_key_up,
    swb_key_char
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
    data->steamInitialized = false;
    data->url = url;
    data->pixelBuffer = NULL;
    data->bufferWidth = 0;
    data->bufferHeight = 0;
    data->hasNewFrame = false;

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
