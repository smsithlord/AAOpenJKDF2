/* taskMenu.js — Task Menu for managing active embedded instances */

var taskListEl = null;

function initTaskMenu() {
    var content = arcadeHud.ui.createWindow({
        title: 'Active Tasks',
        showBack: true,
        showClose: true,
        onBack: function() { if (window.aapi && aapi.manager) aapi.manager.openMainMenu(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); }
    });

    taskListEl = document.createElement('div');
    content.appendChild(taskListEl);
    refreshList();
}

function refreshList() {
    if (!taskListEl) return;

    if (!window.aapi || !aapi.manager || !aapi.manager.getActiveInstances) {
        taskListEl.innerHTML = '<div class="aa-empty-message">API not available</div>';
        return;
    }

    var instances = aapi.manager.getActiveInstances();
    if (!instances || instances.length === 0) {
        taskListEl.innerHTML = '<div class="aa-empty-message">No active instances</div>';
        return;
    }

    var html = '';
    for (var i = 0; i < instances.length; i++) {
        var inst = instances[i];
        html += '<div class="aa-task-item">';
        html += '  <div class="aa-task-info">';
        html += '    <div class="aa-task-title">' + arcadeHud.ui.escapeHtml(inst.title || inst.itemId) + '</div>';
        html += '    <div class="aa-task-url">' + arcadeHud.ui.escapeHtml(inst.url || '') + '</div>';
        html += '  </div>';
        html += '  <button class="aa-task-close" onclick="onDeactivate(\'' + arcadeHud.ui.escapeHtml(inst.itemId) + '\')">Close</button>';
        html += '</div>';
    }
    taskListEl.innerHTML = html;
}

function onDeactivate(itemId) {
    if (window.aapi && aapi.manager && aapi.manager.deactivateInstance) {
        aapi.manager.deactivateInstance(itemId);
        refreshList();
    }
}
