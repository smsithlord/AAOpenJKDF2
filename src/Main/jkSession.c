#include "jkSession.h"

#include "General/stdJSON.h"
#include "General/stdString.h"
#include "stdPlatform.h"
#include "Main/Main.h"
#include "Main/jkRes.h"
#include "Main/jkMain.h"
#include "World/jkPlayer.h"
#include "World/sithThing.h"
#include "Gui/jkGUINetHost.h"

#include <string.h>

#define JKSESSION_FNAME "openjkdf2_lastsession.json"
#define JKSESSION_VERSION 1

jkSessionMode jkSession_currentMode      = SESSION_MODE_NONE;
int           jkSession_pendingMpHosting = 0;
int           jkSession_bResumed         = 0;
char          jkSession_resumeShortName[32] = {0};

// Several engine globals are defined in .c files without a header-declared
// extern. Re-declare the ones we need here to avoid spreading externs.
extern char    jkRes_episodeGobName[32];
extern char    jkMain_aLevelJklFname[128];
extern wchar_t jkPlayer_playerShortName[32];

extern int     jkGuiNetHost_maxRank;
extern int     jkGuiNetHost_timeLimit;
extern int     jkGuiNetHost_scoreLimit;
extern int     jkGuiNetHost_maxPlayers;
extern int     jkGuiNetHost_sessionFlags;
extern int     jkGuiNetHost_gameFlags;
extern int     jkGuiNetHost_tickRate;
extern wchar_t jkGuiNetHost_gameName[32];

extern jkPlayerMpcInfo jkGuiMultiplayer_mpcInfo;

extern sithThing*  sithPlayer_pLocalPlayerThing;
extern sithWorld*  sithWorld_pCurrentWorld;

// Pending teleport state — populated by LoadAndApply, consumed once by
// ApplyPendingPosition after the player thing becomes valid.
static int        jkSession_bPendingPosition = 0;
static rdVector3  jkSession_pendingPos;
static rdMatrix34 jkSession_pendingLookOrient;
static rdVector3  jkSession_pendingEyePYR;
static int        jkSession_pendingSectorIdx = -1;
static char       jkSession_pendingMapJkl[128] = {0};

static const char* jkSession_ModeStr(jkSessionMode mode)
{
    switch (mode)
    {
        case SESSION_MODE_SP:    return "sp";
        case SESSION_MODE_DEBUG: return "debug";
        case SESSION_MODE_MP:    return "mp";
        default:                 return "";
    }
}

static jkSessionMode jkSession_ModeFromStr(const char* s)
{
    if (!s || !s[0]) return SESSION_MODE_NONE;
    if (!strcmp(s, "sp"))    return SESSION_MODE_SP;
    if (!strcmp(s, "debug")) return SESSION_MODE_DEBUG;
    if (!strcmp(s, "mp"))    return SESSION_MODE_MP;
    return SESSION_MODE_NONE;
}

