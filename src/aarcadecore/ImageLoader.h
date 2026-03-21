#ifndef IMAGELOADER_H
#define IMAGELOADER_H

#include <string>
#include <functional>
#include <queue>
#include <mutex>
#include <map>
#include <vector>
#include <algorithm>
#include <Ultralight/Ultralight.h>
#include <AppCore/AppCore.h>
#include <JavaScriptCore/JavaScript.h>

using namespace ultralight;

struct ImageLoadResult {
    bool success;
    std::string filePath;
    std::string url;
    std::function<void(const ImageLoadResult&)> callback;
};

class ImageLoader : public LoadListener {
public:
    ImageLoader();
    ~ImageLoader();

    bool init();
    void update();
    void shutdown();

    void loadAndCacheImage(const std::string& url, std::function<void(const ImageLoadResult&)> callback);
    void processCompletions();
    bool isInitialized() const { return isInitialized_; }

    /* Get cached BGRA pixels for a given cache file path. Returns true if found. Caller must call freePixels after. */
    bool getPixels(const std::string& cachePath, uint8_t** pixelsOut, int* widthOut, int* heightOut);
    void freePixels(const std::string& cachePath);

    // Called from image-loader.html JS
    void onImageLoaded(bool success, const std::string& url, int rectX, int rectY, int rectW, int rectH);
    void onImageLoaderReady();

private:
    RefPtr<Renderer> renderer_;
    RefPtr<View> view_;

    std::string cacheBasePath_;
    bool isInitialized_;
    bool isProcessing_;
    std::string currentUrl_;

    int currentRectX_, currentRectY_, currentRectW_, currentRectH_;

    struct LoadJob {
        std::string url;
        std::string hash;
        std::vector<std::function<void(const ImageLoadResult&)>> callbacks;
    };

    std::queue<std::string> jobQueue_;
    std::map<std::string, LoadJob> jobMap_;
    std::queue<ImageLoadResult> completionQueue_;
    std::mutex queueMutex_;
    std::mutex completionMutex_;

    /* Pixel cache: cachePath → raw BGRA pixels (read once by host, then freed) */
    struct CachedPixels {
        uint8_t* pixels;
        uint32_t width, height;
    };
    std::map<std::string, CachedPixels> pixelCache_;
    std::mutex pixelCacheMutex_;

    std::string normalizeUrl(const std::string& url);
    std::string calculateKodiHash(const std::string& normalizedUrl);
    std::string getCacheFilePath(const std::string& url);
    std::string getCachedFilePath(const std::string& url);
    void processNextJob();
    void loadImageInView(const std::string& url);
    void renderAndSave();
    void setupJSBridge();

    // LoadListener
    void OnDOMReady(ultralight::View* caller, uint64_t frame_id,
                    bool is_main_frame, const String& url) override;
    void OnFinishLoading(ultralight::View* caller, uint64_t frame_id,
                         bool is_main_frame, const String& url) override;
};

#endif
