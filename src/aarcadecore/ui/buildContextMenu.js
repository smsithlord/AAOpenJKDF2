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

    var isSlave = false;
    if (window.aapi && aapi.manager && aapi.manager.isAimedObjectSlave)
        isSlave = aapi.manager.isAimedObjectSlave();

    var title = info.title || info.itemId || 'Unknown';
    content.innerHTML =
        '<div class="aa-object-title">' + arcadeHud.ui.escapeHtml(title) + '</div>' +
        '<div class="aa-object-info">' + arcadeHud.ui.escapeHtml(info.url || '') + '</div>' +
        '<button class="aa-btn" onclick="onMoveObject()">Move Object</button>' +
        '<button class="aa-btn" id="mirrorBtn" onclick="onToggleMirror()">' + (isSlave ? 'Disable Mirror' : 'Enable Mirror') + '</button>' +
        '<button class="aa-btn aa-btn-danger" onclick="onDestroyObject()">Destroy Object</button>';
}

function onMoveObject() {
    if (window.aapi && aapi.manager) {
        aapi.manager.moveAimedObject();
        aapi.manager.closeMenu();
    }
}

function onToggleMirror() {
    if (window.aapi && aapi.manager && aapi.manager.toggleSlaveAimedObject) {
        var newState = aapi.manager.toggleSlaveAimedObject();
        var btn = document.getElementById('mirrorBtn');
        if (btn) btn.textContent = newState ? 'Disable Mirror' : 'Enable Mirror';
    }
}

function onDestroyObject() {
    if (window.aapi && aapi.manager && aapi.manager.destroyAimedObject) {
        aapi.manager.destroyAimedObject();
    }
    if (window.aapi && aapi.manager) aapi.manager.closeMenu();
}