void jkSession_SaveCurrent(void)
{
    const char* fpath = JKSESSION_FNAME;

    if (jkSession_currentMode == SESSION_MODE_NONE)
        return;

    if (!jkRes_episodeGobName[0] && !jkMain_aLevelJklFname[0])
        return;

    char shortName[32];
    memset(shortName, 0, sizeof(shortName));
    stdString_WcharToChar(shortName, jkPlayer_playerShortName, 31);
    shortName[31] = 0;

    // Start from a clean file so stale MP fields don't leak into an SP session
    // (or vice versa).
    stdJSON_EraseAll(fpath);

    stdJSON_SaveInt  (fpath, "version",           JKSESSION_VERSION);
    stdJSON_SetString(fpath, "mode",              jkSession_ModeStr(jkSession_currentMode));
    stdJSON_SetString(fpath, "episode_gob",       jkRes_episodeGobName);
    stdJSON_SetString(fpath, "map_jkl",           jkMain_aLevelJklFname);
    stdJSON_SetString(fpath, "player_short_name", shortName);
    stdJSON_SaveInt  (fpath, "force_rank",        0); // reserved; profile restore carries rank via .plr

    // Position snapshot — only when the local player thing is alive and the
    // world is available so we can compute the sector index.
    sithThing* pLocal = sithPlayer_pLocalPlayerThing;
    sithWorld* pWorld = sithWorld_pCurrentWorld;
    int bPlayerValid =
        pLocal && pWorld
        && (pLocal->thingflags & SITH_TF_DEAD) == 0
        && pLocal->actorParams.health > 0.0f
        && pLocal->sector
        && pWorld->sectors
        && pLocal->sector >= pWorld->sectors
        && pLocal->sector <  pWorld->sectors + pWorld->numSectors;

    stdJSON_SaveBool(fpath, "has_position", bPlayerValid);
    if (bPlayerValid)
    {
        int sectorIdx = (int)(pLocal->sector - pWorld->sectors);
        stdJSON_SaveInt  (fpath, "sector_idx", sectorIdx);
        stdJSON_SaveFloat(fpath, "pos_x",      pLocal->position.x);
        stdJSON_SaveFloat(fpath, "pos_y",      pLocal->position.y);
        stdJSON_SaveFloat(fpath, "pos_z",      pLocal->position.z);
        stdJSON_SaveFloat(fpath, "look_rvec_x", pLocal->lookOrientation.rvec.x);
        stdJSON_SaveFloat(fpath, "look_rvec_y", pLocal->lookOrientation.rvec.y);
        stdJSON_SaveFloat(fpath, "look_rvec_z", pLocal->lookOrientation.rvec.z);
        stdJSON_SaveFloat(fpath, "look_lvec_x", pLocal->lookOrientation.lvec.x);
        stdJSON_SaveFloat(fpath, "look_lvec_y", pLocal->lookOrientation.lvec.y);
        stdJSON_SaveFloat(fpath, "look_lvec_z", pLocal->lookOrientation.lvec.z);
        stdJSON_SaveFloat(fpath, "look_uvec_x", pLocal->lookOrientation.uvec.x);
        stdJSON_SaveFloat(fpath, "look_uvec_y", pLocal->lookOrientation.uvec.y);
        stdJSON_SaveFloat(fpath, "look_uvec_z", pLocal->lookOrientation.uvec.z);
        stdJSON_SaveFloat(fpath, "eye_pyr_x",  pLocal->actorParams.eyePYR.x);
        stdJSON_SaveFloat(fpath, "eye_pyr_y",  pLocal->actorParams.eyePYR.y);
        stdJSON_SaveFloat(fpath, "eye_pyr_z",  pLocal->actorParams.eyePYR.z);
    }

    if (jkSession_currentMode == SESSION_MODE_MP)
    {
        stdJSON_SetWString(fpath, "mp_char_name",       (const char16_t*)jkGuiMultiplayer_mpcInfo.name);
        stdJSON_SetString (fpath, "mp_char_model",      jkGuiMultiplayer_mpcInfo.model);
        stdJSON_SetString (fpath, "mp_char_sound",      jkGuiMultiplayer_mpcInfo.soundClass);
        stdJSON_SetString (fpath, "mp_saber_side_mat",  jkGuiMultiplayer_mpcInfo.sideMat);
        stdJSON_SetString (fpath, "mp_saber_tip_mat",   jkGuiMultiplayer_mpcInfo.tipMat);
        stdJSON_SaveInt   (fpath, "mp_char_jedi_rank",  jkGuiMultiplayer_mpcInfo.jediRank);

        stdJSON_SaveBool  (fpath, "mp_was_hosting",     jkSession_pendingMpHosting);
        stdJSON_SetWString(fpath, "mp_server_name",     (const char16_t*)jkGuiNetHost_gameName);
        stdJSON_SaveInt   (fpath, "mp_session_flags",   jkGuiNetHost_sessionFlags);
        stdJSON_SaveInt   (fpath, "mp_multi_mode_flags",jkGuiNetHost_gameFlags);
        stdJSON_SaveInt   (fpath, "mp_max_players",     jkGuiNetHost_maxPlayers);
        stdJSON_SaveInt   (fpath, "mp_max_rank",        jkGuiNetHost_maxRank);
        stdJSON_SaveInt   (fpath, "mp_score_limit",     jkGuiNetHost_scoreLimit);
        stdJSON_SaveInt   (fpath, "mp_time_limit",      jkGuiNetHost_timeLimit);
        stdJSON_SaveInt   (fpath, "mp_tick_rate",       jkGuiNetHost_tickRate);
    }
}

