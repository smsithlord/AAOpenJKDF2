#include "aaMainMenu.h"

#include "types.h"
#include "globals.h"
#include "stdPlatform.h"
#include "Platform/Common/AACoreManager.h"

#include <SDL.h>

static int g_gKeyWasDown = 0;
static int g_escKeyWasDown = 0;

void aaMainMenu_Update(void)
{
    /* Use SDL_GetKeyboardState directly so G and Escape work even when
     * the game's stdControl polling is suppressed during input mode */
    const Uint8* keys = SDL_GetKeyboardState(NULL);

    /* G key toggles menu */
    int gDown = keys[SDL_SCANCODE_G];
    if (gDown && !g_gKeyWasDown) {
        stdPlatform_Printf("aaMainMenu: G pressed, toggling main menu\n");
        AACoreManager_ToggleMainMenu();
    }
    g_gKeyWasDown = gDown;

    /* Escape closes menu when in main menu mode */
    int escDown = keys[SDL_SCANCODE_ESCAPE];
    if (escDown && !g_escKeyWasDown && AACoreManager_IsActive()) {
        stdPlatform_Printf("aaMainMenu: Escape pressed, closing main menu\n");
        AACoreManager_ToggleMainMenu();
    }
    g_escKeyWasDown = escDown;
}
