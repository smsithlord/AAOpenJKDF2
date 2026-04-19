function initCreateItem() {
    var params = new URLSearchParams(window.location.search);
    var initialFile = params.get('file') || '';
    var givenType  = params.get('type') || '';
    var badyt      = params.get('badyt') === '1';

    var TUBEINFO_URL = 'https://anarchyarcade.com/metaverse/tubeinfo.php';
    var APPLIED_FIELDS = ['title', 'type', 'file', 'screen', 'preview', 'marquee', 'description', 'app'];

    /* Load types for dropdown */
    var types = [];
    try {
        if (window.aapi && aapi.library && aapi.library.getTypes) {
            types = aapi.library.getTypes() || [];
        }
    } catch (e) {}

    function onCreateTab(content) {
        content.innerHTML = '';

        /* In-memory item — accumulates fetched fields until Create & Spawn. */
        var itemFields = { title: '', type: givenType || '', file: initialFile, screen: '', preview: '', marquee: '', description: '', app: '' };

        /* File/URL input */
        var fileLabel = document.createElement('label');
        fileLabel.style.cssText = 'color:#aaa; font-size:12px; display:block; margin-bottom:4px;';
        fileLabel.textContent = 'File or URL:';
        content.appendChild(fileLabel);

        var fileInput = document.createElement('input');
        fileInput.type = 'text';
        fileInput.className = 'aa-edit-row-input';
        fileInput.style.cssText = 'width:100%; margin-bottom:12px;';
        fileInput.value = initialFile;
        fileInput.placeholder = 'Enter a URL, Steam App ID, or local file path...';
        content.appendChild(fileInput);

        /* Check button */
        var checkBtn = document.createElement('button');
        checkBtn.className = 'aa-btn';
        checkBtn.style.marginBottom = '8px';
        checkBtn.textContent = 'Check & Continue';
        content.appendChild(checkBtn);

        /* Status message */
        var status = document.createElement('p');
        status.style.cssText = 'color:#aaa; font-size:12px; margin:0 0 12px;';
        content.appendChild(status);

        /* Fetching spinner area (shown while tubeinfo request is in flight) */
        var fetchingArea = document.createElement('div');
        fetchingArea.style.cssText = 'display:none; color:#e8a735; font-size:14px; padding:16px; text-align:center;';
        fetchingArea.textContent = 'PLEASE WAIT — fetching video info…';
        content.appendChild(fetchingArea);

        /* Form area (hidden until check passes) */
        var formArea = document.createElement('div');
        formArea.style.display = 'none';
        content.appendChild(formArea);

        /* Title input */
        var titleLabel = document.createElement('label');
        titleLabel.style.cssText = 'color:#aaa; font-size:12px; display:block; margin-bottom:4px;';
        titleLabel.textContent = 'Title:';
        formArea.appendChild(titleLabel);

        var titleInput = document.createElement('input');
        titleInput.type = 'text';
        titleInput.className = 'aa-edit-row-input';
        titleInput.style.cssText = 'width:100%; margin-bottom:12px;';
        titleInput.placeholder = 'Item name';
        formArea.appendChild(titleInput);

        /* Type dropdown */
        var typeLabel = document.createElement('label');
        typeLabel.style.cssText = 'color:#aaa; font-size:12px; display:block; margin-bottom:4px;';
        typeLabel.textContent = 'Type:';
        formArea.appendChild(typeLabel);

        var typeSelect = document.createElement('select');
        typeSelect.className = 'aa-edit-row-input aa-edit-row-select';
        typeSelect.style.cssText = 'width:100%; margin-bottom:12px;';
        for (var i = 0; i < types.length; i++) {
            var opt = document.createElement('option');
            opt.value = types[i].id;
            opt.textContent = types[i].title || types[i].id;
            if (givenType && types[i].id === givenType) opt.selected = true;
            typeSelect.appendChild(opt);
        }
        formArea.appendChild(typeSelect);
        if (typeof arcadeHud !== 'undefined' && arcadeHud.ui && arcadeHud.ui.enhanceSelect) {
            arcadeHud.ui.enhanceSelect(typeSelect);
        }

        /* File display (read-only) */
        var fileDisplay = document.createElement('label');
        fileDisplay.style.cssText = 'color:#666; font-size:11px; display:block; margin-bottom:12px;';
        formArea.appendChild(fileDisplay);

        /* Confirm button */
        var confirmBtn = document.createElement('button');
        confirmBtn.className = 'aa-btn';
        confirmBtn.textContent = 'Create & Spawn';
        confirmBtn.addEventListener('click', function() {
            var title = titleInput.value.trim();
            var type = typeSelect.value;
            var file = fileInput.value.trim();
            if (!file) { status.textContent = 'File/URL is required.'; return; }
            if (!title) title = file;

            try {
                var newId = aapi.library.createItem(title, type, file);
                if (!newId) { status.textContent = 'Failed to create item.'; return; }

                /* Apply any extra fields gathered from tubeinfo.php / badyt fallback. */
                if (aapi.library.updateItem) {
                    var extras = ['screen', 'preview', 'marquee', 'description', 'app'];
                    for (var e = 0; e < extras.length; e++) {
                        var field = extras[e];
                        var val = itemFields[field];
                        if (val) {
                            try { aapi.library.updateItem(newId, field, val); } catch (_) {}
                        }
                    }
                }

                aapi.manager.spawnItemObject(newId);
                aapi.manager.closeMenu();
            } catch (e) {
                status.textContent = 'Error: ' + e;
            }
        });
        formArea.appendChild(confirmBtn);

        checkBtn.addEventListener('click', doCheck);

        /* Enter key on file input triggers check */
        fileInput.addEventListener('keydown', function(e) {
            if (e.key === 'Enter') { e.preventDefault(); doCheck(); }
        });

        function showForm(file) {
            fetchingArea.style.display = 'none';
            formArea.style.display = 'block';
            checkBtn.style.display = '';
            fileDisplay.textContent = 'File: ' + file;
        }

        function hideFormForFetch() {
            formArea.style.display = 'none';
            checkBtn.style.display = 'none';
            fetchingArea.style.display = 'block';
            status.textContent = '';
        }

        function defaultTitleFromFile(file) {
            var name = file.replace(/\\/g, '/');
            name = name.split('/').pop().split('?')[0];
            name = name.replace(/[_-]/g, ' ').replace(/\.\w+$/, '');
            return name || file;
        }

        function applyServerFields(data) {
            /* Copy every scalar field the server returned into itemFields so it
             * gets persisted when the user hits Create & Spawn. */
            if (!data || typeof data !== 'object') return;
            for (var k in data) {
                if (!Object.prototype.hasOwnProperty.call(data, k)) continue;
                var v = data[k];
                if (v === null || typeof v === 'object') continue;
                itemFields[k] = String(v);
            }
            /* Echo user-visible fields into the form. */
            if (itemFields.title) titleInput.value = itemFields.title;
            if (itemFields.type) {
                var tid = arcadeHud.resolveTypeValue(itemFields.type, types);
                if (tid) {
                    typeSelect.value = tid;
                    itemFields.type = tid;
                }
            }
        }

        function reloadWithBadyt() {
            var url = window.location.href;
            var sep = url.indexOf('?') >= 0 ? '&' : '?';
            window.location.href = url + sep + 'badyt=1';
        }

        function fetchYouTubeInfo(ytId, file) {
            /* Preset type to "videos" before the request fires — server may overwrite. */
            var videosTypeId = arcadeHud.findTypeIdByTitle(types, 'videos');
            if (videosTypeId) {
                itemFields.type = videosTypeId;
                typeSelect.value = videosTypeId;
            }

            var isPlaylist = /[?&]list=/i.test(file);

            /* Build the item payload exactly as legacy did:
             * JSON.stringify → encodeURIComponent → encodeRFC5987ValueChars. */
            var payload = {
                title: itemFields.title || defaultTitleFromFile(file),
                type: itemFields.type,
                file: file,
                screen: '', preview: '', marquee: '', description: '', app: ''
            };
            var encodedItem = arcadeHud.encodeRFC5987ValueChars(
                encodeURIComponent(JSON.stringify(payload))
            );
            var url = TUBEINFO_URL + '?item=' + encodedItem;

            hideFormForFetch();

            fetch(url, { method: 'GET', mode: 'cors', cache: 'no-store', credentials: 'omit' })
                .then(function(resp) {
                    if (!resp.ok) throw new Error('HTTP ' + resp.status);
                    return resp.text();
                })
                .then(function(body) {
                    if (!body) { reloadWithBadyt(); return; }
                    var parsed;
                    try { parsed = JSON.parse(body); } catch (_) { reloadWithBadyt(); return; }
                    if (!parsed || !parsed.data) { reloadWithBadyt(); return; }

                    applyServerFields(parsed.data);
                    if (isPlaylist && titleInput.value) {
                        titleInput.value = 'Playlist: ' + titleInput.value;
                        itemFields.title = titleInput.value;
                    }
                    status.style.color = '#0f9d58';
                    status.textContent = 'Video info loaded.';
                    showForm(file);
                })
                .catch(function() { reloadWithBadyt(); });
        }

        function applyBadytFallback(ytId, file) {
            var videosTypeId = arcadeHud.findTypeIdByTitle(types, 'videos');
            if (videosTypeId) {
                itemFields.type = videosTypeId;
                typeSelect.value = videosTypeId;
            }
            itemFields.screen = 'https://i.ytimg.com/vi/' + ytId + '/maxresdefault.jpg';
            status.style.color = '#e8a735';
            status.textContent = 'Could not fetch video info. Using fallback thumbnail.';
        }

        function doCheck() {
            var file = fileInput.value.trim();
            if (!file) { status.textContent = 'Please enter a file or URL.'; return; }

            itemFields.file = file;

            /* Already in the library? */
            var existingId = null;
            try {
                if (window.aapi && aapi.library && aapi.library.findItemByFile) {
                    existingId = aapi.library.findItemByFile(file);
                }
            } catch (e) {}

            if (existingId) {
                status.textContent = 'Item already exists in your library.';
                status.style.color = '#e8a735';
                formArea.style.display = 'none';
                fetchingArea.style.display = 'none';

                var spawnExisting = document.createElement('button');
                spawnExisting.className = 'aa-btn aa-spawn-existing';
                spawnExisting.style.marginTop = '8px';
                spawnExisting.textContent = 'Spawn Existing Item';
                spawnExisting.addEventListener('click', function() {
                    try {
                        aapi.manager.spawnItemObject(existingId);
                        aapi.manager.closeMenu();
                    } catch (e) {}
                });
                var prev = content.querySelector('.aa-spawn-existing');
                if (prev) prev.remove();
                content.appendChild(spawnExisting);
                return;
            }

            /* New item — kick off detection. */
            status.textContent = 'New item. Fill in the details below.';
            status.style.color = '#0f9d58';
            var prev = content.querySelector('.aa-spawn-existing');
            if (prev) prev.remove();

            if (!titleInput.value) titleInput.value = defaultTitleFromFile(file);
            itemFields.title = titleInput.value;

            var ytId = arcadeHud.extractYouTubeId(file);

            if (ytId && !badyt) {
                fetchYouTubeInfo(ytId, file);
                return;
            }
            if (ytId && badyt) {
                applyBadytFallback(ytId, file);
                showForm(file);
                return;
            }

            /* Non-YouTube auto-detect. */
            var detectedTypeId = arcadeHud.detectItemType(file, types, { badyt: badyt });
            if (detectedTypeId) {
                itemFields.type = detectedTypeId;
                typeSelect.value = detectedTypeId;
            }
            showForm(file);
        }

        /* Auto-run check when a file was passed in via ?file=... */
        if (initialFile) {
            setTimeout(doCheck, 100);
        }

        fileInput.focus();
    }

    arcadeHud.ui.createWindow({
        title: 'Create New Item',
        showBack: true,
        showClose: true,
        onBack: function() { window.history.back(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); },
        tabs: [
            { label: 'Create', onActivate: onCreateTab }
        ]
    });
}
