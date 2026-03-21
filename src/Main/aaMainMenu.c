#include "aaMainMenu.h"

#include "types.h"
#include "globals.h"
#include "stdPlatform.h"
#include "Platform/Common/AACoreManager.h"
#include "Main/jkMain.h"

#include <SDL.h>

static int g_escKeyWasDown = 0;

void aaMainMenu_Update(void)
{
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
