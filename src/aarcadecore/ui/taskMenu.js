/* taskMenu.js — Standalone Task Menu page */

function initTaskMenu() {
    var content = arcadeHud.ui.createWindow({
        title: 'Active Tasks',
        showBack: true,
        showClose: true,
        onBack: function() { if (window.aapi && aapi.manager) aapi.manager.openMainMenu(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); }
    });

    arcadeHud.ui.renderTaskList(content);
}
