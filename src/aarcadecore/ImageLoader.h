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

    /* Item-channel image request.
     * JS builds the full (field × variation) candidate list from the item's fields and
     * channel, walks it to completion, and reports back either a winning sourceUrl or failure.
     * `channel` must be "marquee" or "screen". */
    struct ItemChannelFields {
        std::string id;
        std::string marquee;
        std::string screen;
        std::string preview;
        std::string file;
    };
    void requestItemChannel(const std::string& requestId,
                            const ItemChannelFields& fields,
                            const std::string& channel,
                            std::function<void(const ImageLoadResult&)> callback);

    void processCompletions();
    bool isInitialized() const { return isInitialized_; }

    bool getPixels(const std::string& cachePath, uint8_t** pixelsOut, int* widthOut, int* heightOut);
    /* Clear the in-memory BGRA pixel cache for a path. Leaves the on-disk
     * cache file alone — use deleteCacheFile for that. */
    void clearPixelCache(const std::string& cachePath);

    /* Delete an on-disk cache file, rejecting any path outside ./aarcadecore/cache/. */
    bool deleteCacheFile(const std::string& cachePath);

    /* Delete the cache file for a URL, if present.
     * cacheType: "url" (default) for ./aarcadecore/cache/urls, "snapshot" for ./aarcadecore/cache/snapshots. */
    bool deleteCacheForUrl(const std::string& url, const std::string& cacheType = "url");

    /* Save a snapshot of raw BGRA pixels to cache/snapshots/{hash}.png keyed by itemId */
    bool saveSnapshot(const std::string& key, const uint8_t* bgraPixels, int width, int height);

    /* Save a thumbnail (resized to max 512px) to cache/thumbnails/{hash}.png keyed by modelId */
    bool saveThumbnail(const std::string& key, const uint8_t* rgbaPixels, int width, int height);

    /* Get snapshot path for a key (empty if not cached) */
    std::string getSnapshotPath(const std::string& key);


    /* Called from image-loader.html JS */
    void onImageQueued(const std::string& identifier);  /* image downloaded, ready for capture */
    void onImageLoaded(bool success, const std::string& identifier,
                       int rectX, int rectY, int rectW, int rectH,
                       const std::string& sourceUrl = "");
    void onImageLoaderReady();

private:
    RefPtr<Renderer> renderer_;
    RefPtr<View> view_;

    std::string cacheBasePath_;
    bool isInitialized_;
    bool captureReady_;    /* an image has been shown, waiting one frame to capture */
    std::string captureIdentifier_; /* identifier (URL or requestId) of image displayed for capture */
    std::string captureSourceUrl_;  /* non-empty for item-channel captures: URL to cache-key by */
    int captureRectX_, captureRectY_, captureRectW_, captureRectH_;

    /* Per-URL callback tracking (legacy single-URL flow) */
    struct PendingImage {
        std::string url;
        std::vector<std::function<void(const ImageLoadResult&)>> callbacks;
    };
    std::map<std::string, PendingImage> pendingImages_; /* hash → pending */
    std::mutex pendingMutex_;

    /* Per-requestId callback tracking (item-channel flow) */
    struct PendingItemChannel {
        std::function<void(const ImageLoadResult&)> callback;
    };
    std::map<std::string, PendingItemChannel> pendingItemChannels_; /* requestId → pending */
    std::mutex pendingItemChannelMutex_;

    /* Queue of URLs that have finished downloading (JS onload fired) */
    std::queue<std::string> downloadedQueue_;
    std::mutex downloadedMutex_;

    std::queue<ImageLoadResult> completionQueue_;
    std::mutex completionMutex_;

    /* Pixel cache */
    struct CachedPixels {
        uint8_t* pixels;
        uint32_t width, height;
    };
    std::map<std::string, CachedPixels> pixelCache_;
    std::mutex pixelCacheMutex_;

    std::string normalizeUrl(const std::string& url);
    std::string calculateKodiHash(const std::string& normalizedUrl);
    std::string getCacheFilePath(const std::string& url);
    std::string getCachedFilePath(const std::string& url, const std::string& cacheType = "url");
    void sendUrlToJS(const std::string& url);
    void sendItemChannelToJS(const std::string& requestId,
                             const ItemChannelFields& fields,
                             const std::string& channel);
    void showNextAndCapture();
    void renderAndSave();
    void setupJSBridge();
    void callJSCaptureComplete();

    void OnDOMReady(ultralight::View* caller, uint64_t frame_id,
                    bool is_main_frame, const String& url) override;
    void OnFinishLoading(ultralight::View* caller, uint64_t frame_id,
                         bool is_main_frame, const String& url) override;
};

#endif
