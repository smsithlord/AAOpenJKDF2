/* mainMenu.js — AArcade Main Menu */

function initMainMenu() {
    var content = arcadeHud.ui.createWindow({
        title: 'AArcade',
        showClose: true,
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); }
    });

    content.innerHTML =
        '<button class="aa-btn" helpText="Central control hub with tabbed interface." onclick="onTabMenu()">Tab Menu</button>' +
        '<button class="aa-btn" helpText="Browse and spawn items from the media library." onclick="onLibraryBrowser()">Library Browser</button>' +
        '<button class="aa-btn" helpText="View and manage active embedded instances." onclick="onTaskMenu()">Task Menu</button>' +
        '<button class="aa-btn" helpText="Start a test Libretro emulator instance." onclick="onTestLibretro()">Test Libretro</button>' +
        '<button class="aa-btn" helpText="Open the engine\'s built-in main menu." onclick="onEngineMenu()">Engine Main Menu</button>';
}

function onTabMenu() {
    if (window.aapi && aapi.manager) aapi.manager.openTabMenu();
}

function onLibraryBrowser() {
    if (window.aapi && aapi.manager) aapi.manager.openLibraryBrowser();
}

function onTaskMenu() {
    if (window.aapi && aapi.manager) aapi.manager.openTaskMenu();
}

function onTestLibretro() {
    if (window.aapi && aapi.manager) aapi.manager.startLibretro();
}

function onEngineMenu() {
    if (window.aapi && aapi.manager) aapi.manager.openEngineMenu();
}
