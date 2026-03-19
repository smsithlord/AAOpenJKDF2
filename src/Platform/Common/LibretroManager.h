#ifndef LIBRETRO_MANAGER_H
#define LIBRETRO_MANAGER_H

#include "EmbeddedInstance.h"

#ifdef __cplusplus
extern "C" {
#endif

void LibretroManager_Init(void);
void LibretroManager_Shutdown(void);
void LibretroManager_Update(void);
EmbeddedInstance* LibretroManager_GetActive(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBRETRO_MANAGER_H */
