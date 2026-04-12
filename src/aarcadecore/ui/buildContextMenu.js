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
        content.innerHTML =
            '<input type="text" class="aa-edit-row-input" id="pasteUrlInput" placeholder="Paste file or URL to spawn here..." style="width:100%; margin-top:12px;">' +
            '<button class="aa-btn" style="margin-top:8px;" onclick="onPasteUrlSubmit()">Create Item</button>';
        var input = content.querySelector('#pasteUrlInput');
        if (input) {
            input.addEventListener('keydown', function(e) {
                if (e.key === 'Enter') { e.preventDefault(); onPasteUrlSubmit(); }
            });
            input.focus();
        }
        return;
    }

    var isSlave = false;
    if (window.aapi && aapi.manager && aapi.manager.isAimedObjectSlave)
        isSlave = aapi.manager.isAimedObjectSlave();

    var isModel = !info.itemId && info.modelId;

    /* Resolve title and info line */
    var title = info.title || info.itemId || info.modelId || 'Unknown';
    var infoLine = (info.item && info.item.file) || '';
    if (isModel && info.modelId && window.aapi && aapi.library) {
        var model = aapi.library.getModelById(info.modelId);
        if (model && model.title) title = model.title;
        var platformFile = aapi.library.getModelPlatformFile(info.modelId);
        if (platformFile) infoLine = platformFile;
    }

    var iconStyle = 'width:24px; height:24px; vertical-align:middle;';
    var btnStyle = 'display:inline-block; width:auto; padding:6px 8px; margin:2px;';
    function iconBtn(icon, helpText, onclick, extraClass) {
        return '<button class="aa-btn' + (extraClass ? ' ' + extraClass : '') + '" style="' + btnStyle + '" onclick="' + onclick + '" helpText="' + helpText + '"><img src="icons/' + icon + '" style="' + iconStyle + '"></button>';
    }

    var html =
        '<div class="aa-object-title">' + arcadeHud.ui.escapeHtml(title) + '</div>' +
        '<div class="aa-object-info">' + arcadeHud.ui.escapeHtml(infoLine) + '</div>' +
        '<div style="display:flex; flex-wrap:wrap; gap:4px; margin-top:8px;">' +
        iconBtn('moveicon.png', 'Move Object', 'onMoveObject()');

    if (info.itemId) {
        html += iconBtn('itemicon.png', 'Edit Item', 'onEditItem()');
        html += iconBtn('itemicon.png', 'Launch', 'onLaunchItem()');
        html += iconBtn('photoicon.png', 'Refresh Texture', 'onRefreshTexture()');
    }

    if (isModel) {
        html += iconBtn('photoicon.png', 'Capture Thumbnail', 'onCaptureThumbnail()');
    }

    html += iconBtn('cloneicon.png', 'Clone Object', 'onCloneObject()');

    if (!isModel) {
        var mirrorIcon = isSlave ? 'unscreenmirror.png' : 'screenmirror.png';
        var mirrorHelp = isSlave ? 'Disable Mirror' : 'Enable Mirror';
        html += '<button class="aa-btn" id="mirrorBtn" style="' + btnStyle + '" onclick="onToggleMirror()" helpText="' + mirrorHelp + '"><img src="icons/' + mirrorIcon + '" style="' + iconStyle + '"></button>';
    }

    html += iconBtn('trashicon.png', 'Destroy Object', 'onDestroyObject()', 'aa-btn-danger');
    html += '</div>';
    content.innerHTML = html;

    /* Store info for button handlers */
    window._buildContextInfo = info;
    window._buildContextModelId = info.modelId || '';
}

function onMoveObject() {
    if (window.aapi && aapi.manager) {
        aapi.manager.moveAimedObject();
        aapi.manager.closeMenu();
    }
}

function onPasteUrlSubmit() {
    var input = document.getElementById('pasteUrlInput');
    var file = input ? input.value.trim() : '';
    if (file) {
        window.location.href = 'file:///aarcadecore/ui/createItem.html?file=' + encodeURIComponent(file);
    }
}

function onRefreshTexture() {
    var info = window._buildContextInfo;
    if (info && info.itemId && window.aapi && aapi.manager && aapi.manager.refreshItemTextures) {
        aapi.manager.refreshItemTextures(info.itemId);
        aapi.manager.closeMenu();
    }
}

function onCloneObject() {
    if (window.aapi && aapi.manager && aapi.manager.cloneAimedObject) {
        aapi.manager.cloneAimedObject();
        aapi.manager.closeMenu();
    }
}

function onToggleMirror() {
    if (window.aapi && aapi.manager && aapi.manager.toggleSlaveAimedObject) {
        var newState = aapi.manager.toggleSlaveAimedObject();
        var btn = document.getElementById('mirrorBtn');
        if (btn) {
            btn.innerHTML = '<img src="icons/' + (newState ? 'unscreenmirror.png' : 'screenmirror.png') + '" style="width:24px; height:24px; vertical-align:middle;">';
            btn.setAttribute('helpText', newState ? 'Disable Mirror' : 'Enable Mirror');
        }
    }
}

function onLaunchItem() {
    var info = window._buildContextInfo;
    if (info && info.itemId && window.aapi && aapi.manager && aapi.manager.launchItem) {
        var ok = aapi.manager.launchItem(info.itemId);
        if (ok)
            window.location.href = 'file:///aarcadecore/ui/pause.html';
        else
            window.location.href = 'file:///aarcadecore/ui/launchFailed.html';
    }
}

function onEditItem() {
    var info = window._buildContextInfo;
    if (info && info.itemId) {
        window.location.href = 'file:///aarcadecore/ui/editItem.html?id=' + encodeURIComponent(info.itemId);
    }
}

function onCaptureThumbnail() {
    var modelId = window._buildContextModelId;
    if (modelId) {
        window.location.href = 'file:///aarcadecore/ui/modelThumbnailMaker.html?modelId=' + encodeURIComponent(modelId);
    }
}

function onDestroyObject() {
    if (window.aapi && aapi.manager && aapi.manager.destroyAimedObject) {
        aapi.manager.destroyAimedObject();
    }
    if (window.aapi && aapi.manager) aapi.manager.closeMenu();
}
