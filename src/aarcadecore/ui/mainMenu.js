/*
 * mainMenu.js — JS bridge for the AArcade Core main menu
 *
 * The C++ side exposes an `aacore` object on the global window with:
 *   aacore.call("command")      — fire-and-forget (async)
 *   aacore.callSync("command")  — returns a value (sync)
 */

function onCloseMenu() {
    if (window.aacore) aacore.call("closeMenu");
}

function onEngineMenu() {
    if (window.aacore) aacore.call("openEngineMenu");
}

function onTestLibretro() {
    if (window.aacore) aacore.call("startLibretro");
}
