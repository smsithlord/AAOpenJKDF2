/* buildContextMenu.js — Build Context Menu for aimed-at AArcade objects */

function initBuildContextMenu() {
    var content = arcadeHud.ui.createWindow({
        title: 'Build',
        showClose: true,
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); }
    });

    var info = null;
    if (window.aapi && aapi.manager && aapi.manager.getAimedObjectInfo) {
        info = aapi.manager.getAimedObjectInfo();
    }

    if (!info) {
        content.innerHTML = '<div class="aa-empty-message">No object aimed at.</div>';
        return;
    }

    var title = info.title || info.itemId || 'Unknown';
    content.innerHTML =
        '<div class="aa-object-title">' + arcadeHud.ui.escapeHtml(title) + '</div>' +
        '<div class="aa-object-info">' + arcadeHud.ui.escapeHtml(info.url || '') + '</div>' +
        '<button class="aa-btn" helpText="Move this object to a new position." onclick="onMoveObject()">Move Object</button>' +
        '<button class="aa-btn aa-btn-danger" helpText="Permanently remove this object from the instance." onclick="onDestroyObject()">Destroy Object</button>';
}

function onMoveObject() {
    /* TODO: implement move mode */
    if (window.aapi && aapi.manager) aapi.manager.closeMenu();
}

function onDestroyObject() {
    if (window.aapi && aapi.manager && aapi.manager.destroyAimedObject) {
        aapi.manager.destroyAimedObject();
    }
    if (window.aapi && aapi.manager) aapi.manager.closeMenu();
}
