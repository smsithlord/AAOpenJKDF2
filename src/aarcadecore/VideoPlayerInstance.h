#ifndef VIDEO_PLAYER_INSTANCE_H
#define VIDEO_PLAYER_INSTANCE_H

#include "aarcadecore_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create a video player instance that plays the given file on the named material.
 * Returns NULL on allocation failure. Call vtable->init() to start playback. */
EmbeddedInstance* VideoPlayerInstance_Create(const char* file_path, const char* material_name);
void VideoPlayerInstance_Destroy(EmbeddedInstance* inst);

/* Get playback position and duration in seconds. Returns false if not a video player. */
bool VideoPlayerInstance_GetTimeInfo(EmbeddedInstance* inst, double* posOut, double* durOut);

/* Seek to absolute position in seconds. */
void VideoPlayerInstance_Seek(EmbeddedInstance* inst, double position);

#ifdef __cplusplus
}
#endif

#endif // VIDEO_PLAYER_INSTANCE_H
