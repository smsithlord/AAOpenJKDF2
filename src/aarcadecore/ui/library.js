class LibraryBrowser {
    constructor() {
        this.currentEntries = [];
        this.filteredEntries = [];
        this.currentQuery = null;
        this.hasMoreEntries = true;
        this.isLoading = false;
        this.currentOffset = 0;  // Track pagination offset for typed API

        // Search state
        this.isSearchMode = false;
        this.currentSearchTerm = '';
        this.searchDebounceTimer = null;

        // Image cache state
        // Map of url -> {status: 'pending'|'loading'|'cached'|'error', filePath: string, elements: Set<HTMLImageElement>}
        this.imageCache = new Map();
        this.placeholderImage = 'media-loading.jpg';

        this.initializeElements();
        this.bindEvents();
        this.updateStatus('Ready to load entries');
    }

    initializeElements() {
        this.elements = {
            entryType: document.getElementById('entryType'),
            pageSize: document.getElementById('pageSize'),
            loadBtn: document.getElementById('loadBtn'),
            loadMoreBtn: document.getElementById('loadMoreBtn'),
            searchBox: document.getElementById('searchBox'),
            statusText: document.getElementById('statusText'),
            entryCount: document.getElementById('entryCount'),
            libraryGrid: document.getElementById('libraryGrid'),
            loadingIndicator: document.getElementById('loadingIndicator'),
            errorMessage: document.getElementById('errorMessage'),
            modalOverlay: document.getElementById('modalOverlay'),
            modalTitle: document.getElementById('modalTitle'),
            modalBody: document.getElementById('modalBody'),
            modalClose: document.getElementById('modalClose')
        };
    }

    bindEvents() {
        this.elements.loadBtn.addEventListener('click', () => this.loadEntries(true));
        this.elements.loadMoreBtn.addEventListener('click', () => this.loadMoreEntries());
        this.elements.searchBox.addEventListener('input', (e) => this.handleSearchInput(e.target.value));
        this.elements.modalClose.addEventListener('click', () => this.closeModal());
        this.elements.modalOverlay.addEventListener('click', (e) => {
            if (e.target === this.elements.modalOverlay) {
                this.closeModal();
            }
        });
        
        // Clear search when entry type changes
        this.elements.entryType.addEventListener('change', () => this.clearSearch());
        this.elements.pageSize.addEventListener('change', () => this.clearSearch());
        
        // Keyboard shortcuts
        document.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') {
                this.closeModal();
            }
        });
    }

    async loadEntries(isNewQuery = true) {
        if (this.isLoading) return;
        
        this.isLoading = true;
        this.showLoading();
        this.hideError();
        
        try {
            const entryType = this.elements.entryType.value;
            const pageSize = parseInt(this.elements.pageSize.value);
            
            let newEntries;
            
            if (isNewQuery) {
                // Reset state for new query
                this.currentEntries = [];
                this.currentQuery = entryType;
                this.currentOffset = 0;  // Reset offset for typed API
                this.hasMoreEntries = true;
                this.isSearchMode = false;
                this.elements.libraryGrid.innerHTML = '';

                // Get first batch - use typed API for items, types, platforms, and models; legacy for others
                if (entryType === 'items') {
                    newEntries = aapi.library.getItems(this.currentOffset, pageSize);
                    this.currentOffset += pageSize;  // Advance offset for next load
                } else if (entryType === 'types') {
                    // Types API returns all types at once (no pagination)
                    newEntries = aapi.library.getTypes();
                    this.hasMoreEntries = false;  // All types fetched, disable "Load More"
                } else if (entryType === 'platforms') {
                    // Platforms API returns all platforms at once (typically just 1 entry)
                    newEntries = aapi.library.getPlatforms();
                    this.hasMoreEntries = false;  // All platforms fetched, disable "Load More"
                } else if (entryType === 'models') {
                    // Models API supports pagination
                    newEntries = aapi.library.getModels(this.currentOffset, pageSize);
                    this.currentOffset += pageSize;  // Advance offset for next load
                } else if (entryType === 'apps') {
                    // Apps API supports pagination
                    newEntries = aapi.library.getApps(this.currentOffset, pageSize);
                    this.currentOffset += pageSize;  // Advance offset for next load
                } else if (entryType === 'maps') {
                    // Maps API supports pagination
                    newEntries = aapi.library.getMaps(this.currentOffset, pageSize);
                    this.currentOffset += pageSize;  // Advance offset for next load
                } else if (entryType === 'instances') {
                    // Instances API supports pagination
                    newEntries = aapi.library.getInstances(this.currentOffset, pageSize);
                    this.currentOffset += pageSize;  // Advance offset for next load
                } else {
                    // TODO: Other entry types still use legacy API until typed methods are added
                    newEntries = aapi.getFirstEntries(entryType, pageSize);
                }
                this.updateStatus(`Loading ${entryType}...`);
            } else {
                // This should not be called for new browsing - use loadMoreEntries instead
                return;
            }
            
            if (newEntries && newEntries.length > 0) {
                this.currentEntries.push(...newEntries);
                this.filteredEntries = [...this.currentEntries]; // No client-side filtering in browse mode
                this.renderEntries();
                
                this.updateStatus(`Loaded ${this.currentEntries.length} ${entryType} entries`);
                this.elements.loadMoreBtn.style.display = 'inline-block';
                this.elements.loadMoreBtn.textContent = 'Load More';
                this.elements.loadMoreBtn.disabled = false;
                
                // Check if we got less than requested (might be end of data)
                if (newEntries.length < pageSize) {
                    this.hasMoreEntries = false;
                    this.elements.loadMoreBtn.textContent = 'No more entries';
                    this.elements.loadMoreBtn.disabled = true;
                }
            } else {
                this.hasMoreEntries = false;
                this.elements.loadMoreBtn.style.display = 'none';
                this.updateStatus(`No ${entryType} entries found`);
            }
            
        } catch (error) {
            console.error('Error loading entries:', error);
            this.showError(`Failed to load ${this.elements.entryType.value} entries: ${error.message}`);
        } finally {
            this.isLoading = false;
            this.hideLoading();
            this.updateEntryCount();
        }
    }

    async loadMoreEntries() {
        if (this.isLoading) return;
        
        this.isLoading = true;
        this.showLoading();
        
        try {
            const pageSize = parseInt(this.elements.pageSize.value);
            let newEntries;
            
            if (this.isSearchMode) {
                // Load more search results
                if (this.currentQuery === 'items' || this.currentQuery === 'models' || this.currentQuery === 'types' || this.currentQuery === 'platforms') {
                    // NOTE: Typed API search doesn't support pagination (no offset parameter yet)
                    // All results fetched in initial search
                    newEntries = [];
                    this.hasMoreEntries = false;
                } else {
                    // TODO: Other entry types still use legacy API
                    newEntries = aapi.getNextSearchResults(pageSize);
                }
                this.updateStatus(`Loading more search results for "${this.currentSearchTerm}"...`);
            } else {
                // Load more browse results
                if (this.currentQuery === 'items' || this.currentQuery === 'models') {
                    // Items and Models support pagination
                    const methodName = this.currentQuery === 'items' ? 'getItemsTyped' : 'getModelsTyped';
                    newEntries = aapi[methodName](this.currentOffset, pageSize);
                    this.currentOffset += pageSize;  // Advance offset for next load
                } else if (this.currentQuery === 'types' || this.currentQuery === 'platforms') {
                    // Types/Platforms don't support pagination (all fetched at once)
                    newEntries = [];
                    this.hasMoreEntries = false;
                } else {
                    // TODO: Other entry types still use legacy API
                    newEntries = aapi.getNextEntries(pageSize);
                }
                this.updateStatus(`Loading more ${this.currentQuery} entries...`);
            }
            
            if (newEntries && newEntries.length > 0) {
                this.currentEntries.push(...newEntries);
                this.filteredEntries = [...this.currentEntries];
                this.renderEntries();
                
                if (this.isSearchMode) {
                    this.updateStatus(`Found ${this.currentEntries.length} results for "${this.currentSearchTerm}"`);
                } else {
                    this.updateStatus(`Loaded ${this.currentEntries.length} ${this.currentQuery} entries`);
                }
                
                // Check if we got less than requested (might be end of data)
                if (newEntries.length < pageSize) {
                    this.hasMoreEntries = false;
                    this.elements.loadMoreBtn.textContent = 'No more entries';
                    this.elements.loadMoreBtn.disabled = true;
                }
            } else {
                this.hasMoreEntries = false;
                this.elements.loadMoreBtn.textContent = 'No more entries';
                this.elements.loadMoreBtn.disabled = true;
                
                if (this.isSearchMode) {
                    this.updateStatus(`All search results loaded for "${this.currentSearchTerm}" (${this.currentEntries.length} total)`);
                } else {
                    this.updateStatus(`All ${this.currentQuery} entries loaded (${this.currentEntries.length} total)`);
                }
            }
            
        } catch (error) {
            console.error('Error loading more entries:', error);
            this.showError('Failed to load more entries: ' + error.message);
        } finally {
            this.isLoading = false;
            this.hideLoading();
            this.updateEntryCount();
        }
    }

    handleSearchInput(searchTerm) {
        // Clear any existing debounce timer
        if (this.searchDebounceTimer) {
            clearTimeout(this.searchDebounceTimer);
        }
        
        // If search is empty, switch back to browse mode
        if (!searchTerm.trim()) {
            this.clearSearch();
            return;
        }
        
        // Debounce the search to avoid excessive API calls
        this.searchDebounceTimer = setTimeout(() => {
            this.performSearch(searchTerm.trim());
        }, 300); // 300ms delay
    }

    async performSearch(searchTerm) {
        if (this.isLoading) return;
        
        this.isLoading = true;
        this.showLoading();
        this.hideError();
        
        try {
            const entryType = this.elements.entryType.value;
            const pageSize = parseInt(this.elements.pageSize.value);
            
            // Start new search
            this.currentEntries = [];
            this.isSearchMode = true;
            this.currentSearchTerm = searchTerm;
            this.currentQuery = entryType;
            this.currentOffset = 0;  // Reset offset (though search doesn't use pagination yet)
            this.hasMoreEntries = true;
            this.elements.libraryGrid.innerHTML = '';

            this.updateStatus(`Searching ${entryType} for "${searchTerm}"...`);

            // Get first search results - use typed API for items and models, legacy for others
            let searchResults;
            if (entryType === 'items') {
                searchResults = aapi.library.searchItems(searchTerm, pageSize);
                // NOTE: searchItemsTyped doesn't support pagination yet
                // Disable "Load More" since all results are fetched at once
                this.hasMoreEntries = false;
            } else if (entryType === 'models') {
                searchResults = aapi.library.searchModels(searchTerm, pageSize);
                // NOTE: searchModelsTyped doesn't support pagination yet
                // Disable "Load More" since all results are fetched at once
                this.hasMoreEntries = false;
            } else if (entryType === 'apps') {
                searchResults = aapi.library.searchApps(searchTerm, pageSize);
                // NOTE: searchAppsTyped doesn't support pagination yet
                // Disable "Load More" since all results are fetched at once
                this.hasMoreEntries = false;
            } else if (entryType === 'maps') {
                searchResults = aapi.library.searchMaps(searchTerm, pageSize);
                // NOTE: searchMapsTyped doesn't support pagination yet
                // Disable "Load More" since all results are fetched at once
                this.hasMoreEntries = false;
            } else if (entryType === 'instances') {
                searchResults = aapi.library.searchInstances(searchTerm, pageSize);
                // NOTE: Instances search by ID substring (no title field)
                // Disable "Load More" since all results are fetched at once
                this.hasMoreEntries = false;
            } else if (entryType === 'types') {
                // Types: fetch all and do client-side filtering (very few types, no need for SQL search)
                const allTypes = aapi.library.getTypes();
                searchResults = allTypes.filter(type =>
                    (type.title && type.title.toLowerCase().includes(searchTerm.toLowerCase())) ||
                    (type.titleformat && type.titleformat.toLowerCase().includes(searchTerm.toLowerCase()))
                );
                this.hasMoreEntries = false;  // All results fetched at once
            } else if (entryType === 'platforms') {
                // Platforms: fetch all and do client-side filtering (typically just 1 platform)
                const allPlatforms = aapi.library.getPlatforms();
                searchResults = allPlatforms.filter(platform =>
                    (platform.title && platform.title.toLowerCase().includes(searchTerm.toLowerCase()))
                );
                this.hasMoreEntries = false;  // All results fetched at once
            } else {
                // TODO: Other entry types still use legacy API
                searchResults = aapi.getFirstSearchResults(entryType, searchTerm, pageSize);
            }

            if (searchResults && searchResults.length > 0) {
                this.currentEntries.push(...searchResults);
                this.filteredEntries = [...this.currentEntries];
                this.renderEntries();

                this.updateStatus(`Found ${this.currentEntries.length} results for "${searchTerm}"`);

                // Show/hide "Load More" based on whether pagination is supported
                if (entryType === 'items' || entryType === 'models' || entryType === 'apps' || entryType === 'maps' || entryType === 'instances' || entryType === 'types' || entryType === 'platforms') {
                    // Typed API search doesn't support pagination yet (all results at once)
                    this.elements.loadMoreBtn.style.display = 'none';
                } else {
                    this.elements.loadMoreBtn.style.display = 'inline-block';
                    this.elements.loadMoreBtn.textContent = 'Load More Results';
                    this.elements.loadMoreBtn.disabled = false;
                }
                
                // Check if we got less than requested (might be end of search results)
                if (searchResults.length < pageSize) {
                    this.hasMoreEntries = false;
                    this.elements.loadMoreBtn.textContent = 'No more results';
                    this.elements.loadMoreBtn.disabled = true;
                }
            } else {
                this.hasMoreEntries = false;
                this.elements.loadMoreBtn.style.display = 'none';
                this.updateStatus(`No results found for "${searchTerm}"`);
            }
            
        } catch (error) {
            console.error('Error performing search:', error);
            this.showError(`Search failed: ${error.message}`);
        } finally {
            this.isLoading = false;
            this.hideLoading();
            this.updateEntryCount();
        }
    }

    clearSearch() {
        // Clear search input and reset to browse mode
        this.elements.searchBox.value = '';
        this.currentSearchTerm = '';
        this.isSearchMode = false;
        
        // Clear debounce timer
        if (this.searchDebounceTimer) {
            clearTimeout(this.searchDebounceTimer);
            this.searchDebounceTimer = null;
        }
        
        // Reset display
        this.currentEntries = [];
        this.filteredEntries = [];
        this.currentOffset = 0;  // Reset offset when clearing
        this.elements.libraryGrid.innerHTML = '';
        this.elements.loadMoreBtn.style.display = 'none';
        this.updateStatus('Ready to load entries');
        this.updateEntryCount();
    }

    renderEntries() {
        this.elements.libraryGrid.innerHTML = '';
        
        this.filteredEntries.forEach((entry, index) => {
            const card = this.createEntryCard(entry, index);
            this.elements.libraryGrid.appendChild(card);
        });
        
        // Update entry count after rendering
        this.updateEntryCount();
    }

    createEntryCard(entry) {
        const card = document.createElement('div');
        card.className = 'entry-card';
        card.addEventListener('click', () => this.showEntryDetail(entry));

        // Determine the best image to use
        const imageUrl = this.getBestImage(entry);
        const imageElement = imageUrl
            ? `<img src="${this.placeholderImage}" data-url="${imageUrl}" alt="${entry.title || 'Entry'}" class="card-image loading">`
            : `<div class="card-image placeholder">🖼️</div>`;

        // Create meta tags
        const metaTags = this.createMetaTags(entry);

        card.innerHTML = `
            ${imageElement}
            <div class="card-content">
                <h3 class="card-title">${this.escapeHtml(entry.title || 'Untitled')}</h3>
                ${entry.description ? `<p class="card-description">${this.escapeHtml(entry.description)}</p>` : ''}
                <div class="card-meta">
                    ${metaTags}
                </div>
            </div>
        `;

        // Trigger image loading if URL exists
        if (imageUrl) {
            const imgElement = card.querySelector('img[data-url]');
            if (imgElement) {
                this.loadImageWithCache(imageUrl, imgElement);
            }
        }

        return card;
    }

    getBestImage(entry) {
        // Priority order for images
        const imageFields = ['marquee', 'screen', 'file'];
        
        for (const field of imageFields) {
            if (entry[field] && this.isImageUrl(entry[field])) {
                return entry[field];
            }
        }
        
        return null;
    }

    isImageUrl(url) {
        if (!url || typeof url !== 'string') return false;
        
        // Check if URL has image extension
        const imageExtensions = ['.jpg', '.jpeg', '.png', '.gif', '.webp', '.bmp', '.svg'];
        const urlLower = url.toLowerCase();
        
        return imageExtensions.some(ext => urlLower.includes(ext)) ||
               urlLower.includes('image') ||
               urlLower.includes('screenshot') ||
               urlLower.includes('preview') ||
               urlLower.includes('thumb');
    }

    createMetaTags(entry) {
        const tags = [];

        // Add file type based on content
        if (entry.reference && entry.reference.includes('steam')) {
            tags.push(`<span class="meta-tag">🎮 Steam</span>`);
        } else if (entry.stream || entry.file?.includes('twitch')) {
            tags.push(`<span class="meta-tag">📺 Stream</span>`);
        } else if (entry.file?.includes('youtube') || entry.preview?.includes('youtube')) {
            tags.push(`<span class="meta-tag">📹 Video</span>`);
        } else if (this.isImageUrl(entry.file)) {
            tags.push(`<span class="meta-tag">🖼️ Image</span>`);
        }

        return tags.join('');
    }

    loadImageWithCache(url, imgElement) {
        // Check if already in cache
        if (this.imageCache.has(url)) {
            const cacheEntry = this.imageCache.get(url);

            // Add element to tracking set
            if (!cacheEntry.elements) {
                cacheEntry.elements = new Set();
            }
            cacheEntry.elements.add(imgElement);

            // Update based on current status
            if (cacheEntry.status === 'cached') {
                this.updateImageElement(imgElement, cacheEntry.filePath, 'loaded');
            } else if (cacheEntry.status === 'error') {
                this.updateImageElement(imgElement, null, 'error');
            }
            // If 'loading', just wait for completion

            return;
        }

        // Create new cache entry
        const cacheEntry = {
            status: 'loading',
            filePath: null,
            elements: new Set([imgElement])
        };
        this.imageCache.set(url, cacheEntry);

        // Start loading through arcadeHud
        if (typeof arcadeHud === 'undefined' || !arcadeHud.loadImage) {
            console.error('arcadeHud.loadImage not available, falling back to direct URL');
            this.updateImageElement(imgElement, url, 'error');
            cacheEntry.status = 'error';
            return;
        }

        arcadeHud.loadImage(url)
            .then(result => {
                cacheEntry.status = 'cached';
                cacheEntry.filePath = result.filePath;

                // Update all elements waiting for this image
                if (cacheEntry.elements) {
                    cacheEntry.elements.forEach(el => {
                        this.updateImageElement(el, result.filePath, 'loaded');
                    });
                }
            })
            .catch(error => {
                console.error(`Failed to load image ${url}:`, error);
                cacheEntry.status = 'error';

                // Update all elements waiting for this image
                if (cacheEntry.elements) {
                    cacheEntry.elements.forEach(el => {
                        this.updateImageElement(el, null, 'error');
                    });
                }
            });
    }

    updateImageElement(imgElement, filePath, className) {
        // Remove loading class
        imgElement.classList.remove('loading');

        if (className === 'loaded' && filePath) {
            imgElement.src = filePath;
            imgElement.classList.add('loaded');
            imgElement.classList.remove('error');
        } else if (className === 'error') {
            // Show placeholder on error
            imgElement.outerHTML = '<div class="card-image placeholder">🖼️</div>';
        }
    }

    showEntryDetail(entry) {
        this.elements.modalTitle.textContent = entry.title || 'Entry Details';
        this.elements.modalBody.innerHTML = this.createDetailView(entry);
        this.elements.modalOverlay.style.display = 'flex';

        // Load image for detail view
        const detailImg = this.elements.modalBody.querySelector('img[data-url]');
        if (detailImg) {
            const url = detailImg.getAttribute('data-url');
            this.loadImageWithCache(url, detailImg);
        }
    }

    createDetailView(entry) {
        let html = '';

        // Main image - use placeholder and load from cache
        const imageUrl = this.getBestImage(entry);
        if (imageUrl) {
            html += `<img src="${this.placeholderImage}" data-url="${imageUrl}" alt="${entry.title || 'Entry'}" class="detail-image loading">`;
        }
        
        // Basic information
        html += `<div class="detail-section">
            <h4>Basic Information</h4>
            <div class="detail-grid">`;
        
        if (entry.title) {
            html += `<div class="detail-label">Title:</div>
                     <div class="detail-value">${this.escapeHtml(entry.title)}</div>`;
        }
        
        if (entry.description) {
            html += `<div class="detail-label">Description:</div>
                     <div class="detail-value">${this.escapeHtml(entry.description)}</div>`;
        }
        
        if (entry.keywords) {
            html += `<div class="detail-label">Keywords:</div>
                     <div class="detail-value">${this.escapeHtml(entry.keywords)}</div>`;
        }
        
        html += `</div></div>`;
        
        // Links and files
        const linkFields = ['file', 'reference', 'preview', 'stream'];
        const hasLinks = linkFields.some(field => entry[field]);
        
        if (hasLinks) {
            html += `<div class="detail-section">
                <h4>Links & Files</h4>
                <div class="detail-grid">`;
            
            linkFields.forEach(field => {
                if (entry[field]) {
                    html += `<div class="detail-label">${this.capitalizeFirst(field)}:</div>
                             <div class="detail-value"><a href="${entry[field]}" target="_blank">${entry[field]}</a></div>`;
                }
            });
            
            html += `</div></div>`;
        }

        // Local data
        if (entry.local) {
            html += `<div class="detail-section">
                <h4>Local Data</h4>
                <div class="detail-grid">`;
            
            this.renderNestedObject(entry.local, html);
            
            html += `</div></div>`;
        }
        
        // Actions
        html += `<div class="detail-section">
            <h4>Actions</h4>
            <button class="spawn-btn" style="padding: 10px 20px; background: #0f9d58; color: white; border: none; border-radius: 6px; cursor: pointer; font-size: 14px;"
                onclick="window.libraryBrowser.spawnObject('${this.escapeAttr(entry.id)}', '${this.escapeAttr(entry.file || '')}', '${this.escapeAttr(entry.screen || '')}')">
                Spawn Object
            </button>
        </div>`;

        // Raw data
        html += `<div class="detail-section">
            <h4>Raw Data</h4>
            <pre style="background: #f5f5f5; padding: 10px; border-radius: 4px; overflow-x: auto; font-size: 12px;">${JSON.stringify(entry, null, 2)}</pre>
        </div>`;

        return html;
    }

    escapeAttr(str) {
        return str.replace(/'/g, "\\'").replace(/"/g, '&quot;');
    }

    renderNestedObject(obj, html) {
        Object.entries(obj).forEach(([key, value]) => {
            if (typeof value === 'object' && value !== null) {
                html += `<div class="detail-label">${this.capitalizeFirst(key)}:</div>
                         <div class="detail-value">${JSON.stringify(value, null, 2)}</div>`;
            } else {
                let displayValue = value;
                if (key === 'modified' || key === 'created') {
                    displayValue = new Date(parseInt(value) * 1000).toLocaleString();
                }
                html += `<div class="detail-label">${this.capitalizeFirst(key)}:</div>
                         <div class="detail-value">${this.escapeHtml(displayValue)}</div>`;
            }
        });
    }

    closeModal() {
        this.elements.modalOverlay.style.display = 'none';
    }

    updateStatus(message) {
        this.elements.statusText.textContent = message;
    }

    updateEntryCount() {
        const total = this.currentEntries.length;
        
        if (this.isSearchMode) {
            this.elements.entryCount.textContent = `${total} search results`;
        } else {
            this.elements.entryCount.textContent = `${total} entries`;
        }
    }

    showLoading() {
        this.elements.loadingIndicator.style.display = 'flex';
        this.elements.loadBtn.disabled = true;
        this.elements.loadMoreBtn.disabled = true;
    }

    hideLoading() {
        this.elements.loadingIndicator.style.display = 'none';
        this.elements.loadBtn.disabled = false;
        if (this.hasMoreEntries) {
            this.elements.loadMoreBtn.disabled = false;
        }
    }

    showError(message) {
        this.elements.errorMessage.textContent = message;
        this.elements.errorMessage.style.display = 'block';
        this.updateStatus('Error occurred');
    }

    hideError() {
        this.elements.errorMessage.style.display = 'none';
    }

    // Utility functions
    escapeHtml(text) {
        const div = document.createElement('div');
        div.textContent = text;
        return div.innerHTML;
    }

    capitalizeFirst(str) {
        return str.charAt(0).toUpperCase() + str.slice(1);
    }

    spawnObject(itemId, fileUrl, previewUrl) {
        if (window.aapi && aapi.manager && aapi.manager.spawnItemObject) {
            aapi.manager.spawnItemObject(itemId, fileUrl, previewUrl);
            /* Close the detail modal */
            this.elements.modalOverlay.style.display = 'none';
            /* Close the menu so the player can see the spawned object */
            aapi.manager.closeMenu();
        }
    }
}

// Initialize the library browser when the page loads
document.addEventListener('DOMContentLoaded', function() {
    // Check if the aapi object is available
    if (typeof aapi === 'undefined') {
        console.error('aapi object not found. Make sure the C++ bridge is properly initialized.');
        document.getElementById('statusText').textContent = '❌ API not available';
        return;
    }

    // Initialize the library browser
    const libraryBrowser = new LibraryBrowser();
    
    // Make it available globally for debugging
    window.libraryBrowser = libraryBrowser;
    
    console.log('Library browser initialized successfully');
});