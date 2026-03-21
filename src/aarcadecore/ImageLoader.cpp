#include "ImageLoader.h"
#include "aarcadecore_internal.h"
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define MKDIR(path) mkdir(path, 0755)
#endif

// CRC32 lookup table (Kodi-compatible)
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

// Global pointer for JS callbacks to reach the ImageLoader
static ImageLoader* g_activeImageLoader = nullptr;

// JS callback: window.cppBridge.onImageLoaded(success, url, x, y, w, h)
static JSValueRef js_onImageLoaded(JSContextRef ctx, JSObjectRef function,
    JSObjectRef thisObject, size_t argc, const JSValueRef args[], JSValueRef* exc)
{
    if (!g_activeImageLoader || argc < 6) return JSValueMakeUndefined(ctx);

    bool success = JSValueToBoolean(ctx, args[0]);
    JSStringRef urlStr = JSValueToStringCopy(ctx, args[1], nullptr);
    size_t maxLen = JSStringGetMaximumUTF8CStringSize(urlStr);
    std::string url(maxLen, '\0');
    size_t len = JSStringGetUTF8CString(urlStr, &url[0], maxLen);
    url.resize(len > 0 ? len - 1 : 0);
    JSStringRelease(urlStr);

    int x = (int)JSValueToNumber(ctx, args[2], nullptr);
    int y = (int)JSValueToNumber(ctx, args[3], nullptr);
    int w = (int)JSValueToNumber(ctx, args[4], nullptr);
    int h = (int)JSValueToNumber(ctx, args[5], nullptr);

    g_activeImageLoader->onImageLoaded(success, url, x, y, w, h);
    return JSValueMakeUndefined(ctx);
}

// JS callback: window.cppBridge.onImageLoaderReady()
static JSValueRef js_onImageLoaderReady(JSContextRef ctx, JSObjectRef function,
    JSObjectRef thisObject, size_t argc, const JSValueRef args[], JSValueRef* exc)
{
    if (g_activeImageLoader)
        g_activeImageLoader->onImageLoaderReady();
    return JSValueMakeUndefined(ctx);
}

// ============================================================

ImageLoader::ImageLoader()
    : isInitialized_(false), isProcessing_(false),
      currentRectX_(0), currentRectY_(0), currentRectW_(0), currentRectH_(0)
{
    cacheBasePath_ = ".\\cache\\urls";
}

ImageLoader::~ImageLoader()
{
    shutdown();
}

bool ImageLoader::init()
{
    if (g_host.host_printf) g_host.host_printf("ImageLoader: Initializing...\n");

    // Ensure cache dirs exist
    MKDIR(".\\cache");
    MKDIR(cacheBasePath_.c_str());

    // Ultralight Platform must already be configured (by UltralightInstance)
    renderer_ = Renderer::Create();
    if (!renderer_) {
        if (g_host.host_printf) g_host.host_printf("ImageLoader: Failed to create renderer\n");
        return false;
    }

    ViewConfig vc;
    vc.is_accelerated = false;
    vc.is_transparent = false;
    view_ = renderer_->CreateView(512, 512, vc, nullptr);
    view_->set_load_listener(this);

    g_activeImageLoader = this;

    view_->LoadURL("file:///aarcadecore/ui/image-loader.html");

    if (g_host.host_printf) g_host.host_printf("ImageLoader: View created, loading image-loader.html\n");
    return true;
}

void ImageLoader::update()
{
    if (renderer_) {
        renderer_->Update();
        renderer_->RefreshDisplay(0);
        renderer_->Render();
    }
}

void ImageLoader::shutdown()
{
    if (g_activeImageLoader == this) g_activeImageLoader = nullptr;
    view_ = nullptr;
    renderer_ = nullptr;
    isInitialized_ = false;
}

// --- URL normalization and hashing ---

std::string ImageLoader::normalizeUrl(const std::string& url)
{
    std::string n = url;
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    for (char& c : n) { if (c == '\\') c = '/'; }
    return n;
}

std::string ImageLoader::calculateKodiHash(const std::string& normalizedUrl)
{
    uint32_t crc = 0xFFFFFFFF;
    for (char c : normalizedUrl)
        crc = crc32_table[(crc ^ (uint8_t)c) & 0xFF] ^ (crc >> 8);
    crc ^= 0xFFFFFFFF;
    char hash[9];
    snprintf(hash, sizeof(hash), "%08x", crc);
    return std::string(hash);
}

