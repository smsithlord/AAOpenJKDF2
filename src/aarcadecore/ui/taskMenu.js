/*
 * taskMenu.js — Task Menu for managing active embedded instances
 */

function refreshList() {
    var listEl = document.getElementById('taskList');
    var subtitleEl = document.getElementById('subtitle');

    if (!window.aapi || !aapi.manager || !aapi.manager.getActiveInstances) {
        listEl.innerHTML = '<div class="empty-msg">API not available</div>';
        return;
    }

    var instances = aapi.manager.getActiveInstances();
    if (!instances || instances.length === 0) {
        listEl.innerHTML = '<div class="empty-msg">No active instances</div>';
        subtitleEl.textContent = '0 Active Instances';
        return;
    }

    subtitleEl.textContent = instances.length + ' Active Instance' + (instances.length !== 1 ? 's' : '');

    var html = '';
    for (var i = 0; i < instances.length; i++) {
        var inst = instances[i];
        var displayTitle = inst.title || inst.url || inst.itemId;
        html += '<div class="task-item">';
        html += '  <div class="task-info">';
        html += '    <div class="task-item-id">' + (inst.title ? inst.title : inst.itemId) + '</div>';
        html += '    <div class="task-url" title="' + inst.url + '">' + inst.url + '</div>';
        html += '  </div>';
        html += '  <button class="close-btn" onclick="onDeactivate(\'' + inst.itemId + '\')">Close</button>';
        html += '</div>';
    }
    listEl.innerHTML = html;
}

function onDeactivate(itemId) {
    if (window.aapi && aapi.manager && aapi.manager.deactivateInstance) {
        aapi.manager.deactivateInstance(itemId);
        refreshList();
    }
}

function onBack() {
    if (window.aapi && aapi.manager) {
        aapi.manager.openMainMenu();
    }
}
