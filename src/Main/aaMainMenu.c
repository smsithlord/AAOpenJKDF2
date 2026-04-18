#include "aaMainMenu.h"

#include "types.h"
#include "globals.h"
#include "stdPlatform.h"
#include "Platform/Common/AACoreManager.h"
#include "Devices/sithControl.h"
#include "Gameplay/sithInventory.h"
#include "Gameplay/sithPlayer.h"
#include "Main/jkMain.h"

#include <SDL.h>

static int g_escKeyWasDown = 0;
static int g_hKeyWasDown = 0;
static int g_tabMenuWasDown = 0;
static int g_tasksTabWasDown = 0;
static int g_libraryTabWasDown = 0;
static int g_buildWasDown = 0;
static int g_selectWasDown = 0;
static int g_rememberWasDown = 0;
static int g_virtualInputWasDown = 0;
static int g_inputLockWasDown = 0;

void aaMainMenu_Update(void)
{
    /* Ensure AA keybinds exist (repairs after config reload/cancel) */
    sithControl_EnsureAADefaults();

    const Uint8* keys = SDL_GetKeyboardState(NULL);

    /* AArcade keybinds only active when fists are equipped AND alive, unless already in an AA mode.
     * When dead (health <= 0) with fists, allow fire-to-respawn by disabling AA hotkeys. */
    int fistsOut = 0;
    int alive = 0;
    if (sithPlayer_pLocalPlayerThing && sithPlayer_pLocalPlayerThing->actorParams.playerinfo) {
        fistsOut = (sithInventory_GetCurWeapon(sithPlayer_pLocalPlayerThing) == SITHBIN_FISTS);
        alive = (sithPlayer_pLocalPlayerThing->actorParams.health > 0.0);
    }

    int aaActive = fistsOut && alive;

    int alreadyInAAMode = AACoreManager_IsSpawnModeActive() || AACoreManager_IsInputModeActive()
                        || AACoreManager_IsMainMenuOpen() || AACoreManager_IsFullscreenActive();

    /* Suppress FIRE1/FIRE2 when fists are out and alive so LMB select doesn't punch */
    AACoreManager_SetSuppressFire(aaActive);

    if (!aaActive && !alreadyInAAMode) {
        /* Update edge-trigger state trackers to prevent false triggers on weapon switch */
        g_escKeyWasDown = keys[SDL_SCANCODE_ESCAPE];
        g_selectWasDown = SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT);
        g_virtualInputWasDown = SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_RIGHT);
        g_inputLockWasDown = keys[SDL_SCANCODE_G];
        g_tabMenuWasDown = keys[SDL_SCANCODE_TAB];
        g_buildWasDown = SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_MIDDLE);
        g_rememberWasDown = keys[SDL_SCANCODE_R];
        g_tasksTabWasDown = keys[SDL_SCANCODE_F4];
        g_libraryTabWasDown = keys[SDL_SCANCODE_F6];
        return;
    }

    /* Spawn mode takes priority over all other input */
    if (AACoreManager_IsSpawnModeActive()) {
        int escDown = keys[SDL_SCANCODE_ESCAPE];
        if (escDown && !g_escKeyWasDown) {
            if (AACoreManager_IsInputModeActive())
                AACoreManager_ExitInputMode();
            else
                AACoreManager_CancelSpawn();
        }
        g_escKeyWasDown = escDown;

        /* LMB: only confirm spawn when not in input mode (avoid accidental confirm while using sliders) */
        int lmbDown = SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT);
        if (lmbDown && !g_selectWasDown && !AACoreManager_IsInputModeActive())
            AACoreManager_ConfirmSpawn();
        g_selectWasDown = lmbDown;

        /* RMB: virtual input for transform panel */
        {
            int rmbDown = SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_RIGHT);
            if (rmbDown && !g_virtualInputWasDown) {
                if (!AACoreManager_IsInputModeActive())
                    AACoreManager_EnterInputMode();
            } else if (!rmbDown && g_virtualInputWasDown) {
                if (AACoreManager_IsInputModeActive())
                    AACoreManager_ExitInputMode();
            }
            g_virtualInputWasDown = rmbDown;
        }

        return; /* Skip all other AA input while in spawn mode */
    }

    /* Escape: exit input mode → exit fullscreen → toggle main menu */
    int escDown = keys[SDL_SCANCODE_ESCAPE];
    if (escDown && !g_escKeyWasDown) {
        if (AACoreManager_IsInputModeActive())
            AACoreManager_ExitInputMode();
        else if (AACoreManager_IsFullscreenActive())
            AACoreManager_ExitFullscreen();
        else
            AACoreManager_ToggleMainMenu();
    }
    g_escKeyWasDown = escDown;

    /* Select (LMB): activate aimed-at object, or deselect if aiming at nothing */
    {
        int lmbDown = SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT);
        if (lmbDown && !g_selectWasDown && !AACoreManager_IsFullscreenActive() && !AACoreManager_IsMainMenuOpen()) {
            int aimedIdx = AACoreManager_GetAimedThingIdx();
            AACoreManager_ObjectUsed(aimedIdx); /* -1 = deselect current */
        }
        g_selectWasDown = lmbDown;
    }

    /* Virtual Input (RMB hold): send input to in-world instance while held.
     * Uses raw SDL because sithControl is suppressed when input mode is active. */
    {
        int rmbDown = SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_RIGHT);
        if (rmbDown && !g_virtualInputWasDown) {
            if (!AACoreManager_IsInputModeActive() && !AACoreManager_IsFullscreenActive()
                && (!AACoreManager_IsMainMenuOpen() || AACoreManager_IsSpawnModeActive()))
                AACoreManager_EnterInputMode();
        } else if (!rmbDown && g_virtualInputWasDown) {
            if (AACoreManager_IsInputModeActive())
                AACoreManager_ExitInputMode();
        }
        g_virtualInputWasDown = rmbDown;
    }

    /* Input Lock (G default): toggle input mode on/off */
    {
        int lockDown = keys[SDL_SCANCODE_G];
        if (lockDown && !g_inputLockWasDown) {
            if (AACoreManager_IsInputModeActive())
                AACoreManager_ExitInputMode();
            else if (!AACoreManager_IsFullscreenActive() && !AACoreManager_IsMainMenuOpen())
                AACoreManager_EnterInputMode();
        }
        g_inputLockWasDown = lockDown;
    }

    /* Build (middle mouse): toggle build context menu */
    {
        int buildDown = SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_MIDDLE);
        if (buildDown && !g_buildWasDown) {
            if (!AACoreManager_IsFullscreenActive() && !AACoreManager_IsMainMenuOpen())
                AACoreManager_ToggleBuildContextMenu();
        }
        g_buildWasDown = buildDown;
    }

    /* Remember (R): deselect-only or activate without selecting */
    {
        int rememberDown = keys[SDL_SCANCODE_R];
        if (rememberDown && !g_rememberWasDown) {
            if (!AACoreManager_IsFullscreenActive() && !AACoreManager_IsMainMenuOpen())
                AACoreManager_RememberObject();
        }
        g_rememberWasDown = rememberDown;
    }

    /* Tasks Tab (F4): open tab menu to Tasks tab */
    {
        int tasksTabDown = keys[SDL_SCANCODE_F4];
        if (tasksTabDown && !g_tasksTabWasDown) {
            if (!AACoreManager_IsFullscreenActive())
                AACoreManager_OpenTabMenuToTab(0);
        }
        g_tasksTabWasDown = tasksTabDown;
    }

    /* Library Tab (F6): open tab menu to Library tab */
    {
        int libraryTabDown = keys[SDL_SCANCODE_F6];
        if (libraryTabDown && !g_libraryTabWasDown) {
            if (!AACoreManager_IsFullscreenActive())
                AACoreManager_OpenTabMenuToTab(1);
        }
        g_libraryTabWasDown = libraryTabDown;
    }

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
