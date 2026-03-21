/*
 * mainMenu.js — JS bridge for the AArcade Core main menu
 *
 * Uses the unified aapi bridge:
 *   aapi.manager.closeMenu()
 *   aapi.manager.openEngineMenu()
 *   aapi.manager.startLibretro()
 *   aapi.manager.openLibraryBrowser()
 */

function onCloseMenu() {
    if (window.aapi) aapi.manager.closeMenu();
}

function onEngineMenu() {
    if (window.aapi) aapi.manager.openEngineMenu();
}

function onLibraryBrowser() {
    if (window.aapi) aapi.manager.openLibraryBrowser();
}

function onTaskMenu() {
    if (window.aapi) aapi.manager.openTaskMenu();
}

function onTestLibretro() {
    if (window.aapi) aapi.manager.startLibretro();
}