std::string ImageLoader::getCacheFilePath(const std::string& url)
{
    std::string hash = calculateKodiHash(normalizeUrl(url));
    std::string subfolder = hash.substr(0, 1);
    std::string dir = cacheBasePath_ + "\\" + subfolder;
    MKDIR(dir.c_str());
    return dir + "\\" + hash + ".png";
}

std::string ImageLoader::getCachedFilePath(const std::string& url)
{
    std::string path = getCacheFilePath(url);
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return path;
#else
    FILE* f = fopen(path.c_str(), "rb");
    if (f) { fclose(f); return path; }
#endif
    return "";
}

// --- Job queue ---

void ImageLoader::loadAndCacheImage(const std::string& url, std::function<void(const ImageLoadResult&)> callback)
{
    std::string hash = calculateKodiHash(normalizeUrl(url));
    bool shouldProcess = false;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        auto it = jobMap_.find(hash);
        if (it != jobMap_.end()) {
            it->second.callbacks.push_back(callback);
            return;
        }
        LoadJob job;
        job.url = url;
        job.hash = hash;
        job.callbacks.push_back(callback);
        jobMap_[hash] = job;
        jobQueue_.push(hash);

        if (jobQueue_.size() == 1 && isInitialized_ && !isProcessing_)
            shouldProcess = true;
    }

    if (shouldProcess) processNextJob();
}

void ImageLoader::processNextJob()
{
    std::string hash;
    LoadJob job;

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (isProcessing_ || jobQueue_.empty()) return;
        hash = jobQueue_.front();
        jobQueue_.pop();
        auto it = jobMap_.find(hash);
        if (it == jobMap_.end()) return;
        job = it->second;
        isProcessing_ = true;
    }

    // Check disk cache
    std::string cached = getCachedFilePath(job.url);
    if (!cached.empty()) {
        {
            std::lock_guard<std::mutex> lock(completionMutex_);
            for (auto& cb : job.callbacks)
                completionQueue_.push({true, cached, job.url, cb});
        }
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            jobMap_.erase(job.hash);
            isProcessing_ = false;
        }
        processNextJob();
        return;
    }

    currentUrl_ = job.url;
    loadImageInView(job.url);
}

void ImageLoader::loadImageInView(const std::string& url)
{
    if (!view_ || !isInitialized_) {
        if (g_host.host_printf) g_host.host_printf("ImageLoader: View not ready for %s\n", url.c_str());
        std::string hash = calculateKodiHash(normalizeUrl(url));
        LoadJob job;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            auto it = jobMap_.find(hash);
            if (it != jobMap_.end()) { job = it->second; jobMap_.erase(it); }
            isProcessing_ = false;
        }
        {
            std::lock_guard<std::mutex> lock(completionMutex_);
            for (auto& cb : job.callbacks)
                completionQueue_.push({false, "", url, cb});
        }
        processNextJob();
        return;
    }

    auto ctx_lock = view_->LockJSContext();
    JSContextRef ctx = (*ctx_lock);
    JSObjectRef global = JSContextGetGlobalObject(ctx);

    JSStringRef fnName = JSStringCreateWithUTF8CString("loadImageUrl");
    JSValueRef fnVal = JSObjectGetProperty(ctx, global, fnName, nullptr);
    JSStringRelease(fnName);

    if (JSValueIsObject(ctx, fnVal)) {
        JSStringRef urlStr = JSStringCreateWithUTF8CString(url.c_str());
        JSValueRef urlArg = JSValueMakeString(ctx, urlStr);
        JSStringRelease(urlStr);
        JSValueRef args[] = { urlArg };
        JSObjectCallAsFunction(ctx, (JSObjectRef)fnVal, nullptr, 1, args, nullptr);
    } else {
        if (g_host.host_printf) g_host.host_printf("ImageLoader: loadImageUrl not found in JS!\n");
    }
}

// --- Render and save ---

