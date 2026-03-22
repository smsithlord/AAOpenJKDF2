/* tabMenu.js — Central control hub with tabbed interface */

function initTabMenu() {
    /* Check if C++ requested a specific tab (e.g. from F4/F6 keybind) */
    var requestedTab = null;
    if (window.aapi && aapi.manager && aapi.manager.getRequestedTab)
        requestedTab = aapi.manager.getRequestedTab();

    arcadeHud.ui.createWindow({
        title: 'AArcade',
        showBack: true,
        showClose: true,
        onBack: function() { if (window.aapi && aapi.manager) aapi.manager.openMainMenu(); },
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); },
        tabPosition: 'bottom',
        tabStorageKey: 'tabMenuActiveTab',
        tabOverride: (requestedTab !== null && requestedTab !== undefined) ? requestedTab : -1,
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
