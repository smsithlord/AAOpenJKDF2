function initCreateItem() {
    var params = new URLSearchParams(window.location.search);
    var initialFile = params.get('file') || '';
    var givenType  = params.get('type') || '';
    var badyt      = params.get('badyt') === '1';
    var tabParam   = (params.get('tab') || '').toLowerCase();

    var TUBEINFO_URL = 'https://anarchyarcade.com/metaverse/tubeinfo.php';
    var APPLIED_FIELDS = ['title', 'type', 'file', 'screen', 'preview', 'marquee', 'description', 'app'];

    /* Load types for dropdown. Raw list used by title/detection helpers;
     * `typeOptions` is what the Type select actually renders and has the
     * blank "Other" sentinel prepended (value ''). */
    var types = [];
    try {
        if (window.aapi && aapi.library && aapi.library.getTypes) {
            types = aapi.library.getTypes() || [];
        }
    } catch (e) {}
    var typeOptions = arcadeHud.typeOptionsWithOther(types);

    /* Load Open-With apps — same list editItem.js uses, with the "no app set"
     * sentinel as the first entry. Picking an app auto-guesses Type from the
     * app's declared type. */
    var apps = [];
    try {
        if (window.aapi && aapi.library && aapi.library.getApps) {
            apps = aapi.library.getApps(0, 100) || [];
        }
    } catch (e) {}
    var appOptions = [{ value: '', label: 'Default (Windows)' }];
    for (var aoi = 0; aoi < apps.length; aoi++) {
        appOptions.push({ value: apps[aoi].id, label: apps[aoi].title || apps[aoi].id });
    }

    /* Without an Open-With app, Type stays on the "Other" sentinel (''). With
     * one, inherit that app's declared type if it has one. */
    function typeIdForApp(appId) {
        if (!appId) return '';
        try {
            if (window.aapi && aapi.library && aapi.library.getAppById) {
                var a = aapi.library.getAppById(appId);
                if (a && a.type) return a.type;
            }
        } catch (e) {}
        return '';
    }

    function onItemTab(content) {
        content.innerHTML = '';

        /* In-memory item — accumulates fetched fields until Create & Spawn. */
        var itemFields = { title: '', type: givenType || '', file: initialFile, screen: '', preview: '', marquee: '', description: '', app: '' };

        var intro = document.createElement('p');
        intro.style.cssText = 'color:#aaa; font-size:12px; margin:0 0 12px;';
        intro.textContent = 'Spawn an item into the world from a URL, Steam App ID, or local file path.';
        content.appendChild(intro);

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
        for (var i = 0; i < typeOptions.length; i++) {
            var opt = document.createElement('option');
            opt.value = typeOptions[i].value;
            opt.textContent = typeOptions[i].label;
            if (givenType && typeOptions[i].value === givenType) opt.selected = true;
            typeSelect.appendChild(opt);
        }
        /* Default to the blank "Other" sentinel (value '') when no givenType
         * was passed in, matching how the Open-With dropdown defaults to
         * "Default (Windows)". The Open-With change handler overrides this
         * when the user picks an app that declares its own type. */
        if (!givenType) typeSelect.value = '';
        formArea.appendChild(typeSelect);
        if (typeof arcadeHud !== 'undefined' && arcadeHud.ui && arcadeHud.ui.enhanceSelect) {
            arcadeHud.ui.enhanceSelect(typeSelect);
        }

        /* Open-With dropdown — picking an app auto-guesses Type from that
         * app's declared type. Default sentinel "Default (Windows)" means no
         * Open-With app is set; in that case Type stays on "Other". */
        var appLabel = document.createElement('label');
        appLabel.style.cssText = 'color:#aaa; font-size:12px; display:block; margin-bottom:4px;';
        appLabel.textContent = 'Open With:';
        formArea.appendChild(appLabel);

        var appSelect = document.createElement('select');
        appSelect.className = 'aa-edit-row-input aa-edit-row-select';
        appSelect.style.cssText = 'width:100%; margin-bottom:12px;';
        for (var ao = 0; ao < appOptions.length; ao++) {
            var aOpt = document.createElement('option');
            aOpt.value = appOptions[ao].value;
            aOpt.textContent = appOptions[ao].label;
            appSelect.appendChild(aOpt);
        }
        formArea.appendChild(appSelect);
        appSelect.addEventListener('change', function() {
            itemFields.app = appSelect.value;
            var guessedType = typeIdForApp(appSelect.value);
            if (guessedType) {
                /* Only apply if that type id is actually an option (don't
                 * silently stamp an invalid id onto the select). */
                var has = false;
                for (var k = 0; k < typeSelect.options.length; k++) {
                    if (typeSelect.options[k].value === guessedType) { has = true; break; }
                }
                if (has) {
                    typeSelect.value = guessedType;
                    itemFields.type = guessedType;
                }
            }
        });
        if (typeof arcadeHud !== 'undefined' && arcadeHud.ui && arcadeHud.ui.enhanceSelect) {
            arcadeHud.ui.enhanceSelect(appSelect);
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

    function onFavoritesTab(content) {
        content.innerHTML = '';

        var intro = document.createElement('p');
        intro.style.cssText = 'color:#aaa; font-size:12px; margin:0 0 12px;';
        intro.textContent = 'Create a new favorites list to organize items and models you care about.';
        content.appendChild(intro);

        var titleLabel = document.createElement('label');
        titleLabel.style.cssText = 'color:#aaa; font-size:12px; display:block; margin-bottom:4px;';
        titleLabel.textContent = 'List name:';
        content.appendChild(titleLabel);

        var titleInput = document.createElement('input');
        titleInput.type = 'text';
        titleInput.className = 'aa-edit-row-input';
        titleInput.style.cssText = 'width:100%; margin-bottom:12px;';
        titleInput.placeholder = 'e.g. "Favorites", "Co-op Games"';
        content.appendChild(titleInput);

        var status = document.createElement('p');
        status.style.cssText = 'color:#aaa; font-size:12px; margin:0 0 12px;';
        content.appendChild(status);

        var confirmBtn = document.createElement('button');
        confirmBtn.className = 'aa-btn';
        confirmBtn.textContent = 'Create Favorites List';
        confirmBtn.addEventListener('click', function() {
            var name = titleInput.value.trim();
            if (!name) { status.style.color = '#e8a735'; status.textContent = 'List name is required.'; return; }
            try {
                var list = arcadeHud.favorites.createFavoritesList(null, name, '');
                if (!list) { status.style.color = '#e87a35'; status.textContent = 'Failed to create list.'; return; }
                arcadeHud.favorites.setActiveFavoritesList(list.id);
                if (window.aapi && aapi.manager) aapi.manager.closeMenu();
            } catch (e) {
                status.style.color = '#e87a35';
                status.textContent = 'Error: ' + e;
            }
        });
        content.appendChild(confirmBtn);

        titleInput.focus();
    }

    function onAppTab(content) {
        content.innerHTML = '';

        var intro = document.createElement('p');
        intro.style.cssText = 'color:#aaa; font-size:12px; margin:0 0 12px;';
        intro.textContent = 'Register an "Open With" app so items can be launched through it.';
        content.appendChild(intro);

        /* Executable path */
        var fileLabel = document.createElement('label');
        fileLabel.style.cssText = 'color:#aaa; font-size:12px; display:block; margin-bottom:4px;';
        fileLabel.textContent = 'Executable:';
        content.appendChild(fileLabel);

        var fileRow = document.createElement('div');
        fileRow.style.cssText = 'display:flex; align-items:stretch; gap:6px; margin-bottom:12px;';
        var fileInput = document.createElement('input');
        fileInput.type = 'text';
        fileInput.className = 'aa-edit-row-input';
        /* min-width:0 lets the flex input shrink below its intrinsic content width — without
         * it, the placeholder/value holds the input at natural size and squashes the row. */
        fileInput.style.cssText = 'flex:1 1 auto; min-width:0;';
        fileInput.placeholder = 'Full path to the app executable…';
        var browseBtn = document.createElement('button');
        browseBtn.className = 'aa-btn';
        browseBtn.title = 'Browse…';
        browseBtn.setAttribute('helpText', 'Browse for executable');
        /* Hidden until the aapi.manager.browseForFile native binding is wired up. */
        browseBtn.style.cssText = 'display:none; flex:0 0 auto; padding:4px 8px; line-height:1;';
        browseBtn.innerHTML = '<img src="icons/browseicon.png" style="width:16px; height:16px; vertical-align:middle; display:block;">';
        browseBtn.addEventListener('click', function() {
            try {
                if (window.aapi && aapi.manager && aapi.manager.browseForFile) {
                    var picked = aapi.manager.browseForFile('', 'exe');
                    if (picked) fileInput.value = picked;
                } else {
                    status.style.color = '#e8a735';
                    status.textContent = 'File browser not available — paste the executable path manually.';
                }
            } catch (e) {
                status.style.color = '#e87a35';
                status.textContent = 'Browse error: ' + e;
            }
        });
        fileRow.appendChild(fileInput);
        fileRow.appendChild(browseBtn);
        content.appendChild(fileRow);

        /* Title */
        var titleLabel = document.createElement('label');
        titleLabel.style.cssText = 'color:#aaa; font-size:12px; display:block; margin-bottom:4px;';
        titleLabel.textContent = 'Title:';
        content.appendChild(titleLabel);

        var titleInput = document.createElement('input');
        titleInput.type = 'text';
        titleInput.className = 'aa-edit-row-input';
        titleInput.style.cssText = 'width:100%; margin-bottom:12px;';
        titleInput.placeholder = 'App name';
        content.appendChild(titleInput);

        var status = document.createElement('p');
        status.style.cssText = 'color:#aaa; font-size:12px; margin:0 0 12px;';
        content.appendChild(status);

        var confirmBtn = document.createElement('button');
        confirmBtn.className = 'aa-btn';
        confirmBtn.textContent = 'Create App';
        confirmBtn.addEventListener('click', function() {
            var file = fileInput.value.trim();
            var title = titleInput.value.trim();
            if (!file) { status.style.color = '#e8a735'; status.textContent = 'Executable is required.'; return; }
            if (!title) title = file.replace(/\\/g, '/').split('/').pop().replace(/\.\w+$/, '') || file;

            try {
                if (!(window.aapi && aapi.library && aapi.library.createApp)) {
                    status.style.color = '#e8a735';
                    status.textContent = 'aapi.library.createApp is not available in this build.';
                    return;
                }
                var newId = aapi.library.createApp(title, '', file);
                if (!newId) { status.style.color = '#e87a35'; status.textContent = 'Failed to create app.'; return; }
                window.location.href = 'file:///ui/editApp.html?id=' + encodeURIComponent(newId);
            } catch (e) {
                status.style.color = '#e87a35';
                status.textContent = 'Error: ' + e;
            }
        });
        content.appendChild(confirmBtn);

        fileInput.focus();
    }

    function onTypeTab(content) {
        content.innerHTML = '';

        var intro = document.createElement('p');
        intro.style.cssText = 'color:#aaa; font-size:12px; margin:0 0 12px;';
        intro.textContent = 'Create a new item type so library items can be organized by it.';
        content.appendChild(intro);

        var titleLabel = document.createElement('label');
        titleLabel.style.cssText = 'color:#aaa; font-size:12px; display:block; margin-bottom:4px;';
        titleLabel.textContent = 'Title:';
        content.appendChild(titleLabel);

        var titleInput = document.createElement('input');
        titleInput.type = 'text';
        titleInput.className = 'aa-edit-row-input';
        titleInput.style.cssText = 'width:100%; margin-bottom:12px;';
        titleInput.placeholder = 'e.g. "videos", "games"';
        content.appendChild(titleInput);

        var status = document.createElement('p');
        status.style.cssText = 'color:#aaa; font-size:12px; margin:0 0 12px;';
        content.appendChild(status);

        var confirmBtn = document.createElement('button');
        confirmBtn.className = 'aa-btn';
        confirmBtn.textContent = 'Create Type';
        confirmBtn.addEventListener('click', function() {
            var title = titleInput.value.trim();
            if (!title) { status.style.color = '#e8a735'; status.textContent = 'Title is required.'; return; }
            /* Refuse to create a duplicate — compare case-insensitively on title. */
            var needle = title.toLowerCase();
            for (var i = 0; i < types.length; i++) {
                var existing = String(types[i].title || types[i].id || '').trim().toLowerCase();
                if (existing === needle) {
                    status.style.color = '#e8a735';
                    status.textContent = 'A type named "' + (types[i].title || types[i].id) + '" already exists.';
                    return;
                }
            }
            try {
                if (!(window.aapi && aapi.library && aapi.library.createType)) {
                    status.style.color = '#e8a735';
                    status.textContent = 'aapi.library.createType is not available in this build.';
                    return;
                }
                var newId = aapi.library.createType(title);
                if (!newId) { status.style.color = '#e87a35'; status.textContent = 'Failed to create type.'; return; }
                window.location.href = 'file:///ui/editType.html?id=' + encodeURIComponent(newId);
            } catch (e) {
                status.style.color = '#e87a35';
                status.textContent = 'Error: ' + e;
            }
        });
        content.appendChild(confirmBtn);

        titleInput.focus();
    }

    var tabMap = { item: 0, favorites: 1, app: 2, type: 3 };
    var tabOverride = (tabParam in tabMap) ? tabMap[tabParam] : 0;

    arcadeHud.ui.createWindow({
        title: 'Create New',
        showBack: true,
        showClose: true,
        onBack: function() { window.history.back(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); },
        tabOverride: tabOverride,
        tabs: [
            { label: 'Item',      onActivate: onItemTab },
            { label: 'Favorites', onActivate: onFavoritesTab },
            { label: 'App',       onActivate: onAppTab },
            { label: 'Type',      onActivate: onTypeTab }
        ]
    });
}
