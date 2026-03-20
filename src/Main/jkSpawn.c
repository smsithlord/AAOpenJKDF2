#include "jkSpawn.h"

#include "types.h"
#include "globals.h"
#include "stdPlatform.h"
#include "Platform/stdControl.h"
#include "World/sithThing.h"
#include "World/sithTemplate.h"
#include "Engine/sithCollision.h"
#include "Primitives/rdMatrix.h"
#include "Primitives/rdVector.h"
#include "Platform/Common/AACoreManager.h"

static int g_hKeyWasDown = 0;

void jkSpawn_Update(void)
{
    int hDown = 0;
    sithThing* player;
    sithThing* tmpl;
    rdVector3 lookDir;
    rdVector3 hitPos;
    sithCollisionSearchEntry* searchResult;
    sithSector* hitSector;
    rdMatrix34 orient;
    flex_t dist;

    stdControl_ReadKey(DIK_H, &hDown);

    /* Debounce: only trigger on key-down edge */
    if (!hDown || g_hKeyWasDown) {
        g_hKeyWasDown = hDown;
        return;
    }
    g_hKeyWasDown = 1;

    player = sithPlayer_pLocalPlayerThing;
    if (!player || !player->sector)
        return;

    /* Look up the template from the level's template list */
    tmpl = sithTemplate_GetEntryByName("slcompmoniter");
    if (!tmpl) {
        stdPlatform_Printf("jkSpawn: Template 'slcompmoniter' not found in level\n");
        return;
    }

    /* Get the player's actual aim direction, including pitch (eyePYR).
     * Same pattern as sithWeapon_Fire in sithAICmd.c:796-799 */
    {
        rdMatrix34 aimMatrix;
        _memcpy(&aimMatrix, &player->lookOrientation, sizeof(aimMatrix));
        rdMatrix_PreRotate34(&aimMatrix, &player->actorParams.eyePYR);
        lookDir = aimMatrix.lvec;
    }

    /* Raycast forward from player position */
    dist = sithCollision_SearchRadiusForThings(
        player->sector, player,
        &player->position, &lookDir,
        10.0f,  /* max distance */
        0.0f,   /* radius */
        0x1     /* collision flags */
    );

    if (dist <= 0.0f) {
        sithCollision_SearchClose();
        stdPlatform_Printf("jkSpawn: No surface hit\n");
        return;
    }

    /* Get the first hit result */
    searchResult = sithCollision_NextSearchResult();
    if (!searchResult) {
        sithCollision_SearchClose();
        stdPlatform_Printf("jkSpawn: No search result\n");
        return;
    }

    /* Compute hit position, offset slightly along normal to avoid being inside the surface */
    {
        float offset = 0.02f;
        hitPos.x = player->position.x + lookDir.x * searchResult->distance + searchResult->hitNorm.x * offset;
        hitPos.y = player->position.y + lookDir.y * searchResult->distance + searchResult->hitNorm.y * offset;
        hitPos.z = player->position.z + lookDir.z * searchResult->distance + searchResult->hitNorm.z * offset;
    }

    /* Build orientation: bottom on surface (uvec = normal), facing player (lvec) */
    {
        rdVector3 normal = searchResult->hitNorm;
        rdVector3 toPlayer;
        flex_t d, len;

        /* Direction from hit point toward player */
        rdVector_Sub3(&toPlayer, &player->position, &hitPos);

        /* Project onto surface plane: remove the normal component */
        d = rdVector_Dot3(&toPlayer, &normal);
        toPlayer.x -= normal.x * d;
        toPlayer.y -= normal.y * d;
        toPlayer.z -= normal.z * d;

        len = rdVector_Normalize3Acc(&toPlayer);
        if (len < 0.001f) {
            /* Degenerate case (looking straight at surface normal) — pick arbitrary forward */
            rdVector3 arbitrary = {0.0f, 1.0f, 0.0f};
            if (normal.y > 0.9f || normal.y < -0.9f)
                arbitrary = (rdVector3){1.0f, 0.0f, 0.0f};
            d = rdVector_Dot3(&arbitrary, &normal);
            toPlayer.x = arbitrary.x - normal.x * d;
            toPlayer.y = arbitrary.y - normal.y * d;
            toPlayer.z = arbitrary.z - normal.z * d;
            rdVector_Normalize3Acc(&toPlayer);
        }

        /* Flip forward to face the player (toPlayer currently points toward player,
         * but lvec needs to be the direction the model's front faces) */
        toPlayer.x = -toPlayer.x;
        toPlayer.y = -toPlayer.y;
        toPlayer.z = -toPlayer.z;

        /* rvec = cross(lvec, uvec) */
        rdVector_Cross3(&orient.rvec, &toPlayer, &normal);
        rdVector_Normalize3Acc(&orient.rvec);

        orient.lvec = toPlayer;
        orient.uvec = normal;
        rdVector_Zero3(&orient.scale);
    }

    /* Offset along surface normal so the model doesn't sink into the surface */
    {
        float normalOffset = 0.025f;
        hitPos.x += searchResult->hitNorm.x * normalOffset;
        hitPos.y += searchResult->hitNorm.y * normalOffset;
        hitPos.z += searchResult->hitNorm.z * normalOffset;
    }

    /* Use the hit surface's sector — more reliable than tracing to the hit position */
    if (searchResult->surface && searchResult->surface->parent_sector) {
        hitSector = searchResult->surface->parent_sector;
    } else {
        hitSector = player->sector;
    }

    stdPlatform_Printf("jkSpawn: Hit sector=%d, player sector=%d, hitType=%d\n",
                      hitSector ? (int)hitSector->id : -1,
                      player->sector ? (int)player->sector->id : -1,
                      searchResult->hitType);

    sithCollision_SearchClose();

    /* Spawn the thing */
    sithThing* spawned = sithThing_Create(tmpl, &hitPos, &orient, hitSector, NULL);

    if (spawned) {
        stdPlatform_Printf("jkSpawn: Spawned at (%.2f, %.2f, %.2f) thingIdx=%d\n",
                          hitPos.x, hitPos.y, hitPos.z, spawned->thingIdx);
        /* Register this thing to show task 0 (active task) on its compscreen material */
        AACoreManager_RegisterThingTask(spawned, spawned->thingIdx, 0);
    } else {
        stdPlatform_Printf("jkSpawn: Failed to spawn\n");
    }
}
