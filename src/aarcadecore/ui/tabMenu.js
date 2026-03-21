/* tabMenu.js — Central control hub with tabbed interface */

function initTabMenu() {
    arcadeHud.ui.createWindow({
        title: 'AArcade',
        showBack: true,
        showClose: true,
        onBack: function() { if (window.aapi && aapi.manager) aapi.manager.openMainMenu(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); },
        tabPosition: 'bottom',
        tabs: [
            { label: 'Tasks', onActivate: onTasksTab },
            { label: 'Library', onActivate: onLibraryTab }
        ]
    });
}

function onTasksTab(contentEl) {
    arcadeHud.ui.renderTaskList(contentEl);
}

function onLibraryTab(contentEl) {
    arcadeHud.ui.renderLibrary(contentEl);
}
