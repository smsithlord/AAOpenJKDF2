/* editApp.js — Open-With App Properties Editor */

function initEditApp() {
    /* Parse appId from URL params. Accept either ?appId= (legacy) or ?id= (shared edit-page convention). */
    var params = new URLSearchParams(window.location.search);
    var appId = params.get('appId') || params.get('id') || '';

    /* "Default (Windows)" is the "no app set" sentinel in the Open-With
     * dropdown (value=''). It isn't a real app row and has nothing to edit.
     * Render a short explanatory page instead of an editable form if someone
     * lands here with an empty appId. */
    if (!appId) {
        arcadeHud.ui.createWindow({
            title: 'Open-With App Properties',
            showBack: true,
            showClose: true,
            onBack: function() { window.history.back(); },
            onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); },
            tabs: [
                { label: 'General', onActivate: function(content) {
                    content.innerHTML = '';
                    var p = document.createElement('div');
                    p.style.padding = '40px 20px';
                    p.style.textAlign = 'center';
                    p.style.color = '#888';
                    p.textContent = 'Default (Windows) is the "no app set" sentinel and cannot be edited.';
                    content.appendChild(p);
                } }
            ]
        });
        return;
    }

    var app = null;
    var filepaths = [];

    /* Load app data */
    try {
        if (appId && window.aapi && aapi.library) {
            if (aapi.library.getAppById) app = aapi.library.getAppById(appId);
            if (aapi.library.getAppFilepaths) filepaths = aapi.library.getAppFilepaths(appId) || [];
        }
    } catch (e) { console.log('editApp load error: ' + e); }

    /* Load types for dropdown. Blank "Other" sentinel sits at position 0, same
     * as createItem.js / editItem.js. */
    var typeOptions = [{ value: '', label: 'Other' }];
    try {
        if (window.aapi && aapi.library && aapi.library.getTypes) {
            typeOptions = arcadeHud.typeOptionsWithOther(aapi.library.getTypes() || []);
        }
    } catch (e) {}

    function saveAttribute(field, value) {
        try {
            if (window.aapi && aapi.library && aapi.library.saveAppAttribute)
                aapi.library.saveAppAttribute(appId, field, value);
        } catch (e) { console.log('saveAppAttribute error: ' + e); }
    }

    function saveFilepaths() {
        var entries = document.querySelectorAll('.folder-entry');
        var paths = [];
        for (var i = 0; i < entries.length; i++) {
            var p = entries[i].querySelector('.folder-path').value;
            var ext = entries[i].querySelector('.folder-ext').value;
            paths.push({ path: p, extensions: ext });
        }
        try {
            if (window.aapi && aapi.library && aapi.library.saveAppFilepaths)
                aapi.library.saveAppFilepaths(appId, paths);
        } catch (e) { console.log('saveAppFilepaths error: ' + e); }
    }

    function flashSaved(el) {
        el.style.transition = 'none';
        el.style.backgroundColor = '#1a5276';
        el.offsetTop;
        el.style.transition = 'background-color 0.5s';
        el.style.backgroundColor = '';
    }

    function createFormRow(label, value, opts) {
        opts = opts || {};
        var row = document.createElement('tr');
        row.className = 'aa-edit-row';

        /* Icon cell */
        var iconTd = document.createElement('td');
        iconTd.className = 'aa-edit-cell-icon';
        if (opts.icon) {
            var img = document.createElement('img');
            img.className = 'aa-edit-row-icon';
            img.src = 'icons/' + opts.icon;
            iconTd.appendChild(img);
        }
        row.appendChild(iconTd);

        /* Label cell */
        var labelTd = document.createElement('td');
        labelTd.className = 'aa-edit-cell-label';
        labelTd.textContent = label + ':';
        row.appendChild(labelTd);

        /* Input cell */
        var inputTd = document.createElement('td');
        inputTd.className = 'aa-edit-cell-input';
        var input;
        if (opts.type === 'select') {
            input = document.createElement('select');
            input.className = 'aa-edit-row-input';
            for (var i = 0; i < (opts.options || []).length; i++) {
                var o = document.createElement('option');
                o.value = opts.options[i].value || opts.options[i];
                o.textContent = opts.options[i].label || opts.options[i];
                input.appendChild(o);
            }
            input.value = value || '';
        } else {
            input = document.createElement('input');
            input.className = 'aa-edit-row-input';
            input.type = 'text';
            input.value = value || '';
        }
        inputTd.appendChild(input);
        row.appendChild(inputTd);

        /* Actions cell */
        var actionsTd = document.createElement('td');
        actionsTd.className = 'aa-edit-cell-actions';
        row.appendChild(actionsTd);

        /* Wire up save behavior */
        var fieldName = opts.field || label.toLowerCase().replace(/\s+/g, '_');
        if (opts.type === 'select') {
            input.addEventListener('change', function() {
                saveAttribute(fieldName, input.value);
                flashSaved(input);
            });
            if (typeof arcadeHud !== 'undefined' && arcadeHud.ui && arcadeHud.ui.enhanceSelect)
                arcadeHud.ui.enhanceSelect(input);
        } else {
            var originalValue = value || '';
            var confirmed = false;
            input.addEventListener('focus', function() { originalValue = input.value; confirmed = false; });
            input.addEventListener('keydown', function(e) {
                if (e.key === 'Enter') {
                    e.preventDefault();
                    confirmed = true;
                    saveAttribute(fieldName, input.value);
                    originalValue = input.value;
                    flashSaved(input);
                    input.blur();
                }
            });
            input.addEventListener('blur', function() { if (!confirmed) input.value = originalValue; });
        }

        return { row: row, input: input };
    }

    function appendFolderEntry(container, noFoldersMsg, path, extensions) {
        var entry = document.createElement('div');
        entry.className = 'folder-entry';
        entry.style.cssText = 'background:#0a0a0a; border:1px solid #222; border-radius:4px; padding:8px; margin-bottom:6px;';

        var r1 = document.createElement('div');
        r1.style.cssText = 'display:flex; align-items:center; margin-bottom:4px;';
        var l1 = document.createElement('span');
        l1.style.cssText = 'color:#999; font-weight:bold; width:120px; font-size:12px;';
        l1.textContent = 'Content Folder:';
        var i1 = document.createElement('input');
        i1.type = 'text'; i1.value = path || '';
        i1.placeholder = 'Any (all folders accepted)';
        i1.style.cssText = 'flex:1; background:#080808; color:#ccc; border:1px solid #333; border-radius:4px; padding:4px 6px; font-size:12px;';
        i1.className = 'folder-path';
        r1.appendChild(l1); r1.appendChild(i1);

        var r2 = document.createElement('div');
        r2.style.cssText = 'display:flex; align-items:center; margin-bottom:4px;';
        var l2 = document.createElement('span');
        l2.style.cssText = 'color:#999; font-weight:bold; width:120px; font-size:12px;';
        l2.textContent = 'File Extensions:';
        var i2 = document.createElement('input');
        i2.type = 'text'; i2.value = extensions || '';
        i2.placeholder = 'Any (all extensions accepted)';
        i2.style.cssText = 'flex:1; background:#080808; color:#ccc; border:1px solid #333; border-radius:4px; padding:4px 6px; font-size:12px;';
        i2.className = 'folder-ext';
        r2.appendChild(l2); r2.appendChild(i2);

        var r3 = document.createElement('div');
        r3.style.cssText = 'text-align:right;';
        var removeBtn = document.createElement('button');
        removeBtn.textContent = 'Remove Folder';
        removeBtn.style.cssText = 'background:#1a1a1a; color:#999; border:1px solid #333; border-radius:4px; padding:3px 8px; cursor:pointer; font-size:11px; visibility:hidden;';
        removeBtn.addEventListener('click', function() {
            entry.parentNode.removeChild(entry);
            if (container.querySelectorAll('.folder-entry').length === 0) noFoldersMsg.style.display = 'block';
            saveFilepaths();
        });
        r3.appendChild(removeBtn);
        entry.addEventListener('mouseenter', function() { removeBtn.style.visibility = 'visible'; });
        entry.addEventListener('mouseleave', function() { removeBtn.style.visibility = 'hidden'; });

        function setupInput(inp) {
            inp.originalValue = inp.value;
            inp.addEventListener('focus', function() { this.originalValue = this.value; });
            inp.addEventListener('keydown', function(e) {
                if (e.key === 'Enter') {
                    this.originalValue = this.value;
                    this.style.transition = 'none';
                    this.style.backgroundColor = '#1a5276';
                    this.offsetTop;
                    this.style.transition = 'background-color 0.5s';
                    this.style.backgroundColor = '#080808';
                    this.blur();
                    saveFilepaths();
                }
            });
            inp.addEventListener('blur', function() {
                if (this.value !== this.originalValue) this.value = this.originalValue;
            });
        }
        setupInput(i1);
        setupInput(i2);

        entry.appendChild(r1);
        entry.appendChild(r2);
        entry.appendChild(r3);
        container.appendChild(entry);
    }

    /* Tab callbacks */
    function onGeneralTab(content) {
        content.innerHTML = '';
        var form = document.createElement('table');
        form.className = 'aa-edit-form';

        var r1 = createFormRow('Title', app ? app.title : '', { icon: 'titleicon.png', field: 'title' });
        form.appendChild(r1.row);

        var r2 = createFormRow('Type', app ? app.type : '', { icon: 'typeicon.png', field: 'type', type: 'select', options: typeOptions });
        form.appendChild(r2.row);

        var r3 = createFormRow('Executable', app ? app.file : '', { icon: 'fileicon.png', field: 'file' });
        form.appendChild(r3.row);

        var r4 = createFormRow('Command Format', app ? app.commandformat : '', { icon: 'appicon.png', field: 'commandformat' });
        form.appendChild(r4.row);

        content.appendChild(form);

        /* Associated Content Folders section */
        var foldersHeader = document.createElement('div');
        foldersHeader.style.cssText = 'background:rgba(26,82,118,0.3); text-align:center; padding:8px; margin:12px 0 8px 0; border-radius:4px; color:#4a9eda; font-weight:bold;';
        foldersHeader.textContent = 'Associated Content Folders';
        content.appendChild(foldersHeader);

        var noFoldersMsg = document.createElement('div');
        noFoldersMsg.style.cssText = 'text-align:center; color:#888; padding:10px;';
        noFoldersMsg.innerHTML = 'No content folders specified yet.<br><br>Add content folders so this app knows where its files are located.';
        content.appendChild(noFoldersMsg);

        var foldersContainer = document.createElement('div');
        foldersContainer.id = 'foldersContainer';
        content.appendChild(foldersContainer);

        if (filepaths.length > 0) {
            noFoldersMsg.style.display = 'none';
            for (var i = 0; i < filepaths.length; i++)
                appendFolderEntry(foldersContainer, noFoldersMsg, filepaths[i].path, filepaths[i].extensions);
        }

        var addFolderBtn = document.createElement('button');
        addFolderBtn.textContent = 'Add Another Content Folder';
        addFolderBtn.style.cssText = 'width:100%; padding:8px; margin:8px 0; background:#1a1a1a; color:#ccc; border:1px solid #333; border-radius:4px; cursor:pointer;';
        addFolderBtn.addEventListener('click', function() {
            appendFolderEntry(foldersContainer, noFoldersMsg, '', '');
            noFoldersMsg.style.display = 'none';
        });
        content.appendChild(addFolderBtn);
    }

    function onVisualTab(content) {
        content.innerHTML = '<p style="color:#888; padding:16px;">Visual settings placeholder.</p>';
    }

    function onOtherTab(content) {
        content.innerHTML = '<p style="color:#888; padding:16px;">Other settings placeholder.</p>';
    }

    function onDeleteTab(content) {
        content.innerHTML = '<p style="color:#888; padding:16px;">Delete app placeholder.</p>';
    }

    arcadeHud.ui.createWindow({
        title: 'Open-With App Properties',
        showBack: true,
        showClose: true,
        onBack: function() { window.history.back(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); },
        tabs: [
            { label: 'General', onActivate: onGeneralTab },
            { label: 'Visual', onActivate: onVisualTab },
            { label: 'Other', onActivate: onOtherTab },
            { label: 'Delete', onActivate: onDeleteTab }
        ]
    });
}
