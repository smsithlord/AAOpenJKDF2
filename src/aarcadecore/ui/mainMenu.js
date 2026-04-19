/* mainMenu.js — AArcade Main Menu */

function initMainMenu() {
    var content = arcadeHud.ui.createWindow({
        title: 'AArcade',
        showClose: true,
        onClose: function() { if (window.aapi && aapi.manager) aapi.manager.closeMenu(); }
    });

    content.innerHTML =
        '<button class="aa-btn" helpText="Close this menu and return to the game." onclick="onResume()">Resume</button>' +
        '<button class="aa-btn" helpText="Configure settings and import library data." onclick="onOptions()">Options</button>' +
        '<button class="aa-btn" helpText="Open the engine\'s built-in main menu." onclick="onEngineMenu()">Engine Main Menu</button>' +
        '<button class="aa-btn" helpText="Pause the engine and idle at minimal CPU usage." onclick="onPause()">Pause</button>' +
        '<button class="aa-btn aa-btn-danger" helpText="Close OpenJK and exit to the desktop." onclick="onExitGame()">Exit Game</button>';
}

function onResume() {
    if (window.aapi && aapi.manager) aapi.manager.closeMenu();
}

function onOptions() {
    window.location.href = 'file:///ui/options.html';
}

function onEngineMenu() {
    if (window.aapi && aapi.manager) aapi.manager.openEngineMenu();
}

function onPause() {
    window.location.href = 'file:///ui/pause.html';
}

function onExitGame() {
    if (window.aapi && aapi.manager && aapi.manager.exitGame) aapi.manager.exitGame();
}
