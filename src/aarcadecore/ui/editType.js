/* editType.js — Item Type Properties Editor */

function initEditType() {
    var params = new URLSearchParams(window.location.search);
    var typeId = params.get('id') || '';

    /* Types don't have a dedicated getTypeById binding yet — scan the full list. */
    var type = null;
    try {
        if (window.aapi && aapi.library && aapi.library.getTypes) {
            var all = aapi.library.getTypes() || [];
            for (var i = 0; i < all.length; i++) {
                if (all[i].id === typeId) { type = all[i]; break; }
            }
        }
    } catch (e) { console.log('editType load error: ' + e); }

    function saveAttribute(field, value) {
        try {
            if (window.aapi && aapi.library && aapi.library.saveTypeAttribute) {
                aapi.library.saveTypeAttribute(typeId, field, value);
                return true;
            }
        } catch (e) { console.log('saveTypeAttribute error: ' + e); }
        return false;
    }

    function flashSaved(el) {
        el.style.transition = 'none';
        el.style.backgroundColor = '#1a5276';
        el.offsetTop;
        el.style.transition = 'background-color 0.5s';
        el.style.backgroundColor = '';
    }

    function createRow(icon, label, value, field) {
        var row = document.createElement('tr');
        row.className = 'aa-edit-row';

        var iconTd = document.createElement('td');
        iconTd.className = 'aa-edit-cell-icon';
        if (icon) {
            var img = document.createElement('img');
            img.className = 'aa-edit-row-icon';
            img.src = 'icons/' + icon;
            iconTd.appendChild(img);
        }
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
        inputTd.appendChild(input);
        row.appendChild(inputTd);

        var actionsTd = document.createElement('td');
        actionsTd.className = 'aa-edit-cell-actions';
        row.appendChild(actionsTd);

        var originalValue = value || '';
        var confirmed = false;
        input.addEventListener('focus', function() { originalValue = input.value; confirmed = false; });
        input.addEventListener('keydown', function(e) {
            if (e.key === 'Enter') {
                e.preventDefault();
                confirmed = true;
                if (saveAttribute(field, input.value)) {
                    originalValue = input.value;
                    flashSaved(input);
                }
                input.blur();
            }
        });
        input.addEventListener('blur', function() { if (!confirmed) input.value = originalValue; });

        return { row: row, input: input };
    }

    function onGeneralTab(content) {
        content.innerHTML = '';

        if (!type) {
            var err = document.createElement('p');
            err.style.cssText = 'color:#e87a35; padding:16px;';
            err.textContent = 'Type "' + typeId + '" not found.';
            content.appendChild(err);
            return;
        }

        var form = document.createElement('table');
        form.className = 'aa-edit-form';
        form.appendChild(createRow('titleicon.png', 'Title', type.title, 'title').row);
        form.appendChild(createRow('typeicon.png', 'Priority', String(type.priority || 0), 'priority').row);
        content.appendChild(form);

        if (!(window.aapi && aapi.library && aapi.library.saveTypeAttribute)) {
            var note = document.createElement('p');
            note.style.cssText = 'color:#e8a735; font-size:12px; padding:8px 0;';
            note.textContent = 'aapi.library.saveTypeAttribute is not available in this build — edits will not persist.';
            content.appendChild(note);
        }
    }

    function onDeleteTab(content) {
        content.innerHTML = '';

        if (!type) {
            content.innerHTML = '<p style="color:#888; padding:16px;">Nothing to delete.</p>';
            return;
        }

        var warn = document.createElement('p');
        warn.style.cssText = 'color:#e8a735; padding:8px 0;';
        warn.textContent = 'Delete the type "' + type.title + '"? Items with this type may become untyped. This cannot be undone.';
        content.appendChild(warn);

        var status = document.createElement('p');
        status.style.cssText = 'color:#aaa; font-size:12px; margin:0 0 12px;';
        content.appendChild(status);

        var deleteBtn = document.createElement('button');
        deleteBtn.className = 'aa-btn';
        deleteBtn.style.cssText = 'background:#8a2020; color:#fff;';
        deleteBtn.textContent = 'Delete Type';
        var confirmStep = false;
        deleteBtn.addEventListener('click', function() {
            if (!confirmStep) {
                confirmStep = true;
                deleteBtn.textContent = 'Click again to confirm delete';
                status.textContent = 'Click the button once more to confirm.';
                return;
            }
            try {
                if (!(window.aapi && aapi.library && aapi.library.deleteType)) {
                    status.style.color = '#e8a735';
                    status.textContent = 'aapi.library.deleteType is not available in this build.';
                    return;
                }
                var ok = aapi.library.deleteType(typeId);
                if (!ok) { status.style.color = '#e87a35'; status.textContent = 'Failed to delete.'; return; }
                if (window.aapi && aapi.manager) aapi.manager.closeMenu();
                else window.history.back();
            } catch (e) {
                status.style.color = '#e87a35';
                status.textContent = 'Error: ' + e;
            }
        });
        content.appendChild(deleteBtn);
    }

    arcadeHud.ui.createWindow({
        title: type ? ('Type: ' + type.title) : 'Type',
        showBack: true,
        showClose: true,
        onBack: function() { window.history.back(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); },
        tabs: [
            { label: 'General', onActivate: onGeneralTab },
            { label: 'Delete',  onActivate: onDeleteTab }
        ]
    });
}
