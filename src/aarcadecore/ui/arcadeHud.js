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

    /* Cache hash + path helpers live in imageCache.js (shared with image-loader.html). */
    function predictCachePath(url) {
        return window.imageCache.predictCachePath(url);
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

    /* ========================================================================
     * Favorites System
     * ======================================================================== */

    var favoritesLists = {};
    var activeFavoritesListId = 'favorites';

    function initFavorites() {
        try {
            var stored = localStorage.getItem('favoritesLists');
            if (stored) favoritesLists = JSON.parse(stored);
        } catch (e) { favoritesLists = {}; }
        /* Ensure default list exists */
        if (!favoritesLists['favorites']) {
            favoritesLists['favorites'] = { id: 'favorites', title: 'Favorites', screen: '', entries: [] };
            saveFavoritesLists();
        }
        /* Restore active list */
        var storedActive = localStorage.getItem('activeFavoritesList');
        if (storedActive && favoritesLists[storedActive]) {
            activeFavoritesListId = storedActive;
        } else {
            activeFavoritesListId = 'favorites';
        }
    }

    function saveFavoritesLists() {
        try { localStorage.setItem('favoritesLists', JSON.stringify(favoritesLists)); } catch (e) {}
    }

    function getActiveFavoritesList() {
        return favoritesLists[activeFavoritesListId] || favoritesLists['favorites'];
    }

    function setActiveFavoritesList(id) {
        if (favoritesLists[id]) {
            activeFavoritesListId = id;
            try { localStorage.setItem('activeFavoritesList', id); } catch (e) {}
        }
    }

    function createFavoritesList(id, title, screen) {
        if (!id) id = 'fav_' + Date.now() + '_' + Math.random().toString(36).substr(2, 6);
        if (favoritesLists[id]) return favoritesLists[id];
        favoritesLists[id] = { id: id, title: title || 'Untitled', screen: screen || '', entries: [] };
        saveFavoritesLists();
        return favoritesLists[id];
    }

    function updateFavoritesList(id, field, value) {
        var list = favoritesLists[id];
        if (!list) return false;
        list[field] = value;
        saveFavoritesLists();
        return true;
    }

    function deleteFavoritesList(id) {
        if (!favoritesLists[id]) return false;
        delete favoritesLists[id];
        /* If we just removed the active list, fall back to the default. */
        if (activeFavoritesListId === id) setActiveFavoritesList('favorites');
        saveFavoritesLists();
        return true;
    }

    function addToFavorites(mode, entryId) {
        var list = getActiveFavoritesList();
        if (!list) return;
        var key = (mode === 'items') ? 'item' : 'model';
        /* Check for duplicates */
        for (var i = 0; i < list.entries.length; i++) {
            if (list.entries[i][key] === entryId) return;
        }
        var entry = {};
        entry[key] = entryId;
        list.entries.push(entry);
        saveFavoritesLists();
    }

    function removeFromFavorites(mode, entryId) {
        var list = getActiveFavoritesList();
        if (!list) return;
        var key = (mode === 'items') ? 'item' : 'model';
        for (var i = list.entries.length - 1; i >= 0; i--) {
            if (list.entries[i][key] === entryId) {
                list.entries.splice(i, 1);
            }
        }
        saveFavoritesLists();
    }

    function isFavorite(mode, entryId) {
        var list = getActiveFavoritesList();
        if (!list) return false;
        var key = (mode === 'items') ? 'item' : 'model';
        for (var i = 0; i < list.entries.length; i++) {
            if (list.entries[i][key] === entryId) return true;
        }
        return false;
    }

    /* Initialize favorites on arcadeHud init */
    initFavorites();

    /* ========================================================================
     * UI Window Framework
     * ======================================================================== */

    var uiState = {
        windowEl: null,
        helptextEl: null,
        wrapperEl: null,
        isDragging: false,
        dragOffsetX: 0,
        dragOffsetY: 0,
        hasMoved: false
    };

    function escapeHtml(str) {
        var div = document.createElement('div');
        div.textContent = str;
        return div.innerHTML;
    }

    /* --- URL / type detection helpers (used by createItem and friends) --- */

    function extractYouTubeId(url) {
        if (!url) return '';
        var m = url.match(/youtu\.be\/([^?&#]+)/i);
        if (m) return m[1];
        m = url.match(/youtube\.com.*[?&]v=([^&#]+)/i);
        if (m) return m[1];
        m = url.match(/youtube\.com\/embed\/([^?&#]+)/i);
        if (m) return m[1];
        return '';
    }

    function extractTwitchId(url) {
        if (!url) return '';
        var m = url.match(/twitch\.tv\/([A-Za-z0-9_]+)/i);
        return m ? m[1] : '';
    }

    function encodeRFC5987ValueChars(str) {
        return encodeURIComponent(str)
            .replace(/['()*]/g, function(c) { return '%' + c.charCodeAt(0).toString(16).toUpperCase(); })
            .replace(/%(7C|60|5E)/g, function(_, hex) { return String.fromCharCode(parseInt(hex, 16)); });
    }

    /* Drop later duplicates so each human-visible label appears at most once.
     * Keeps the first occurrence; compares on trimmed, case-insensitive label. */
    function dedupTypesByLabel(types) {
        if (!types) return [];
        var seen = {}, out = [];
        for (var i = 0; i < types.length; i++) {
            var t = types[i];
            var label = String(t.title || t.id || '').trim().toLowerCase();
            if (!label || seen[label]) continue;
            seen[label] = true;
            out.push(t);
        }
        return out;
    }

    /* Find a type by case-insensitive exact title match. Returns the id, or ''. */
    function findTypeIdByTitle(types, title) {
        if (!types || !title) return '';
        var target = String(title).toLowerCase();
        for (var i = 0; i < types.length; i++) {
            if (types[i].title && String(types[i].title).toLowerCase() === target) return types[i].id;
        }
        return '';
    }

    /* Auto-detect an item's type id from its file/URL using first-match-wins rules.
     * `types` is the array from aapi.library.getTypes().
     * opts.badyt means "YouTube tubeinfo lookup failed" — suppresses guesses
     * (Twitch/images/movies/websites) that might mis-categorize a YouTube URL.
     * Returns a type id or '' if nothing matched. */
    function detectItemType(file, types, opts) {
        if (!file || !types) return '';
        opts = opts || {};

        if (!opts.badyt && extractYouTubeId(file)) return findTypeIdByTitle(types, 'videos');
        if (!opts.badyt && /twitch\.tv/i.test(file) && extractTwitchId(file)) return findTypeIdByTitle(types, 'twitch');
        if (file.indexOf('http://text.txt/?') === 0) return findTypeIdByTitle(types, 'text');
        if (file.indexOf('travel.html?') === 0) return findTypeIdByTitle(types, 'maps');
        if (!opts.badyt && /(\.jpg|\.jpeg|\.gif|\.png|\.tga)$/i.test(file)) return findTypeIdByTitle(types, 'images');
        if (!opts.badyt && /(\.mkv|\.avi|\.mpg|\.mp4|\.mpeg|\.vob)$/i.test(file)) return findTypeIdByTitle(types, 'movies');
        if (!opts.badyt && (/^https?:\/\//i.test(file) || /^www\./i.test(file))) return findTypeIdByTitle(types, 'websites');
        return '';
    }

    /* Resolve a type identifier returned by the tubeinfo server — which may
     * be either a stable id or a display title — to a concrete id in our
     * current types list. Returns '' if no match. */
    function resolveTypeValue(value, types) {
        if (!value || !types) return '';
        for (var i = 0; i < types.length; i++) {
            if (types[i].id === value) return types[i].id;
        }
        return findTypeIdByTitle(types, value);
    }

    /**
     * Create a window with title bar, content area, optional footer, and help text flyout.
     * @param {Object} options
     * @param {string} options.title - Window title
     * @param {boolean} [options.showBack] - Show back button
     * @param {boolean} [options.showClose] - Show close button
     * @param {Function} [options.onBack] - Back button callback
     * @param {Function} [options.onClose] - Close button callback
     * @param {Array} [options.footerButtons] - [{label, className, onClick}]
     * @returns {HTMLElement} The content container to populate
     */
    function createWindow(options) {
        options = options || {};

        // Wrapper holds window + helptext flyout
        var wrapper = document.createElement('div');
        wrapper.className = 'aa-window-wrapper';

        // Window
        var win = document.createElement('div');
        win.className = 'aa-window' + (options.windowClass ? ' ' + options.windowClass : '');

        // Title bar
        var titlebar = document.createElement('div');
        titlebar.className = 'aa-titlebar aa-draggable';

        var titleIcon = document.createElement('img');
        titleIcon.className = 'aa-title-icon';
        titleIcon.src = 'icons/aaicon.png';
        titlebar.appendChild(titleIcon);

        var title = document.createElement('div');
        title.className = 'aa-title';
        title.textContent = options.title || '';
        titlebar.appendChild(title);

        var buttons = document.createElement('div');
        buttons.className = 'aa-titlebar-buttons';

        if (options.showBack && options.onBack) {
            var backBtn = document.createElement('button');
            backBtn.className = 'aa-titlebar-btn';
            backBtn.innerHTML = '<img src="icons/backarrow.png" class="aa-titlebar-icon">';
            backBtn.addEventListener('click', function(e) { e.stopPropagation(); options.onBack(); });
            buttons.appendChild(backBtn);
        }

        if (options.showClose && options.onClose) {
            var closeBtn = document.createElement('button');
            closeBtn.className = 'aa-titlebar-btn aa-btn-close';
            closeBtn.innerHTML = '<img src="icons/close.png" class="aa-titlebar-icon">';
            closeBtn.addEventListener('click', function(e) { e.stopPropagation(); options.onClose(); });
            buttons.appendChild(closeBtn);
        }

        titlebar.appendChild(buttons);
        win.appendChild(titlebar);

        // Optional tabs (top position by default)
        var tabsRow = null;
        var tabsBottom = options.tabPosition === 'bottom';
        if (options.tabs && options.tabs.length > 0) {
            tabsRow = document.createElement('div');
            tabsRow.className = tabsBottom ? 'aa-tabs aa-tabs-bottom' : 'aa-tabs';
            if (!tabsBottom) win.appendChild(tabsRow);
        }

        // Content area
        var content = document.createElement('div');
        content.className = 'aa-content';
        win.appendChild(content);

        // Bottom tabs go after content
        if (tabsRow && tabsBottom) win.appendChild(tabsRow);

        // Set up tab buttons and switching
        if (tabsRow && options.tabs) {
            var activeTabIndex = -1;
            function activateTab(index) {
                if (index === activeTabIndex) return;
                activeTabIndex = index;
                // Update active class
                var tabBtns = tabsRow.querySelectorAll('.aa-tab');
                for (var j = 0; j < tabBtns.length; j++) {
                    tabBtns[j].classList.toggle('aa-tab-active', j === index);
                }
                // Clear content and call tab callback
                content.innerHTML = '';
                var tab = options.tabs[index];
                if (tab && tab.onActivate) tab.onActivate(content);
                // Persist active tab
                if (options.tabStorageKey) {
                    try { localStorage.setItem(options.tabStorageKey, String(index)); } catch(e) {}
                }
            }
            for (var t = 0; t < options.tabs.length; t++) {
                (function(idx) {
                    var tabBtn = document.createElement('button');
                    tabBtn.className = 'aa-tab';
                    tabBtn.textContent = options.tabs[idx].label || 'Tab ' + (idx + 1);
                    tabBtn.addEventListener('click', function() { activateTab(idx); });
                    tabsRow.appendChild(tabBtn);
                })(t);
            }
        }

        // Optional footer
        if (options.footerButtons && options.footerButtons.length > 0) {
            var footer = document.createElement('div');
            footer.className = 'aa-footer';
            for (var i = 0; i < options.footerButtons.length; i++) {
                var fb = options.footerButtons[i];
                var fbtn = document.createElement('button');
                fbtn.className = 'aa-btn ' + (fb.className || '');
                fbtn.textContent = fb.label;
                if (fb.onClick) fbtn.addEventListener('click', fb.onClick);
                footer.appendChild(fbtn);
            }
            win.appendChild(footer);
        }

        wrapper.appendChild(win);

        // Help text flyout
        var helptext = document.createElement('div');
        helptext.className = 'aa-helptext';
        wrapper.appendChild(helptext);

        // Store references
        uiState.windowEl = win;
        uiState.helptextEl = helptext;
        uiState.wrapperEl = wrapper;
        uiState.hasMoved = false;

        // Dev reload button
        var reloadBtn = document.createElement('a');
        reloadBtn.className = 'reloadButton';
        reloadBtn.textContent = '\u00B7';
        reloadBtn.href = '#';
        reloadBtn.addEventListener('click', function(e) { e.preventDefault(); location.reload(); });
        document.body.appendChild(reloadBtn);

        // Append to body
        document.body.appendChild(wrapper);

        // Set up dragging
        setupDrag(titlebar, wrapper);

        // Set up help text hover
        setupHelpText(helptext);

        // Activate override, saved, or first tab
        if (tabsRow && options.tabs && options.tabs.length > 0) {
            var startTab = 0;
            if (typeof options.tabOverride === 'number' && options.tabOverride >= 0 && options.tabOverride < options.tabs.length) {
                startTab = options.tabOverride;
            } else if (options.tabStorageKey) {
                try {
                    var saved = parseInt(localStorage.getItem(options.tabStorageKey));
                    if (!isNaN(saved) && saved >= 0 && saved < options.tabs.length) startTab = saved;
                } catch(e) {}
            }
            var allTabBtns = tabsRow.querySelectorAll('.aa-tab');
            if (allTabBtns[startTab]) allTabBtns[startTab].click();
        }

        // Call onReady callback if provided (for non-tabbed pages)
        if (options.onReady) options.onReady(content);

        return content;
    }

    function setupDrag(titlebar, wrapper) {
        titlebar.addEventListener('mousedown', function(e) {
            if (e.target.tagName === 'BUTTON') return;
            e.preventDefault();

            // On first drag, switch from flex-centered to absolute positioning
            if (!uiState.hasMoved) {
                var rect = wrapper.getBoundingClientRect();
                wrapper.style.position = 'absolute';
                wrapper.style.left = rect.left + 'px';
                wrapper.style.top = rect.top + 'px';
                // Remove flex centering from body
                document.body.style.justifyContent = 'flex-start';
                document.body.style.alignItems = 'flex-start';
                uiState.hasMoved = true;
            }

            uiState.isDragging = true;
            uiState.dragOffsetX = e.clientX - wrapper.offsetLeft;
            uiState.dragOffsetY = e.clientY - wrapper.offsetTop;
        });

        document.addEventListener('mousemove', function(e) {
            if (!uiState.isDragging) return;
            wrapper.style.left = (e.clientX - uiState.dragOffsetX) + 'px';
            wrapper.style.top = (e.clientY - uiState.dragOffsetY) + 'px';
        });

        document.addEventListener('mouseup', function() {
            uiState.isDragging = false;
        });
    }

    function setupHelpText(helptextEl) {
        document.addEventListener('mouseover', function(e) {
            var el = e.target;
            while (el && el !== document.body) {
                var text = el.getAttribute('helpText');
                if (text) {
                    helptextEl.textContent = text;
                    helptextEl.classList.add('aa-visible');
                    return;
                }
                el = el.parentElement;
            }
            helptextEl.classList.remove('aa-visible');
        });

        document.addEventListener('mouseout', function(e) {
            if (!e.relatedTarget || e.relatedTarget === document.documentElement) {
                helptextEl.classList.remove('aa-visible');
            }
        });
    }

    /* ========================================================================
     * Reusable UI Components
     * ======================================================================== */

    /**
     * Render a task list into a container element.
     * Shows active embedded instances with close buttons.
     * @param {HTMLElement} containerEl - Element to render into
     */
    function renderTaskList(containerEl) {
        if (!containerEl) return;

        /* Walk up the ancestry looking for a match — manual closest() for
         * paranoia against any quirks in the host webview. */
        function findAncestor(el, selector) {
            while (el && el !== containerEl) {
                if (el.classList && el.classList.contains(selector)) return el;
                el = el.parentNode;
            }
            return null;
        }

        function refresh() {
            if (!window.aapi || !aapi.manager || !aapi.manager.getActiveInstances) {
                containerEl.innerHTML = '<div class="aa-empty-message">API not available</div>';
                return;
            }

            var instances = aapi.manager.getActiveInstances();
            if (!instances || instances.length === 0) {
                containerEl.innerHTML = '<div class="aa-empty-message">No active instances</div>';
                return;
            }

            var rememberedId = (aapi.manager.getRememberedItemId ? aapi.manager.getRememberedItemId() : '') || '';

            var html = '';
            for (var i = 0; i < instances.length; i++) {
                var inst = instances[i];
                var isRemembered = (inst.itemId === rememberedId);
                var rowClass = 'aa-task-item' + (isRemembered ? ' aa-task-item-remembered' : '');
                html += '<div class="' + rowClass + '" data-item-id="' + escapeHtml(inst.itemId) + '">';
                html += '  <div class="aa-task-info">';
                html += '    <div class="aa-task-title">' + escapeHtml(inst.title || inst.itemId) + '</div>';
                html += '    <div class="aa-task-url">' + escapeHtml(inst.url || '') + '</div>';
                html += '  </div>';
                html += '  <button class="aa-task-close" data-item-id="' + escapeHtml(inst.itemId) + '">Close</button>';
                html += '</div>';
            }
            containerEl.innerHTML = html;
        }

        /* Single delegated click handler — survives every refresh() since the
         * container is long-lived while the task tab is visible, and innerHTML
         * replacement doesn't strip listeners on the container itself. */
        containerEl.addEventListener('click', function(e) {
            var closeEl = findAncestor(e.target, 'aa-task-close');
            if (closeEl) {
                var itemId = closeEl.getAttribute('data-item-id');
                if (itemId && window.aapi && aapi.manager && aapi.manager.deactivateInstance) {
                    aapi.manager.deactivateInstance(itemId);
                    refresh();
                }
                return;
            }
            var rowEl = findAncestor(e.target, 'aa-task-item');
            if (rowEl) {
                var rowItemId = rowEl.getAttribute('data-item-id');
                if (rowItemId && window.aapi && aapi.manager && aapi.manager.rememberItem) {
                    aapi.manager.rememberItem(rowItemId);
                    refresh();
                }
            }
        });

        refresh();
    }

    /* ========================================================================
     * Library Browser Component
     * ======================================================================== */

    var libraryTypes = [
        { key: 'items',     icon: 'icons/itemicon.png', help: 'Browse items' },
        { key: 'apps',      icon: 'icons/appicon.png', help: 'Browse apps' },
        { key: 'models',    icon: 'icons/3dmodelicon.png', help: 'Browse models' }
    ];

    var DISPLAY_MODES = ['list', 'square', 'landscape', 'large' /*, 'dynamic' */];

    function renderLibrary(containerEl) {
        if (!containerEl) return;

        var PAGE_SIZE = 30;
        var state = {
            type: 'items',
            offset: 0,
            searchTerm: '',
            isSearchMode: false,
            hasMore: true,
            loading: false,
            displayMode: 0, /* index into DISPLAY_MODES */
            itemType: '',   /* type filter for items (empty = all) */
            hasLoadedOnce: false
        };

        // Restore persisted options
        try {
            var saved = JSON.parse(localStorage.getItem('libraryOpts'));
            if (saved) {
                if (saved.type) state.type = saved.type;
                if (typeof saved.displayMode === 'number') state.displayMode = saved.displayMode;
                if (saved.searchTerm) state.searchTerm = saved.searchTerm;
                if (saved.itemType) state.itemType = saved.itemType;
            }
        } catch (e) {}
        /* Drop restored state if it points at a tab that's no longer exposed. */
        var visibleTypeKeys = {};
        for (var vti = 0; vti < libraryTypes.length; vti++) visibleTypeKeys[libraryTypes[vti].key] = true;
        if (!visibleTypeKeys[state.type]) state.type = 'items';

        function saveOpts() {
            try {
                localStorage.setItem('libraryOpts', JSON.stringify({
                    type: state.type,
                    displayMode: state.displayMode,
                    searchTerm: state.searchTerm,
                    itemType: state.itemType
                }));
            } catch (e) {}
        }

        // Build DOM
        var wrapper = document.createElement('div');
        wrapper.className = 'aa-library-wrapper';

        var scrollArea = document.createElement('div');
        scrollArea.className = 'aa-library-scroll';

        var grid = document.createElement('div');
        grid.className = 'aa-library-grid aa-library-mode-list';

        var loadMoreBtn = document.createElement('button');
        loadMoreBtn.className = 'aa-library-loadmore';
        loadMoreBtn.textContent = 'Load More';
        loadMoreBtn.style.display = 'none';
        loadMoreBtn.addEventListener('click', function() { loadMore(); });

        scrollArea.appendChild(grid);
        scrollArea.appendChild(loadMoreBtn);

        // Bottom bar: slider | search | type toggles
        var bar = document.createElement('div');
        bar.className = 'aa-library-bar';

        var slider = document.createElement('input');
        slider.type = 'range';
        slider.className = 'aa-library-slider';
        slider.min = '0';
        slider.max = String(DISPLAY_MODES.length - 1);
        slider.value = '0';
        slider.setAttribute('helpText', 'Display mode: List / Small Squares / Small Landscape / Medium Landscape / Dynamic');
        slider.addEventListener('change', function() {
            state.displayMode = parseInt(slider.value);
            grid.className = 'aa-library-grid aa-library-mode-' + DISPLAY_MODES[state.displayMode];
            saveOpts();
        });
        bar.appendChild(slider);

        var searchInput = document.createElement('input');
        searchInput.className = 'aa-library-search';
        searchInput.type = 'text';
        searchInput.placeholder = 'Search...';
        bar.appendChild(searchInput);

        var addNewBtn = document.createElement('button');
        addNewBtn.className = 'aa-library-type-btn';
        addNewBtn.innerHTML = '<img src="icons/plusicon.png" class="aa-library-bar-icon">';
        addNewBtn.title = 'Add New Item';
        addNewBtn.addEventListener('click', function() {
            window.location.href = 'file:///ui/createItem.html';
        });
        bar.appendChild(addNewBtn);

        var typesDiv = document.createElement('div');
        typesDiv.className = 'aa-library-types';

        for (var i = 0; i < libraryTypes.length; i++) {
            (function(lt, idx) {
                var btn = document.createElement('button');
                btn.className = 'aa-library-type-btn' + (idx === 0 ? ' aa-active' : '');
                btn.innerHTML = '<img src="' + lt.icon + '" class="aa-library-type-icon">';
                btn.setAttribute('helpText', lt.help);
                btn.setAttribute('data-type', lt.key);
                btn.addEventListener('click', function() {
                    switchType(lt.key);
                    var btns = typesDiv.querySelectorAll('.aa-library-type-btn');
                    for (var j = 0; j < btns.length; j++) btns[j].classList.remove('aa-active');
                    btn.classList.add('aa-active');
                });
                typesDiv.appendChild(btn);
            })(libraryTypes[i], i);
        }

        bar.appendChild(typesDiv);

        /* Favorites header bar */
        var favBar = document.createElement('div');
        favBar.className = 'aa-library-favbar';

        /* Reset button */
        var resetBtn = document.createElement('button');
        resetBtn.className = 'aa-library-reset-btn';
        resetBtn.innerHTML = '<img src="icons/refreshicon.png" class="aa-library-bar-icon">';
        resetBtn.title = 'Reset to favorites';
        resetBtn.addEventListener('click', function() {
            searchInput.value = '';
            state.searchTerm = '';
            state.isSearchMode = false;
            state.itemType = '';
            itemTypeSelect.value = '';
            /* Switch to items mode */
            state.type = 'items';
            var btns = typesDiv.querySelectorAll('.aa-library-type-btn');
            for (var j = 0; j < btns.length; j++) btns[j].classList.toggle('aa-active', btns[j].getAttribute('data-type') === 'items');
            saveOpts();
            removeSearchOption();
            favSelect.value = activeFavoritesListId;
            showFavorites();
        });
        favBar.appendChild(resetBtn);

        /* Favorites list drop-down */
        var favSelect = document.createElement('select');
        favSelect.className = 'aa-library-fav-select';
        function populateFavSelect() {
            favSelect.innerHTML = '';
            for (var fid in favoritesLists) {
                var opt = document.createElement('option');
                opt.value = fid;
                opt.textContent = favoritesLists[fid].title;
                favSelect.appendChild(opt);
            }
            favSelect.value = activeFavoritesListId;
        }
        function addSearchOption() {
            if (!favSelect.querySelector('.aa-fav-search-opt')) {
                var searchOpt = document.createElement('option');
                searchOpt.value = '__search__';
                searchOpt.textContent = 'Search results...';
                searchOpt.className = 'aa-fav-search-opt';
                favSelect.insertBefore(searchOpt, favSelect.firstChild);
            }
            favSelect.value = '__search__';
        }
        function removeSearchOption() {
            var opt = favSelect.querySelector('.aa-fav-search-opt');
            if (opt) opt.remove();
        }
        populateFavSelect();
        favSelect.addEventListener('change', function() {
            if (favSelect.value === '__search__') return;
            setActiveFavoritesList(favSelect.value);
            state.isSearchMode = false;
            state.searchTerm = '';
            searchInput.value = '';
            removeSearchOption();
            saveOpts();
            showFavorites();
        });
        favBar.appendChild(favSelect);

        /* Edit favorites button — opens the edit-favorites page for the active list */
        var editFavBtn = document.createElement('button');
        editFavBtn.className = 'aa-library-icon-btn';
        editFavBtn.innerHTML = '<img src="icons/editicon.png" class="aa-library-bar-icon">';
        editFavBtn.title = 'Edit favorites list';
        editFavBtn.addEventListener('click', function() {
            var id = activeFavoritesListId || 'favorites';
            window.location.href = 'file:///ui/editFavorites.html?id=' + encodeURIComponent(id);
        });
        favBar.appendChild(editFavBtn);

        /* Create favorites list button — opens the create page on the Favorites tab */
        var createFavBtn = document.createElement('button');
        createFavBtn.className = 'aa-library-icon-btn';
        createFavBtn.innerHTML = '<img src="icons/plusicon.png" class="aa-library-bar-icon">';
        createFavBtn.title = 'Create new favorites list';
        createFavBtn.addEventListener('click', function() {
            window.location.href = 'file:///ui/createItem.html?tab=favorites';
        });
        favBar.appendChild(createFavBtn);

        /* Item Types drop-down */
        var itemTypeSelect = document.createElement('select');
        itemTypeSelect.className = 'aa-library-itemtype-select';
        var allTypesOpt = document.createElement('option');
        allTypesOpt.value = '';
        allTypesOpt.textContent = 'All Types';
        itemTypeSelect.appendChild(allTypesOpt);
        try {
            var api = (window.aapi && aapi.library) ? aapi.library : null;
            if (api && api.getTypes) {
                var types = dedupTypesByLabel(api.getTypes());
                for (var ti = 0; ti < types.length; ti++) {
                    var topt = document.createElement('option');
                    topt.value = types[ti].id;
                    topt.textContent = types[ti].title || types[ti].id;
                    itemTypeSelect.appendChild(topt);
                }
            }
        } catch (e) {}
        itemTypeSelect.addEventListener('change', function() {
            state.itemType = itemTypeSelect.value;
            saveOpts();
            if (state.searchTerm) {
                doSearch(state.searchTerm);
            } else if (state.hasLoadedOnce) {
                loadEntries(true);
            }
        });
        favBar.appendChild(itemTypeSelect);

        /* Enhance selects with custom dropdown */
        enhanceSelect(favSelect);
        enhanceSelect(itemTypeSelect);

        wrapper.appendChild(favBar);
        wrapper.appendChild(scrollArea);
        wrapper.appendChild(bar);
        containerEl.appendChild(wrapper);

        // Apply restored state to UI
        grid.className = 'aa-library-grid aa-library-mode-' + DISPLAY_MODES[state.displayMode];
        slider.value = String(state.displayMode);
        // Highlight the correct type button
        var typeBtns = typesDiv.querySelectorAll('.aa-library-type-btn');
        for (var tb = 0; tb < typeBtns.length; tb++) {
            typeBtns[tb].classList.toggle('aa-active', typeBtns[tb].getAttribute('data-type') === state.type);
        }

        // Restore search term to input and trigger search, or show initial message
        console.log('[renderLibrary] restored state: type=' + state.type + ' searchTerm="' + state.searchTerm + '" displayMode=' + state.displayMode);
        /* Restore itemType select */
        itemTypeSelect.value = state.itemType || '';

        if (state.searchTerm) {
            searchInput.value = state.searchTerm;
            state.isSearchMode = true;
            state.hasLoadedOnce = true;
            doSearch(state.searchTerm);
            searchInput.focus();
            searchInput.select();
        } else {
            /* Default: show favorites, switch to items mode */
            state.type = 'items';
            var btns2 = typesDiv.querySelectorAll('.aa-library-type-btn');
            for (var tb2 = 0; tb2 < btns2.length; tb2++) btns2[tb2].classList.toggle('aa-active', btns2[tb2].getAttribute('data-type') === 'items');
            showFavorites();
            searchInput.focus();
        }

        // Search debounce
        var searchTimeout = null;
        searchInput.addEventListener('input', function() {
            clearTimeout(searchTimeout);
            var rawVal = searchInput.value;
            console.log('[renderLibrary] input event fired, value="' + rawVal + '"');
            searchTimeout = setTimeout(function() {
                var term = searchInput.value.trim();
                console.log('[renderLibrary] debounce fired, term="' + term + '" hasLoadedOnce=' + state.hasLoadedOnce);
                if (term) {
                    state.isSearchMode = true;
                    state.searchTerm = term;
                    saveOpts();
                    doSearch(term);
                } else {
                    state.isSearchMode = false;
                    state.searchTerm = '';
                    saveOpts();
                    removeSearchOption();
                    favSelect.value = activeFavoritesListId;
                    /* Clearing search keeps you on the current tab. Favorites is
                     * the home view only for the items tab; other tabs reload
                     * their full list instead. */
                    if (state.type === 'items') {
                        showFavorites();
                    } else {
                        loadEntries(true);
                    }
                }
            }, 300);
        });

        // API helpers
        function getApi() {
            return (window.aapi && aapi.library) ? aapi.library : null;
        }

        function loadEntries(reset) {
            console.log('[renderLibrary] loadEntries called, reset=' + reset + ' type=' + state.type);
            console.trace();
            var api = getApi();
            if (!api || state.loading) return;

            if (reset) { state.offset = 0; grid.innerHTML = ''; state.hasMore = true; }

            state.loading = true;
            state.hasLoadedOnce = true;
            var entries = [];

            try {
                var t = state.type;
                var tf = (t === 'items') ? state.itemType : '';
                if (t === 'items') entries = api.getItems(state.offset, PAGE_SIZE, tf);
                else if (t === 'apps') entries = api.getApps(state.offset, PAGE_SIZE);
                else if (t === 'maps') entries = api.getMaps(state.offset, PAGE_SIZE);
                else if (t === 'models') entries = api.getModels(state.offset, PAGE_SIZE);
                else if (t === 'instances') entries = api.getInstances(state.offset, PAGE_SIZE);
            } catch (e) { entries = []; }

            if (!entries || entries.length < PAGE_SIZE) state.hasMore = false;
            state.offset += (entries ? entries.length : 0);
            renderCards(entries || [], !reset);
            loadMoreBtn.style.display = state.hasMore ? 'block' : 'none';
            state.loading = false;
        }

        function doSearch(term) {
            console.log('[renderLibrary] doSearch called, term="' + term + '" type=' + state.type);
            var api = getApi();
            if (!api || state.loading) {
                console.log('[renderLibrary] doSearch bailed: api=' + !!api + ' loading=' + state.loading);
                return;
            }

            state.loading = true;
            state.hasLoadedOnce = true;
            grid.innerHTML = '';
            var entries = [];

            try {
                var t = state.type;
                var tf = (t === 'items') ? state.itemType : '';
                if (t === 'items') entries = api.searchItems(term, PAGE_SIZE, tf);
                else if (t === 'apps') entries = api.searchApps(term, PAGE_SIZE);
                else if (t === 'maps') entries = api.searchMaps(term, PAGE_SIZE);
                else if (t === 'models') entries = api.searchModels(term, PAGE_SIZE);
                else if (t === 'instances') entries = api.searchInstances(term, PAGE_SIZE);
            } catch (e) { entries = []; }

            renderCards(entries || [], false);
            loadMoreBtn.style.display = 'none';
            state.loading = false;
            addSearchOption();
        }

        function showFavorites() {
            var list = getActiveFavoritesList();
            if (!list || !list.entries || list.entries.length === 0) {
                grid.innerHTML = '<div class="aa-empty-message" style="grid-column:1/-1">No favorites yet. Search for items and click the star to add them.</div>';
                loadMoreBtn.style.display = 'none';
                return;
            }
            var api = getApi();
            if (!api) return;
            var entries = [];
            for (var i = 0; i < list.entries.length; i++) {
                var e = list.entries[i];
                try {
                    if (e.item && api.getItemById) {
                        var item = api.getItemById(e.item);
                        if (item) entries.push(item);
                    } else if (e.model && api.getModelById) {
                        var model = api.getModelById(e.model);
                        if (model) entries.push(model);
                    }
                } catch (ex) {}
            }
            renderCards(entries, false, true);
            loadMoreBtn.style.display = 'none';
            /* Update favSelect to show this list */
            var searchOpt = favSelect.querySelector('.aa-fav-search-opt');
            if (searchOpt) searchOpt.style.display = 'none';
            favSelect.value = activeFavoritesListId;
        }

        function loadMore() {
            if (state.isSearchMode || !state.hasMore) return;
            loadEntries(false);
        }

        function switchType(type) {
            state.type = type;
            if (type !== 'items') {
                state.itemType = '';
                itemTypeSelect.value = '';
            }
            saveOpts();
            addSearchOption();
            state.hasLoadedOnce = true;
            if (state.searchTerm) {
                doSearch(state.searchTerm);
            } else {
                loadEntries(true);
            }
        }

        function renderCards(entries, append, mixedTypes) {
            if (!append) grid.innerHTML = '';
            var rendered = 0;
            for (var i = 0; i < entries.length; i++) {
                /* In models view, hide cabinet models (they're accessed via items) */
                if (state.type === 'models' && entries[i].id) {
                    try {
                        if (window.aapi && aapi.manager && aapi.manager.isModelCabinet &&
                            aapi.manager.isModelCabinet(entries[i].id)) continue;
                    } catch (e) {}
                }
                /* When entries can be a mix of items/models (favorites, cross-type results),
                 * let createCard fall back to per-entry shape detection instead of
                 * pinning everything to the current tab. */
                var cardType = mixedTypes ? null : state.type;
                grid.appendChild(createCard(entries[i], cardType));
                rendered++;
            }
            if (!append && rendered === 0) {
                grid.innerHTML = '<div class="aa-empty-message" style="grid-column:1/-1">No results found.</div>';
            }
        }

        function createCard(entry, entryType) {
            var card = document.createElement('div');
            card.className = 'aa-library-card';

            var imgDiv = document.createElement('div');
            imgDiv.className = 'aa-library-card-img';

            var imgUrl = getBestImage(entry);
            /* Items fall back to snapshots, models fall back to thumbnails */
            var fallbackPath = (entry.type !== undefined)
                ? getSnapshotPath(entry.id)
                : getModelThumbnailPath(entry.id);
            loadCardImage(imgDiv, imgUrl, fallbackPath);

            var titleDiv = document.createElement('div');
            titleDiv.className = 'aa-library-card-title';
            titleDiv.textContent = entry.title || entry.id || 'Untitled';

            card.appendChild(imgDiv);
            card.appendChild(titleDiv);

            /* Use the caller-supplied current tab when we have one; otherwise fall
             * back to a shape-based guess (items have a 'type' field, models don't).
             * The fallback matters for favorites/search rendering where entries of
             * multiple types can be interleaved. */
            var entryMode = entryType || ((entry.type !== undefined) ? 'items' : 'models');

            /* Action buttons (hover-only): edit + favorite */
            var actions = document.createElement('div');
            actions.className = 'aa-library-card-actions';

            /* Edit button — all types (use entryMode for correct page in favorites) */
            var editBtn = document.createElement('button');
            editBtn.className = 'aa-library-edit-btn';
            editBtn.innerHTML = '<img src="icons/editicon.png" class="aa-edit-card-icon">';
            (function(t, eId) {
                var editPages = { items: 'editItem', models: 'editModel', apps: 'editApp', maps: 'editMap', instances: 'editInstance' };
                var page = editPages[t] || 'editItem';
                editBtn.addEventListener('click', function(ev) {
                    ev.stopPropagation();
                    window.location.href = 'file:///ui/' + page + '.html?id=' + encodeURIComponent(eId);
                });
            })(entryMode, entry.id);
            actions.appendChild(editBtn);

            /* Favorite star — items and models only */
            if (entryMode === 'items' || entryMode === 'models') {
                var favBtn = document.createElement('button');
                favBtn.className = 'aa-library-fav-btn';
                var entryIsFav = isFavorite(entryMode, entry.id);
                favBtn.innerHTML = entryIsFav ? '<img src="icons/favoriteicon.png" class="aa-fav-icon">' : '<img src="icons/favoriteiconhollow.png" class="aa-fav-icon">';
                if (entryIsFav) card.classList.add('aa-fav-tile');
                (function(eMode, eId, btn, crd) {
                    btn.addEventListener('click', function(ev) {
                        ev.stopPropagation();
                        if (isFavorite(eMode, eId)) {
                            removeFromFavorites(eMode, eId);
                            btn.innerHTML = '<img src="icons/favoriteiconhollow.png" class="aa-fav-icon">';
                            crd.classList.remove('aa-fav-tile');
                        } else {
                            addToFavorites(eMode, eId);
                            btn.innerHTML = '<img src="icons/favoriteicon.png" class="aa-fav-icon">';
                            crd.classList.add('aa-fav-tile');
                        }
                    });
                })(entryMode, entry.id, favBtn, card);
                actions.appendChild(favBtn);
            }

            card.appendChild(actions);

            // Click to spawn (items or models — use entryMode to handle favorites mixed lists)
            if (entryMode === 'items' && entry.id) {
                card.addEventListener('click', function() {
                    if (window.aapi && aapi.manager && aapi.manager.spawnItemObject) {
                        aapi.manager.spawnItemObject(entry.id);
                        aapi.manager.closeMenu();
                    }
                });
            }
            if (entryMode === 'models' && entry.id) {
                card.addEventListener('click', function() {
                    if (window.aapi && aapi.manager && aapi.manager.spawnModelObject) {
                        aapi.manager.spawnModelObject(entry.id);
                        aapi.manager.closeMenu();
                    }
                });
            }

            /* Instance count badge — appended to card (not imgDiv which gets cleared by async image load) */
            if (entry.count && entry.count > 0) {
                var badge = document.createElement('div');
                badge.className = 'aa-library-count-badge';
                badge.textContent = entry.count;
                card.appendChild(badge);
            }

            return card;
        }

        function getSnapshotPath(itemId) {
            return window.imageCache.predictSnapshotPath(itemId);
        }

        function getModelThumbnailPath(modelId) {
            return window.imageCache.predictThumbnailPath(modelId);
        }

        function getBestImage(entry) {
            return entry.marquee || entry.screen || entry.preview || entry.file || '';
        }

        function isImgUrl(url) {
            if (!url || typeof url !== 'string') return false;
            var lower = url.toLowerCase();
            /* Check for image file extensions anywhere in the URL */
            var exts = ['.jpg', '.jpeg', '.png', '.gif', '.webp', '.bmp', '.svg'];
            for (var i = 0; i < exts.length; i++) {
                if (lower.indexOf(exts[i]) !== -1) return true;
            }
            /* Check for image-related URL patterns */
            var patterns = ['/image', '/screenshot', '/preview', '/thumb', '/poster', '/cover', '/header', '/capsule', '/page_bg'];
            for (var p = 0; p < patterns.length; p++) {
                if (lower.indexOf(patterns[p]) !== -1) return true;
            }
            return false;
        }

        function loadCardImage(imgDiv, url, snapshotPath) {
            if (!isImgUrl(url)) {
                /* No valid image URL — try snapshot fallback */
                if (snapshotPath) {
                    var snapImg = document.createElement('img');
                    snapImg.src = snapshotPath;
                    snapImg.onerror = function() { imgDiv.textContent = '\uD83D\uDDBC'; };
                    imgDiv.textContent = '';
                    imgDiv.appendChild(snapImg);
                } else {
                    imgDiv.textContent = '\uD83D\uDDBC';
                }
                return;
            }
            if (typeof arcadeHud.loadImage === 'function') {
                arcadeHud.loadImage(url).then(function(result) {
                    if (result && result.filePath) {
                        var img = document.createElement('img');
                        img.src = result.filePath;
                        img.onerror = function() {
                            /* Image file failed — try snapshot */
                            if (snapshotPath) {
                                var fallback = document.createElement('img');
                                fallback.src = snapshotPath;
                                fallback.onerror = function() { imgDiv.textContent = '\uD83D\uDDBC'; };
                                imgDiv.textContent = '';
                                imgDiv.appendChild(fallback);
                            } else {
                                imgDiv.textContent = '\uD83D\uDDBC';
                            }
                        };
                        imgDiv.textContent = '';
                        imgDiv.appendChild(img);
                    } else if (snapshotPath) {
                        var fallback = document.createElement('img');
                        fallback.src = snapshotPath;
                        fallback.onerror = function() { imgDiv.textContent = '\uD83D\uDDBC'; };
                        imgDiv.textContent = '';
                        imgDiv.appendChild(fallback);
                    }
                }).catch(function() {
                    if (snapshotPath) {
                        var fallback = document.createElement('img');
                        fallback.src = snapshotPath;
                        fallback.onerror = function() { imgDiv.textContent = '\uD83D\uDDBC'; };
                        imgDiv.textContent = '';
                        imgDiv.appendChild(fallback);
                    } else {
                        imgDiv.textContent = '\uD83D\uDDBC';
                    }
                });
            } else {
                imgDiv.textContent = '\uD83D\uDDBC';
            }
        }

    }

    /* ========================================================================
     * Enhanced Select Dropdown
     * ======================================================================== */
    function enhanceSelect(selectEl) {
        var overlay = null;
        var cooldown = false;

        function close() {
            if (overlay && overlay.parentNode) overlay.parentNode.removeChild(overlay);
            overlay = null;
        }

        function open() {
            close();
            var rect = selectEl.getBoundingClientRect();
            overlay = document.createElement('div');
            overlay.className = 'aa-dropdown-overlay';
            overlay.style.position = 'absolute';
            overlay.style.left = rect.left + 'px';
            overlay.style.top = (rect.bottom + 2) + 'px';
            overlay.style.width = rect.width + 'px';

            var list = document.createElement('div');
            list.className = 'aa-dropdown-list';
            overlay.appendChild(list);

            var search = document.createElement('input');
            search.className = 'aa-dropdown-search';
            search.type = 'text';
            search.placeholder = 'Search...';
            overlay.appendChild(search);

            function buildItems(filter) {
                list.innerHTML = '';
                var opts = selectEl.options;
                var filterLower = (filter || '').toLowerCase();
                for (var i = 0; i < opts.length; i++) {
                    var opt = opts[i];
                    if (opt.style && opt.style.display === 'none') continue;
                    var label = opt.textContent || opt.value;
                    if (filterLower && label.toLowerCase().indexOf(filterLower) !== 0) continue;
                    (function(val, lbl) {
                        var item = document.createElement('div');
                        item.className = 'aa-dropdown-item';
                        if (val === selectEl.value) item.classList.add('aa-selected');
                        item.textContent = lbl;
                        item.addEventListener('mousedown', function(e) {
                            e.preventDefault();
                            e.stopPropagation();
                            selectEl.value = val;
                            selectEl.dispatchEvent(new Event('change'));
                            close();
                            cooldown = true;
                            setTimeout(function() { cooldown = false; }, 100);
                        });
                        list.appendChild(item);
                    })(opt.value, label);
                }
            }

            buildItems('');

            search.addEventListener('input', function() {
                buildItems(search.value);
            });
            search.addEventListener('keydown', function(e) {
                if (e.key === 'Escape') { close(); e.stopPropagation(); }
            });

            document.body.appendChild(overlay);
            search.focus();

            /* Close on click outside (but not on the select itself — that's handled by toggle) */
            setTimeout(function() {
                document.addEventListener('mousedown', function handler(e) {
                    if (!overlay) { document.removeEventListener('mousedown', handler); return; }
                    if (!overlay.contains(e.target) && e.target !== selectEl) {
                        close();
                        document.removeEventListener('mousedown', handler);
                    }
                });
            }, 0);
        }

        selectEl.addEventListener('mousedown', function(e) {
            e.preventDefault();
            if (cooldown) return;
            if (overlay) close(); else open();
        });

        return { refresh: function() { if (overlay) { close(); open(); } } };
    }

    /* ========================================================================
     * Overlay Cursor
     * ======================================================================== */
    function initOverlayCursor() {
        var cursor = document.createElement('div');
        cursor.className = 'aa-overlay-cursor';
        cursor.textContent = 'Cursor';
        document.body.appendChild(cursor);
        document.addEventListener('mousemove', function(e) {
            cursor.style.left = e.clientX + 'px';
            cursor.style.top = e.clientY + 'px';
        });
    }

    // Public API
    return {
        initialize: initialize,
        shutdown: shutdown,
        isReady: isReady,
        hasFeature: hasFeature,

        // Image loading
        loadImage: loadImage,
        predictCachePath: predictCachePath,

        // Database
        getSupportedEntryTypes: getSupportedEntryTypes,

        // Favorites
        favorites: {
            addToFavorites: addToFavorites,
            removeFromFavorites: removeFromFavorites,
            isFavorite: isFavorite,
            getActiveFavoritesList: getActiveFavoritesList,
            setActiveFavoritesList: setActiveFavoritesList,
            createFavoritesList: createFavoritesList,
            updateFavoritesList: updateFavoritesList,
            deleteFavoritesList: deleteFavoritesList,
            getLists: function() { return favoritesLists; }
        },

        // UI framework
        ui: {
            createWindow: createWindow,
            escapeHtml: escapeHtml,
            renderTaskList: renderTaskList,
            renderLibrary: renderLibrary,
            enhanceSelect: enhanceSelect
        },

        // URL / type detection helpers
        extractYouTubeId: extractYouTubeId,
        extractTwitchId: extractTwitchId,
        encodeRFC5987ValueChars: encodeRFC5987ValueChars,
        findTypeIdByTitle: findTypeIdByTitle,
        detectItemType: detectItemType,
        resolveTypeValue: resolveTypeValue,
        dedupTypesByLabel: dedupTypesByLabel,

        // Overlay cursor
        initOverlayCursor: initOverlayCursor,

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
