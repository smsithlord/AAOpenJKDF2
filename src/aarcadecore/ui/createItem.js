function initCreateItem() {
    var params = new URLSearchParams(window.location.search);
    var initialFile = params.get('file') || '';

    /* Load types for dropdown */
    var typeOptions = [];
    try {
        if (window.aapi && aapi.library && aapi.library.getTypes) {
            var types = aapi.library.getTypes();
            for (var t = 0; t < types.length; t++) {
                typeOptions.push({ value: types[t].id, label: types[t].title || types[t].id });
            }
        }
    } catch (e) {}

    function onCreateTab(content) {
        content.innerHTML = '';

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

        /* Status message */
        var status = document.createElement('p');
        status.style.cssText = 'color:#aaa; font-size:12px; margin:0 0 12px;';
        content.appendChild(status);

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
                if (newId) {
                    aapi.manager.spawnItemObject(newId);
                    aapi.manager.closeMenu();
                } else {
                    status.textContent = 'Failed to create item.';
                }
            } catch (e) {
                status.textContent = 'Error: ' + e;
            }
        });
        formArea.appendChild(confirmBtn);

        /* Check button */
        var checkBtn = document.createElement('button');
        checkBtn.className = 'aa-btn';
        checkBtn.style.marginBottom = '8px';
        checkBtn.textContent = 'Check & Continue';
        checkBtn.addEventListener('click', doCheck);
        content.insertBefore(checkBtn, status);

        /* Enter key on file input triggers check */
        fileInput.addEventListener('keydown', function(e) {
            if (e.key === 'Enter') { e.preventDefault(); doCheck(); }
        });

        function doCheck() {
            var file = fileInput.value.trim();
            if (!file) { status.textContent = 'Please enter a file or URL.'; return; }

            /* Check if item already exists */
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

                /* Add spawn existing button */
                var spawnExisting = document.createElement('button');
                spawnExisting.className = 'aa-btn';
                spawnExisting.style.marginTop = '8px';
                spawnExisting.textContent = 'Spawn Existing Item';
                spawnExisting.addEventListener('click', function() {
                    try {
                        aapi.manager.spawnItemObject(existingId);
                        aapi.manager.closeMenu();
                    } catch (e) {}
                });
                /* Remove previous spawn button if any */
                var prev = content.querySelector('.aa-spawn-existing');
                if (prev) prev.remove();
                spawnExisting.className += ' aa-spawn-existing';
                content.appendChild(spawnExisting);
            } else {
                status.textContent = 'New item. Fill in the details below.';
                status.style.color = '#0f9d58';
                formArea.style.display = 'block';
                fileDisplay.textContent = 'File: ' + file;
                /* Default title from file */
                if (!titleInput.value) {
                    /* Try to extract a reasonable title from URL or path */
                    var name = file.replace(/\\/g, '/');
                    name = name.split('/').pop().split('?')[0];
                    name = name.replace(/[_-]/g, ' ').replace(/\.\w+$/, '');
                    titleInput.value = name || file;
                }
                /* Remove spawn existing button if shown */
                var prev = content.querySelector('.aa-spawn-existing');
                if (prev) prev.remove();
            }
        }

        /* Auto-check if file was passed via URL parameter */
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
