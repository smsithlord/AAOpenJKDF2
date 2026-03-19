#ifndef EMBEDDED_INSTANCE_MANAGER_H
#define EMBEDDED_INSTANCE_MANAGER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the embedded instance system.
 * By default, creates and activates a LibretroInstance. */
void EmbeddedInstanceManager_Init(void);

/* Shutdown all embedded instances. */
void EmbeddedInstanceManager_Shutdown(void);

/* Update the active embedded instance (call once per frame). */
void EmbeddedInstanceManager_Update(void);

/* Check if any embedded instance is active. */
bool EmbeddedInstanceManager_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif /* EMBEDDED_INSTANCE_MANAGER_H */
