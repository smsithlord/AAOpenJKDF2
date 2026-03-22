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
bool AACoreManager_IsMainMenuOpen(void);
bool AACoreManager_ShouldOpenEngineMenu(void);
void AACoreManager_ClearEngineMenuFlag(void);
bool AACoreManager_ShouldStartLibretro(void);
void AACoreManager_ClearStartLibretroFlag(void);
void AACoreManager_StartLibretro(void);

/* Draw the fullscreen overlay (call from render path, after 3D scene) */
void AACoreManager_DrawOverlay(int screenWidth, int screenHeight);

/* Register a spawned thing to show a task on its compscreen material */
void AACoreManager_RegisterThingTask(void* pSithThing, int thingIdx, int taskIndex);

/* Map lifecycle notifications */
void AACoreManager_OnMapLoaded(void);
void AACoreManager_OnMapUnloaded(void);

/* Notify DLL that player "used" (activated) an AArcade object */
void AACoreManager_ObjectUsed(int thingIdx);

/* Remember: deselect without closing instance, or activate aimed-at object without selecting */
void AACoreManager_RememberObject(void);

/* Input mode — send input to in-world instance without fullscreen overlay */
void AACoreManager_EnterInputMode(void);
void AACoreManager_ExitInputMode(void);
bool AACoreManager_IsInputModeActive(void);

/* Selector ray — get which AArcade thing the player is aiming at */
int AACoreManager_GetAimedThingIdx(void);

/* Spawn mode */
bool AACoreManager_IsSpawnModeActive(void);
void AACoreManager_ConfirmSpawn(void);
void AACoreManager_CancelSpawn(void);

/* Tab menu — open to specific tab */
void AACoreManager_OpenTabMenuToTab(int tabIndex);

/* Build context menu */
void AACoreManager_ToggleBuildContextMenu(void);

/* Fullscreen overlay mode for embedded instances */
bool AACoreManager_IsFullscreenActive(void);
void AACoreManager_ExitFullscreen(void);

/* Per-thing render hooks — swap cloned texture before/after rdThing_Draw */
void AACoreManager_PreRenderThing(void* pSithThing);
void AACoreManager_PostRenderThing(void* pSithThing);

#ifdef __cplusplus
}
#endif

#endif /* AACORE_MANAGER_H */
