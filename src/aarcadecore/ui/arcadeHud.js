/**
 * Arcade HUD - JavaScript manager for Arcade Core functionality
 *
 * Provides a simple, unified API for working with the native C++ bridge (aapi).
 * Handles background tasks like polling for image completions, managing intervals, etc.
 */

const arcadeHud = (function() {
    'use strict';

    // Private state
    let imageCompletionInterval = null;
    let isInitialized = false;

    // CRC32 lookup table (same as C++ ImageLoader)
    const crc32Table = new Uint32Array([
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
    ]);

    /**
     * Normalize URL for consistent hashing (same as C++ implementation)
     * @private
     */
    function normalizeUrl(url) {
        return url.toLowerCase().replace(/\\/g, '/');
    }

    /**
     * Calculate Kodi-style CRC32 hash (same as C++ implementation)
     * @private
     */
    function calculateKodiHash(normalizedUrl) {
        let crc = 0xFFFFFFFF;

        for (let i = 0; i < normalizedUrl.length; i++) {
            const byte = normalizedUrl.charCodeAt(i);
            crc = (crc >>> 8) ^ crc32Table[(crc ^ byte) & 0xFF];
        }

        crc = crc ^ 0xFFFFFFFF;
        // Convert to unsigned 32-bit and format as 8-char hex (lowercase)
        return (crc >>> 0).toString(16).padStart(8, '0');
    }

    /**
     * Predict the cache file path for a URL
     * @private
     */
    function predictCachePath(url) {
        const normalized = normalizeUrl(url);
        const hash = calculateKodiHash(normalized);
        const subfolder = hash.charAt(0);
        return `file:///./cache/urls/${subfolder}/${hash}.png`;
    }

    /**
     * Initialize the arcade HUD
     * Sets up polling intervals and other background tasks
     */
    function initialize() {
        if (isInitialized) {
            console.warn('[arcadeHud] Already initialized');
            return;
        }

        console.log('[arcadeHud] Initializing...');

        // Check if aapi is available
        if (typeof aapi === 'undefined') {
            console.error('[arcadeHud] aapi not available. Make sure this is running in Arcade Core.');
            return false;
        }

        // Start polling for image completions if the method exists
        if (typeof aapi.images.processCompletions === 'function') {
            startImageCompletionPolling();
        } else {
            console.warn('[arcadeHud] aapi.images.processCompletions not available');
        }

        isInitialized = true;
        console.log('[arcadeHud] Initialized successfully');
        return true;
    }

    /**
     * Start polling for image download completions
     * @private
     */
    function startImageCompletionPolling() {
        if (imageCompletionInterval) {
            return; // Already running
        }

        console.log('[arcadeHud] Starting image completion polling');
        imageCompletionInterval = setInterval(function() {
            try {
                aapi.images.processCompletions();
            } catch (e) {
                console.error('[arcadeHud] Error processing image completions:', e);
            }
        }, 50); // Poll every 50ms
    }

    /**
     * Stop polling for image completions
     * @private
     */
    function stopImageCompletionPolling() {
        if (imageCompletionInterval) {
            clearInterval(imageCompletionInterval);
            imageCompletionInterval = null;
            console.log('[arcadeHud] Stopped image completion polling');
        }
    }

    /**
     * Load and cache an image from a URL
     *
     * This function first tries to load from the predicted cache location.
     * If the file exists, it loads instantly without calling C++.
     * If not found, it falls back to C++ to download and cache the image.
     *
     * The image is rendered in a 512x512 view with aspect ratio preservation (object-fit: contain).
     * The actual rendered image rect is calculated and the bitmap is cropped to only save the
     * actual image pixels, eliminating transparent borders.
     *
     * @param {string} url - The image URL to download and cache
     * @returns {Promise<Object>} Promise that resolves with image details:
     *   - filePath: Local file:// URL to the cached PNG
     *   - downloaderName: Name of the downloader used (e.g., "ImageLoader")
     *   - rectX: X coordinate of the image within the 512x512 canvas (before crop)
     *   - rectY: Y coordinate of the image within the 512x512 canvas (before crop)
     *   - rectWidth: Width of the actual image in pixels
     *   - rectHeight: Height of the actual image in pixels
     *
     * Note: The saved PNG file is cropped to rectWidth x rectHeight, maintaining the
     * original image's aspect ratio.
     */
    function loadImage(url) {
        if (!isInitialized) {
            console.warn('[arcadeHud] Not initialized, auto-initializing...');
            initialize();
        }

        if (typeof aapi === 'undefined' || typeof aapi.images.getCacheImage !== 'function') {
            return Promise.reject(new Error('Image caching not available'));
        }

        // Predict the cache file path
        const predictedPath = predictCachePath(url);

        // Try to load from predicted cache location first
        return new Promise((resolve, reject) => {
            const testImg = new Image();

            testImg.onload = function() {
                // Cache hit! Image exists at predicted location
                console.log('[arcadeHud] Cache hit (predicted):', url);
                resolve({
                    filePath: predictedPath,
                    success: true,
                    fromCache: true
                });
            };

            testImg.onerror = function() {
                // Cache miss - need to download through C++
                console.log('[arcadeHud] Cache miss, requesting download:', url);

                // Call C++ to download and cache
                aapi.images.getCacheImage(url)
                    .then(result => {
                        resolve(result);
                    })
                    .catch(error => {
                        reject(error);
                    });
            };

            // Trigger the test load
            testImg.src = predictedPath;
        });
    }

    /**
     * Get supported entry types from the database
     * @returns {Array<string>} Array of supported entry type names
     */
    function getSupportedEntryTypes() {
        if (typeof aapi === 'undefined' || typeof aapi.getSupportedEntryTypes !== 'function') {
            console.error('[arcadeHud] getSupportedEntryTypes not available');
            return [];
        }

        return aapi.getSupportedEntryTypes();
    }

    /**
     * Shutdown the arcade HUD
     * Stops all background tasks and cleans up resources
     */
    function shutdown() {
        console.log('[arcadeHud] Shutting down...');
        stopImageCompletionPolling();
        isInitialized = false;
    }

    /**
     * Check if the arcade HUD is initialized
     * @returns {boolean} True if initialized
     */
    function isReady() {
        return isInitialized;
    }

    /**
     * Check if a specific feature is available
     * @param {string} feature - Feature name (e.g., 'imageCache', 'database')
     * @returns {boolean} True if feature is available
     */
    function hasFeature(feature) {
        if (typeof aapi === 'undefined') {
            return false;
        }

        switch (feature) {
            case 'imageCache':
                return typeof aapi.images.getCacheImage === 'function' &&
                       typeof aapi.images.processCompletions === 'function';
            case 'database':
                return typeof aapi.getFirstEntries === 'function';
            case 'search':
                return typeof aapi.getFirstSearchResults === 'function';
            default:
                return false;
        }
    }

    // Public API
    return {
        initialize: initialize,
        shutdown: shutdown,
        isReady: isReady,
        hasFeature: hasFeature,

        // Image loading
        loadImage: loadImage,

        // Database
        getSupportedEntryTypes: getSupportedEntryTypes,

        // Direct access to aapi (for advanced usage)
        get aapi() {
            return typeof aapi !== 'undefined' ? aapi : null;
        }
    };
})();

// Auto-initialize when the DOM is ready
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', function() {
        arcadeHud.initialize();
    });
} else {
    // DOM already loaded
    arcadeHud.initialize();
}

// Cleanup on page unload
window.addEventListener('beforeunload', function() {
    arcadeHud.shutdown();
});