void ImageLoader::renderAndSave()
{
    renderer_->RefreshDisplay(0);
    renderer_->Render();

    BitmapSurface* bmpSurface = (BitmapSurface*)view_->surface();
    RefPtr<Bitmap> bitmap = bmpSurface->bitmap();

    RefPtr<Bitmap> cropped;
    if (currentRectW_ > 0 && currentRectH_ > 0) {
        int x = (std::max)(0, currentRectX_);
        int y = (std::max)(0, currentRectY_);
        int w = (std::min)(currentRectW_, (int)bitmap->width() - x);
        int h = (std::min)(currentRectH_, (int)bitmap->height() - y);

        cropped = Bitmap::Create(w, h, BitmapFormat::BGRA8_UNORM_SRGB);
        uint8_t* src = (uint8_t*)bitmap->LockPixels();
        uint8_t* dst = (uint8_t*)cropped->LockPixels();
        uint32_t srcRowBytes = bitmap->row_bytes();
        uint32_t dstRowBytes = cropped->row_bytes();
        for (int row = 0; row < h; row++)
            memcpy(dst + row * dstRowBytes, src + (y + row) * srcRowBytes + x * 4, w * 4);
        cropped->UnlockPixels();
        bitmap->UnlockPixels();
    } else {
        cropped = bitmap;
    }

    std::string outputPath = getCacheFilePath(currentUrl_);
    cropped->WritePNG(outputPath.c_str());

    if (g_host.host_printf) g_host.host_printf("ImageLoader: Saved %s\n", outputPath.c_str());

    // Complete all callbacks for this URL
    std::string hash = calculateKodiHash(normalizeUrl(currentUrl_));
    LoadJob job;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        auto it = jobMap_.find(hash);
        if (it != jobMap_.end()) { job = it->second; jobMap_.erase(it); }
    }
    {
        std::lock_guard<std::mutex> lock(completionMutex_);
        for (auto& cb : job.callbacks)
            completionQueue_.push({true, outputPath, currentUrl_, cb});
    }
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        isProcessing_ = false;
    }
    processNextJob();
}

// --- JS bridge callbacks ---

void ImageLoader::onImageLoaded(bool success, const std::string& url, int x, int y, int w, int h)
{
    if (success) {
        currentRectX_ = x; currentRectY_ = y; currentRectW_ = w; currentRectH_ = h;
        renderAndSave();
    } else {
        std::string hash = calculateKodiHash(normalizeUrl(url));
        LoadJob job;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            auto it = jobMap_.find(hash);
            if (it != jobMap_.end()) { job = it->second; jobMap_.erase(it); }
            isProcessing_ = false;
        }
        {
            std::lock_guard<std::mutex> lock(completionMutex_);
            for (auto& cb : job.callbacks)
                completionQueue_.push({false, "", url, cb});
        }
        processNextJob();
    }
}

void ImageLoader::onImageLoaderReady()
{
    if (g_host.host_printf) g_host.host_printf("ImageLoader: HTML ready\n");
    isInitialized_ = true;

    bool hasJobs = false;
    { std::lock_guard<std::mutex> lock(queueMutex_); hasJobs = !jobQueue_.empty(); }
    if (hasJobs) processNextJob();
}

void ImageLoader::processCompletions()
{
    std::lock_guard<std::mutex> lock(completionMutex_);
    while (!completionQueue_.empty()) {
        ImageLoadResult result = completionQueue_.front();
        completionQueue_.pop();
        if (result.callback) result.callback(result);
    }
}

// --- LoadListener ---

void ImageLoader::setupJSBridge()
{
    auto ctx_lock = view_->LockJSContext();
    JSContextRef ctx = (*ctx_lock);
    JSObjectRef global = JSContextGetGlobalObject(ctx);

    // Create window.cppBridge with onImageLoaded and onImageLoaderReady
    JSObjectRef bridge = JSObjectMake(ctx, nullptr, nullptr);

    JSStringRef name1 = JSStringCreateWithUTF8CString("onImageLoaded");
    JSObjectRef fn1 = JSObjectMakeFunctionWithCallback(ctx, name1, js_onImageLoaded);
    JSObjectSetProperty(ctx, bridge, name1, fn1, 0, nullptr);
    JSStringRelease(name1);

    JSStringRef name2 = JSStringCreateWithUTF8CString("onImageLoaderReady");
    JSObjectRef fn2 = JSObjectMakeFunctionWithCallback(ctx, name2, js_onImageLoaderReady);
    JSObjectSetProperty(ctx, bridge, name2, fn2, 0, nullptr);
    JSStringRelease(name2);

    JSStringRef bridgeName = JSStringCreateWithUTF8CString("cppBridge");
    JSObjectSetProperty(ctx, global, bridgeName, bridge, 0, nullptr);
    JSStringRelease(bridgeName);

    if (g_host.host_printf) g_host.host_printf("ImageLoader: JS bridge set up\n");
}

void ImageLoader::OnDOMReady(ultralight::View* caller, uint64_t frame_id,
                             bool is_main_frame, const String& url)
{
    if (!is_main_frame) return;
    setupJSBridge();
}

void ImageLoader::OnFinishLoading(ultralight::View* caller, uint64_t frame_id,
                                  bool is_main_frame, const String& url)
{
    if (!is_main_frame) return;
    if (g_host.host_printf) g_host.host_printf("ImageLoader: Finished loading HTML\n");
    onImageLoaderReady();
}
