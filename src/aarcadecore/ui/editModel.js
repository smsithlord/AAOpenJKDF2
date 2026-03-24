function initEditModel() {
    var params = new URLSearchParams(window.location.search);
    var modelId = params.get('id') || '';
    var model = null;
    var templateName = '';

    try {
        if (window.aapi && aapi.library) {
            model = aapi.library.getModelById(modelId);
            templateName = aapi.library.getModelPlatformFile(modelId) || '';
        }
    } catch (e) {}

    function saveModelAttribute(field, value) {
        try {
            if (window.aapi && aapi.library && aapi.library.updateModel) {
                aapi.library.updateModel(modelId, field, value);
            }
        } catch (e) {
            console.log('[editModel] save error: ' + e);
        }
    }

    function flashSaved(inputEl) {
        inputEl.classList.add('aa-edit-saved');
        setTimeout(function() { inputEl.classList.remove('aa-edit-saved'); }, 80);
    }

    function createRow(icon, label, value, opts) {
        opts = opts || {};
        var row = document.createElement('tr');
        row.className = 'aa-edit-row';

        var iconTd = document.createElement('td');
        iconTd.className = 'aa-edit-cell-icon';
        var iconEl = document.createElement('img');
        iconEl.className = 'aa-edit-row-icon';
        iconEl.src = 'icons/' + icon;
        iconEl.alt = '';
        iconTd.appendChild(iconEl);
        row.appendChild(iconTd);

        var labelTd = document.createElement('td');
        labelTd.className = 'aa-edit-cell-label';
        labelTd.textContent = label + ':';
        row.appendChild(labelTd);

        var inputTd = document.createElement('td');
        inputTd.className = 'aa-edit-cell-input';
        var input = document.createElement('input');
        input.className = 'aa-edit-row-input';
        input.type = 'text';
        input.value = value || '';
        if (opts.readOnly) {
            input.readOnly = true;
            input.style.opacity = '0.6';
        }
        inputTd.appendChild(input);
        row.appendChild(inputTd);

        var actionsTd = document.createElement('td');
        actionsTd.className = 'aa-edit-cell-actions';
        row.appendChild(actionsTd);

        if (!opts.readOnly) {
            var fieldName = opts.field || label.toLowerCase().replace(/\s+/g, '_');
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
                    saveModelAttribute(fieldName, input.value);
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

    function onGeneralTab(content) {
        content.innerHTML = '';
        var form = document.createElement('table');
        form.className = 'aa-edit-form';

        form.appendChild(createRow('titleicon.png', 'Title', model ? model.title : '', { field: 'title' }).row);
        form.appendChild(createRow('fileicon.png', 'Template Name', templateName, { readOnly: true }).row);
        form.appendChild(createRow('screenicon.png', 'Screen Image', model ? model.screen : '', { field: 'screen' }).row);

        content.appendChild(form);
    }

    arcadeHud.ui.createWindow({
        title: (model && model.title) ? 'Model: ' + model.title : 'Model Properties',
        showBack: true,
        showClose: true,
        onBack: function() { window.history.back(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); },
        tabs: [
            { label: 'General', onActivate: onGeneralTab }
        ]
    });
}
