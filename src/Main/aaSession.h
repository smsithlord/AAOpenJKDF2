#ifndef _AASESSION_H
#define _AASESSION_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum aaSessionMode
{
    SESSION_MODE_NONE  = 0,
    SESSION_MODE_SP    = 1,
    SESSION_MODE_DEBUG = 2,
    SESSION_MODE_MP    = 3,
} aaSessionMode;

extern aaSessionMode aaSession_currentMode;
extern int           aaSession_pendingMpHosting;
extern int           aaSession_bResumed;
extern char          aaSession_resumeShortName[32];

// Writes openjkdf2_lastsession.json based on current globals
// (jkRes_episodeGobName, jkMain_aLevelJklFname, jkPlayer_playerShortName,
// aaSession_currentMode, and, for MP, jkGuiNetHost_* + jkGuiMultiplayer_mpcInfo).
void aaSession_SaveCurrent(void);

// Reads openjkdf2_lastsession.json (if present) and populates
// Main_bAutostart / Main_bAutostartSp / Main_bDevMode / Main_strEpisode /
// Main_strMap / jkGuiNetHost_* / jkGuiMultiplayer_mpcInfo so that the
// existing Main_StartupDedicated path drives the user straight back in.
// Returns 1 if the session file was loaded and applied, 0 otherwise.
int aaSession_LoadAndApply(void);

// After the world is initialized and the local player thing is spawned,
// teleport the player to the position/orientation/eyePYR/sector captured at
// exit. Consumed once per resume — subsequent level loads get normal spawn.
void aaSession_ApplyPendingPosition(void);

#ifdef __cplusplus
}
#endif

#endif // _AASESSION_H