int jkSession_LoadAndApply(void)
{
    const char* fpath = JKSESSION_FNAME;

    int version = stdJSON_GetInt(fpath, "version", 0);
    if (version < 1)
        return 0;

    char modeStr[16] = {0};
    stdJSON_GetString(fpath, "mode", modeStr, sizeof(modeStr), "");
    jkSessionMode mode = jkSession_ModeFromStr(modeStr);
    if (mode == SESSION_MODE_NONE)
        return 0;

    char episode[128] = {0};
    char mapJkl [128] = {0};
    stdJSON_GetString(fpath, "episode_gob", episode, sizeof(episode), "");
    stdJSON_GetString(fpath, "map_jkl",     mapJkl,  sizeof(mapJkl),  "");
    if (!episode[0] || !mapJkl[0])
        return 0;

    // Drive the existing -autostart path in Main_StartupDedicated.
    Main_bAutostart   = 1;
    Main_bAutostartSp = (mode == SESSION_MODE_SP || mode == SESSION_MODE_DEBUG) ? 1 : 0;
    Main_bDevMode     = (mode == SESSION_MODE_DEBUG) ? 1 : 0;

    stdString_SafeStrCopy(Main_strEpisode, episode, sizeof(Main_strEpisode));
    stdString_SafeStrCopy(Main_strMap,     mapJkl,  sizeof(Main_strMap));

    // Hand the profile short-name off to Main_StartupDedicated via our module buffer.
    memset(jkSession_resumeShortName, 0, sizeof(jkSession_resumeShortName));
    stdJSON_GetString(fpath, "player_short_name",
                      jkSession_resumeShortName,
                      sizeof(jkSession_resumeShortName), "");

    if (mode == SESSION_MODE_MP)
    {
        memset(&jkGuiMultiplayer_mpcInfo, 0, sizeof(jkGuiMultiplayer_mpcInfo));
        stdJSON_GetWString(fpath, "mp_char_name",
                           (char16_t*)jkGuiMultiplayer_mpcInfo.name, 32,
                           (const char16_t*)L"");
        stdJSON_GetString (fpath, "mp_char_model",
                           jkGuiMultiplayer_mpcInfo.model, 32, "ky.3do");
        stdJSON_GetString (fpath, "mp_char_sound",
                           jkGuiMultiplayer_mpcInfo.soundClass, 32, "ky.snd");
        stdJSON_GetString (fpath, "mp_saber_side_mat",
                           jkGuiMultiplayer_mpcInfo.sideMat, 32, "sabergreen1.mat");
        stdJSON_GetString (fpath, "mp_saber_tip_mat",
                           jkGuiMultiplayer_mpcInfo.tipMat, 32, "sabergreen0.mat");
        jkGuiMultiplayer_mpcInfo.jediRank =
            stdJSON_GetInt(fpath, "mp_char_jedi_rank", 0);

        stdJSON_GetWString(fpath, "mp_server_name",
                           (char16_t*)jkGuiNetHost_gameName, 32,
                           (const char16_t*)L"OpenJKDF2 Server");
        jkGuiNetHost_sessionFlags = stdJSON_GetInt(fpath, "mp_session_flags",    0);
        jkGuiNetHost_gameFlags    = stdJSON_GetInt(fpath, "mp_multi_mode_flags", 144);
        jkGuiNetHost_maxPlayers   = stdJSON_GetInt(fpath, "mp_max_players",      4);
        jkGuiNetHost_maxRank      = stdJSON_GetInt(fpath, "mp_max_rank",         4);
        jkGuiNetHost_scoreLimit   = stdJSON_GetInt(fpath, "mp_score_limit",      100);
        jkGuiNetHost_timeLimit    = stdJSON_GetInt(fpath, "mp_time_limit",       30);
        jkGuiNetHost_tickRate     = stdJSON_GetInt(fpath, "mp_tick_rate",        180);
    }

    // Pull the saved position data (if any) into the pending-teleport state.
    // It'll be consumed by jkSession_ApplyPendingPosition after the first
    // level load completes — if the loaded map matches mapJkl.
    jkSession_bPendingPosition = 0;
    if (stdJSON_GetBool(fpath, "has_position", 0))
    {
        jkSession_pendingSectorIdx = stdJSON_GetInt(fpath, "sector_idx", -1);
        jkSession_pendingPos.x     = stdJSON_GetFloat(fpath, "pos_x", 0.0f);
        jkSession_pendingPos.y     = stdJSON_GetFloat(fpath, "pos_y", 0.0f);
        jkSession_pendingPos.z     = stdJSON_GetFloat(fpath, "pos_z", 0.0f);
        jkSession_pendingLookOrient.rvec.x = stdJSON_GetFloat(fpath, "look_rvec_x", 1.0f);
        jkSession_pendingLookOrient.rvec.y = stdJSON_GetFloat(fpath, "look_rvec_y", 0.0f);
        jkSession_pendingLookOrient.rvec.z = stdJSON_GetFloat(fpath, "look_rvec_z", 0.0f);
        jkSession_pendingLookOrient.lvec.x = stdJSON_GetFloat(fpath, "look_lvec_x", 0.0f);
        jkSession_pendingLookOrient.lvec.y = stdJSON_GetFloat(fpath, "look_lvec_y", 1.0f);
        jkSession_pendingLookOrient.lvec.z = stdJSON_GetFloat(fpath, "look_lvec_z", 0.0f);
        jkSession_pendingLookOrient.uvec.x = stdJSON_GetFloat(fpath, "look_uvec_x", 0.0f);
        jkSession_pendingLookOrient.uvec.y = stdJSON_GetFloat(fpath, "look_uvec_y", 0.0f);
        jkSession_pendingLookOrient.uvec.z = stdJSON_GetFloat(fpath, "look_uvec_z", 1.0f);
        jkSession_pendingLookOrient.scale.x = 0.0f;
        jkSession_pendingLookOrient.scale.y = 0.0f;
        jkSession_pendingLookOrient.scale.z = 0.0f;
        jkSession_pendingEyePYR.x  = stdJSON_GetFloat(fpath, "eye_pyr_x", 0.0f);
        jkSession_pendingEyePYR.y  = stdJSON_GetFloat(fpath, "eye_pyr_y", 0.0f);
        jkSession_pendingEyePYR.z  = stdJSON_GetFloat(fpath, "eye_pyr_z", 0.0f);
        stdString_SafeStrCopy(jkSession_pendingMapJkl, mapJkl, sizeof(jkSession_pendingMapJkl));
        jkSession_bPendingPosition = 1;
    }

    jkSession_bResumed   = 1;
    jkSession_currentMode = mode;
    return 1;
}

