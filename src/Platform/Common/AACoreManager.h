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

#ifdef __cplusplus
}
#endif

#endif /* AACORE_MANAGER_H */
