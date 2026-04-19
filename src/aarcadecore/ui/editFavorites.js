/* editFavorites.js — Favorites List Properties Editor */

function initEditFavorites() {
    var params = new URLSearchParams(window.location.search);
    var listId = params.get('id') || 'favorites';

    var lists = (arcadeHud.favorites.getLists && arcadeHud.favorites.getLists()) || {};
    var list = lists[listId] || null;

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

        /* Confirm on Enter, revert on blur — same UX as editApp.js. */
        var originalValue = value || '';
        var confirmed = false;
        input.addEventListener('focus', function() { originalValue = input.value; confirmed = false; });
        input.addEventListener('keydown', function(e) {
            if (e.key === 'Enter') {
                e.preventDefault();
                confirmed = true;
                var ok = arcadeHud.favorites.updateFavoritesList(listId, field, input.value);
                if (ok) {
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

        if (!list) {
            var err = document.createElement('p');
            err.style.cssText = 'color:#e87a35; padding:16px;';
            err.textContent = 'Favorites list "' + listId + '" not found.';
            content.appendChild(err);
            return;
        }

        var form = document.createElement('table');
        form.className = 'aa-edit-form';
        form.appendChild(createRow('titleicon.png', 'Title', list.title, 'title').row);
        form.appendChild(createRow('screenicon.png', 'Screen Image', list.screen, 'screen').row);
        content.appendChild(form);

        var info = document.createElement('p');
        info.style.cssText = 'color:#888; font-size:12px; margin:12px 0 0;';
        info.textContent = list.entries ? (list.entries.length + ' entries in this list.') : '';
        content.appendChild(info);
    }

    function onDeleteTab(content) {
        content.innerHTML = '';

        if (!list) {
            content.innerHTML = '<p style="color:#888; padding:16px;">Nothing to delete.</p>';
            return;
        }

        var warn = document.createElement('p');
        warn.style.cssText = 'color:#e8a735; padding:8px 0;';
        warn.textContent = 'Delete the favorites list "' + list.title + '"? Its ' + (list.entries ? list.entries.length : 0) + ' entries will be removed. This cannot be undone.';
        content.appendChild(warn);

        var status = document.createElement('p');
        status.style.cssText = 'color:#aaa; font-size:12px; margin:0 0 12px;';
        content.appendChild(status);

        var deleteBtn = document.createElement('button');
        deleteBtn.className = 'aa-btn';
        deleteBtn.style.cssText = 'background:#8a2020; color:#fff;';
        deleteBtn.textContent = 'Delete Favorites List';
        var confirmStep = false;
        deleteBtn.addEventListener('click', function() {
            if (!confirmStep) {
                confirmStep = true;
                deleteBtn.textContent = 'Click again to confirm delete';
                status.textContent = 'Click the button once more to confirm.';
                return;
            }
            try {
                var ok = arcadeHud.favorites.deleteFavoritesList(listId);
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
        title: list ? ('Favorites List: ' + list.title) : 'Favorites List',
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