void jkSession_ApplyPendingPosition(void)
{
    if (!jkSession_bPendingPosition) return;

    sithThing* pLocal = sithPlayer_pLocalPlayerThing;
    sithWorld* pWorld = sithWorld_pCurrentWorld;
    if (!pLocal || !pWorld || !pWorld->sectors) {
        jkSession_bPendingPosition = 0;
        return;
    }

    // Only teleport if the currently-loaded map matches the one the position
    // was captured in. Prevents wild coordinates on a mismatched world.
    if (jkSession_pendingMapJkl[0] && jkMain_aLevelJklFname[0]
        && strcmp(jkSession_pendingMapJkl, jkMain_aLevelJklFname) != 0) {
        jkSession_bPendingPosition = 0;
        return;
    }

    if (jkSession_pendingSectorIdx < 0
        || jkSession_pendingSectorIdx >= (int)pWorld->numSectors) {
        jkSession_bPendingPosition = 0;
        return;
    }

    sithSector* pSector = &pWorld->sectors[jkSession_pendingSectorIdx];

    sithThing_DetachThing(pLocal);
    sithThing_LeaveSector(pLocal);
    sithThing_SetPosAndRot(pLocal, &jkSession_pendingPos, &jkSession_pendingLookOrient);
    sithThing_EnterSector(pLocal, pSector, 1, 0);
    pLocal->actorParams.eyePYR = jkSession_pendingEyePYR;

    // One-shot — subsequent level loads during the same session get normal spawn.
    jkSession_bPendingPosition = 0;
}
