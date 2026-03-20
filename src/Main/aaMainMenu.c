#include "aaMainMenu.h"

#include "types.h"
#include "globals.h"
#include "stdPlatform.h"
#include "Platform/stdControl.h"
#include "Platform/Common/AACoreManager.h"

static int g_gKeyWasDown = 0;

void aaMainMenu_Update(void)
{
    int gDown = 0;

    stdControl_ReadKey(DIK_G, &gDown);

    /* Debounce: only trigger on key-down edge */
    if (!gDown || g_gKeyWasDown) {
        g_gKeyWasDown = gDown;
        return;
    }
    g_gKeyWasDown = 1;

    stdPlatform_Printf("aaMainMenu: G pressed, toggling main menu\n");
    AACoreManager_ToggleMainMenu();
}
