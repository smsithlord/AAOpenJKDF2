#include "aaMainMenu.h"

#include "types.h"
#include "globals.h"
#include "stdPlatform.h"
#include "Platform/Common/AACoreManager.h"
#include "Devices/sithControl.h"
#include "Main/jkMain.h"

#include <SDL.h>

static int g_escKeyWasDown = 0;
static int g_hKeyWasDown = 0;
static int g_tabMenuWasDown = 0;
static int g_tasksTabWasDown = 0;
static int g_libraryTabWasDown = 0;
static int g_buildWasDown = 0;
static int g_selectWasDown = 0;

void aaMainMenu_Update(void)
{
    /* Ensure AA keybinds exist (repairs after config reload/cancel) */
    sithControl_EnsureAADefaults();

    const Uint8* keys = SDL_GetKeyboardState(NULL);

    /* Escape: exit fullscreen if active, otherwise toggle the AArcade main menu */
    int escDown = keys[SDL_SCANCODE_ESCAPE];
    if (escDown && !g_escKeyWasDown) {
        if (AACoreManager_IsFullscreenActive())
            AACoreManager_ExitFullscreen();
        else
            AACoreManager_ToggleMainMenu();
    }
    g_escKeyWasDown = escDown;

    /* Select (LMB): activate the aimed-at AArcade object — uses raw SDL, not sithControl */
    {
        int lmbDown = SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT);
        if (lmbDown && !g_selectWasDown) {
            int aimedIdx = AACoreManager_GetAimedThingIdx();
            if (aimedIdx >= 0 && !AACoreManager_IsFullscreenActive() && !AACoreManager_IsMainMenuOpen())
                AACoreManager_ObjectUsed(aimedIdx);
        }
        g_selectWasDown = lmbDown;
    }

    /* Build (middle mouse default): toggle build context menu */
    int buildDown = 0;
    sithControl_ReadFunctionMap(INPUT_FUNC_AABUILD, &buildDown);
    if (buildDown && !g_buildWasDown) {
        if (!AACoreManager_IsFullscreenActive() && !AACoreManager_IsMainMenuOpen())
            AACoreManager_ToggleBuildContextMenu();
    }
    g_buildWasDown = buildDown;

    /* Tasks Tab (F4 default): open tab menu to Tasks tab */
    int tasksTabDown = 0;
    sithControl_ReadFunctionMap(INPUT_FUNC_AATASKSTAB, &tasksTabDown);
    if (tasksTabDown && !g_tasksTabWasDown) {
        if (!AACoreManager_IsFullscreenActive())
            AACoreManager_OpenTabMenuToTab(0);
    }
    g_tasksTabWasDown = tasksTabDown;

    /* Library Tab (F6 default): open tab menu to Library tab */
    int libraryTabDown = 0;
    sithControl_ReadFunctionMap(INPUT_FUNC_AALIBRARYTAB, &libraryTabDown);
    if (libraryTabDown && !g_libraryTabWasDown) {
        if (!AACoreManager_IsFullscreenActive())
            AACoreManager_OpenTabMenuToTab(1);
    }
    g_libraryTabWasDown = libraryTabDown;

    /* Tab Menu (TAB default): hold to show, release to close.
     * Uses raw SDL key state because sithControl is suppressed when menu is open. */
    int tabDown = keys[SDL_SCANCODE_TAB];
    if (tabDown && !g_tabMenuWasDown) {
        if (!AACoreManager_IsFullscreenActive() && !AACoreManager_IsMainMenuOpen())
            AACoreManager_OpenTabMenuToTab(-1);
    } else if (!tabDown && g_tabMenuWasDown) {
        if (AACoreManager_IsMainMenuOpen())
            AACoreManager_ToggleMainMenu();
    }
    g_tabMenuWasDown = tabDown;

    /* Check if DLL requested opening the engine menu */
    if (AACoreManager_ShouldOpenEngineMenu()) {
        AACoreManager_ClearEngineMenuFlag();
        if (AACoreManager_IsMainMenuOpen())
            AACoreManager_ToggleMainMenu();
        jkMain_do_guistate6();
    }

    /* Check if DLL requested starting Libretro */
    if (AACoreManager_ShouldStartLibretro()) {
        AACoreManager_ClearStartLibretroFlag();
        AACoreManager_StartLibretro();
    }
}
