/*
 * mainMenu.js — JS bridge for the AArcade Core main menu
 *
 * The C++ side exposes an `aacore` object on the global window with:
 *   aacore.closeMenu() — closes the main menu
 */

function onCloseMenu() {
    if (window.aacore && aacore.closeMenu) {
        aacore.closeMenu();
    }
}
