function initEditItem() {
    /* Get item ID from URL parameter */
    var params = new URLSearchParams(window.location.search);
    var itemId = params.get('id') || '';
    var item = null;

    /* Load item data from bridge */
    try {
        if (window.aapi && aapi.library && aapi.library.getItemById) {
            item = aapi.library.getItemById(itemId);
        }
    } catch (e) {}

    var title = (item && item.title) ? 'Item Properties: ' + item.title : 'Item Properties';

    /* Placeholder save function — will use JS bridge in next task */
    function saveItemAttribute(field, value) {
        console.log('[editItem] saveItemAttribute: field=' + field + ' value=' + value + ' itemId=' + itemId);
        /* TODO: aapi.library.updateItem(itemId, field, value) */
    }

    /* Flash saved highlight on an input */
    function flashSaved(inputEl) {
        inputEl.classList.add('aa-edit-saved');
        setTimeout(function() { inputEl.classList.remove('aa-edit-saved'); }, 80);
    }

    /* Helper: create a form row */
    function createRow(icon, label, value, opts) {
        opts = opts || {};
        var row = document.createElement('tr');
        row.className = 'aa-edit-row';

        /* Icon cell */
        var iconTd = document.createElement('td');
        iconTd.className = 'aa-edit-cell-icon';
        var iconEl = document.createElement('img');
        iconEl.className = 'aa-edit-row-icon';
        iconEl.src = 'icons/' + icon;
        iconEl.alt = '';
        iconTd.appendChild(iconEl);
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
            input.className = 'aa-edit-row-input aa-edit-row-select';
            if (opts.options) {
                for (var i = 0; i < opts.options.length; i++) {
                    var o = document.createElement('option');
                    o.value = opts.options[i].value || opts.options[i];
                    o.textContent = opts.options[i].label || opts.options[i];
                    input.appendChild(o);
                }
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
        if (opts.actions) {
            for (var a = 0; a < opts.actions.length; a++) {
                var btn = document.createElement('button');
                btn.className = 'aa-edit-row-action-btn';
                btn.innerHTML = opts.actions[a].icon;
                btn.title = opts.actions[a].title || '';
                if (opts.actions[a].onClick) btn.addEventListener('click', opts.actions[a].onClick);
                actionsTd.appendChild(btn);
            }
        }
        row.appendChild(actionsTd);

        /* Wire up confirm/revert/auto-save behavior */
        var fieldName = opts.field || label.toLowerCase().replace(/\s+/g, '_');
        if (opts.type === 'select') {
            /* Select: auto-save on change */
            input.addEventListener('change', function() {
                saveItemAttribute(fieldName, input.value);
                flashSaved(input);
            });
            /* Enhance with custom dropdown */
            if (typeof arcadeHud !== 'undefined' && arcadeHud.ui && arcadeHud.ui.enhanceSelect) {
                arcadeHud.ui.enhanceSelect(input);
            }
        } else {
            /* Text input: confirm on Enter, revert on blur */
            var originalValue = value || '';
            var confirmed = false;
            input.addEventListener('focus', function() {
                originalValue = input.value;
                confirmed = false;
            });
            input.addEventListener('keydown', function(e) {
                if (e.key === 'Enter') {
                    e.preventDefault();
                    confirmed = true;
                    saveItemAttribute(fieldName, input.value);
                    originalValue = input.value;
                    flashSaved(input);
                    input.blur();
                }
            });
            input.addEventListener('blur', function() {
                if (!confirmed) {
                    input.value = originalValue;
                }
            });
        }

        return { row: row, input: input };
    }

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

    /* Load apps for dropdown */
    var appOptions = [{ value: '', label: 'Default (Windows)' }];
    try {
        if (window.aapi && aapi.library && aapi.library.getApps) {
            var apps = aapi.library.getApps(0, 100);
            for (var ap = 0; ap < apps.length; ap++) {
                appOptions.push({ value: apps[ap].id, label: apps[ap].title || apps[ap].id });
            }
        }
    } catch (e) {}

    /* Tab callbacks */
    function onGeneralTab(content) {
        content.innerHTML = '';
        var form = document.createElement('table');
        form.className = 'aa-edit-form';

        var r1 = createRow('titleicon.png', 'Title', item ? item.title : '');
        form.appendChild(r1.row);

        var r2 = createRow('typeicon.png', 'Type', item ? item.type : '', {
            type: 'select',
            options: typeOptions,
            actions: [
                { icon: '<img src="icons/editicon.png" class="aa-edit-action-icon">', title: 'Edit type' },
                { icon: '<img src="icons/plusicon.png" class="aa-edit-action-icon">', title: 'Add new type' }
            ]
        });
        form.appendChild(r2.row);

        var r3 = createRow('appicon.png', 'Open With', item ? item.app : '', {
            type: 'select',
            options: appOptions,
            actions: [
                {
                    icon: '<img src="icons/editicon.png" class="aa-edit-action-icon">',
                    title: 'Edit app',
                    onClick: function() {
                        var sel = r3.input;
                        if (sel && sel.value)
                            window.location = 'file:///aarcadecore/ui/editApp.html?appId=' + encodeURIComponent(sel.value);
                    }
                },
                { icon: '<img src="icons/plusicon.png" class="aa-edit-action-icon">', title: 'Create new app' }
            ]
        });
        form.appendChild(r3.row);

        var r4 = createRow('fileicon.png', 'File Target', item ? item.file : '', {
            actions: [
                { icon: '<img src="icons/browseicon.png" class="aa-edit-action-icon">', title: 'Browse for file' }
            ]
        });
        form.appendChild(r4.row);

        content.appendChild(form);
    }

    function onVisualTab(content) {
        content.innerHTML = '';
        var form = document.createElement('table');
        form.className = 'aa-edit-form';

        form.appendChild(createRow('screenicon.png', 'Screen Image', item ? item.screen : '').row);
        form.appendChild(createRow('marqueeicon.png', 'Marquee Image', item ? item.marquee : '').row);
        form.appendChild(createRow('previewicon.png', 'Preview Website', item ? item.preview : '').row);

        content.appendChild(form);
    }

    function onOtherTab(content) {
        content.innerHTML = '';
        var form = document.createElement('table');
        form.className = 'aa-edit-form';

        form.appendChild(createRow('streamicon.png', 'Stream Website', '').row);
        form.appendChild(createRow('downloadicon.png', 'Download Website', '').row);
        form.appendChild(createRow('referenceicon.png', 'Reference Website', '').row);
        form.appendChild(createRow('descriptionicon.png', 'Description', item ? item.description : '').row);
        form.appendChild(createRow('tagsicon.png', 'Tags', '').row);

        content.appendChild(form);
    }

    arcadeHud.ui.createWindow({
        title: 'Item Properties',
        showBack: true,
        showClose: true,
        onBack: function() { window.history.back(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); },
        tabs: [
            { label: 'General', onActivate: onGeneralTab },
            { label: 'Visual', onActivate: onVisualTab },
            { label: 'Other', onActivate: onOtherTab }
        ]
    });
}
