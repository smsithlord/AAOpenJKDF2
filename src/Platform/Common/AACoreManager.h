#ifndef AACORE_MANAGER_H
#define AACORE_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Load aarcadecore.dll and initialize it with host callbacks */
void AACoreManager_Init(void);

/* Shutdown and unload the DLL */
void AACoreManager_Shutdown(void);

/* Update the DLL (call once per frame) */
void AACoreManager_Update(void);

/* Check if any embedded instance is active */
bool AACoreManager_IsActive(void);

/* Forward keyboard events to the DLL */
void AACoreManager_KeyDown(int vk_code, int modifiers);
void AACoreManager_KeyUp(int vk_code, int modifiers);
void AACoreManager_KeyChar(unsigned int unicode_char, int modifiers);

/* Mouse input */
void AACoreManager_MouseMove(int x, int y);
void AACoreManager_MouseDown(int button);
void AACoreManager_MouseUp(int button);
void AACoreManager_MouseWheel(int delta);

/* Main menu */
void AACoreManager_ToggleMainMenu(void);

/* Draw the fullscreen overlay (call from render path, after 3D scene) */
void AACoreManager_DrawOverlay(int screenWidth, int screenHeight);

#ifdef __cplusplus
}
#endif

#endif /* AACORE_MANAGER_H */
