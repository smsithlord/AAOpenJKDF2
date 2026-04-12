#include "sithTemplate.h"

#include "World/sithThing.h"
#include "World/sithWorld.h"
#include "General/stdString.h"
#include "General/stdConffile.h"
#include "General/stdHashTable.h"
#include "General/stdFnames.h"

#include "jk.h"

int sithTemplate_addonReserve = 0;
static int sithTemplate_addonStartIndex = -1; /* index where addon templates begin in static world */

int sithTemplate_Startup()
{
    sithTemplate_hashmap = stdHashTable_New(512);
    return sithTemplate_hashmap != 0;
}

void sithTemplate_Shutdown()
{
    if ( sithTemplate_hashmap )
    {
        stdHashTable_Free(sithTemplate_hashmap);
        sithTemplate_hashmap = 0;
    }
}

int sithTemplate_New(sithWorld *world, unsigned int numTemplates)
{
    numTemplates += sithTemplate_addonReserve;
    world->templates = (sithThing*)pSithHS->alloc(sizeof(sithThing) * numTemplates);
    if (!world->templates)
        return 0;

    _memset(world->templates, 0, sizeof(sithThing) * numTemplates);
    for (int i = 0; i < numTemplates; i++)
    {
        sithThing_DoesRdThingInit(&world->templates[i]);
        if ( world->level_type_maybe & 1 )
        {
            world->templates[i].thingIdx = 0x8000 | i;
        }
        else
        {
            world->templates[i].thingIdx = i;
        }
    }

    world->numTemplates = numTemplates;
    world->numTemplatesLoaded = 0;
    return 1;
}

sithThing* sithTemplate_GetEntryByIdx(int idx)
{
    sithWorld* world = sithWorld_pCurrentWorld;
    if ( idx & 0x8000 )
    {
        world = sithWorld_pStatic;
        idx &= ~0x8000; // ?
    }
    
    if ( world && idx > 0 && idx < world->numTemplatesLoaded ) // original doesn't check world, but Cog does?
    {
        return &world->templates[idx];
    }

    return NULL;
}

int sithTemplate_Load(sithWorld *world, int a2)
{
    unsigned int numTemplates;

    if ( a2 )
        return 0;

    stdConffile_ReadArgs();
    if ( _memcmp(stdConffile_entry.args[0].value, "world", 6u) || _memcmp(stdConffile_entry.args[1].value, "templates", 0xAu) )
        return 0;

    numTemplates = _atoi(stdConffile_entry.args[2].value);
    if ( !numTemplates )
        return 1;
    
    sithTemplate_New(world, numTemplates);
    
    while ( stdConffile_ReadArgs() )
    {
        if ( !_memcmp(stdConffile_entry.args[0].value, "end", 4u) )
            break;
        sithTemplate_CreateEntry(world);
    }

    // If static world, also load addon template entries (conffile nesting)
    if (world->level_type_maybe & 1) {
        sithTemplate_addonStartIndex = world->numTemplatesLoaded;
        char addonPath[128];
        char addonSection[64];
        stdFnames_MakePath(addonPath, 128, "jkl", "addon-static.jkl");
        if (stdConffile_OpenRead(addonPath)) {
            while (stdConffile_ReadLine()) {
                if (_sscanf(stdConffile_aLine, " section: %s", addonSection) == 1
                    && !__strcmpi(addonSection, "templates")) {
                    if (stdConffile_ReadArgs()) { // skip "world templates N" header
                        while (stdConffile_ReadArgs()) {
                            if (!_memcmp(stdConffile_entry.args[0].value, "end", 4u))
                                break;
                            sithTemplate_CreateEntry(world);
                        }
                    }
                    break;
                }
            }
            stdConffile_Close();
        }
    }

    return 1;
}

void sithTemplate_ReloadAddon(void)
{
    sithWorld* world = sithWorld_pStatic;
    if (!world || sithTemplate_addonStartIndex < 0) return;

    /* Count how many addon templates are in the current file */
    int newAddonCount = 0;
    {
        char scanPath[128];
        char scanSection[64];
        stdFnames_MakePath(scanPath, 128, "jkl", "addon-static.jkl");
        if (stdConffile_OpenRead(scanPath)) {
            while (stdConffile_ReadLine()) {
                if (_sscanf(stdConffile_aLine, " section: %s", scanSection) == 1
                    && !__strcmpi(scanSection, "templates")) {
                    if (stdConffile_ReadArgs()) {
                        newAddonCount = _atoi(stdConffile_entry.args[2].value);
                    }
                    break;
                }
            }
            stdConffile_Close();
        }
    }

    /* Grow array if current capacity is insufficient */
    int needed = sithTemplate_addonStartIndex + newAddonCount;
    if (needed > world->numTemplates) {
        sithThing* newArr = (sithThing*)pSithHS->alloc(sizeof(sithThing) * needed);
        if (!newArr) return;
        _memcpy(newArr, world->templates, sizeof(sithThing) * world->numTemplatesLoaded);
        /* Init new slots */
        for (int i = world->numTemplates; i < needed; i++) {
            _memset(&newArr[i], 0, sizeof(sithThing));
            sithThing_DoesRdThingInit(&newArr[i]);
            newArr[i].thingIdx = 0x8000 | i;
        }
        /* Update hashtable pointers — templates are looked up by name, not by pointer from things */
        for (int i = 0; i < world->numTemplatesLoaded; i++) {
#ifdef STDHASHTABLE_CRC32_KEYS
            stdHashTable_FreeKeyCrc32(sithTemplate_hashmap, world->templates[i].templateNameCrc);
            stdHashTable_SetKeyVal(sithTemplate_hashmap, newArr[i].template_name, &newArr[i]);
#else
            stdHashTable_FreeKey(sithTemplate_hashmap, world->templates[i].template_name);
            stdHashTable_SetKeyVal(sithTemplate_hashmap, newArr[i].template_name, &newArr[i]);
#endif
        }
        pSithHS->free(world->templates);
        world->templates = newArr;
        world->numTemplates = needed;
    }

    /* Don't free/replace existing templates — spawned things reference them.
     * sithTemplate_CreateEntry checks the hashtable and skips duplicates.
     * We only add genuinely new templates that weren't in the previous load. */
    char addonPath[128];
    char addonSection[64];
    sithWorld* oldLoading = sithWorld_pLoading;
    sithWorld_pLoading = world;
    stdFnames_MakePath(addonPath, 128, "jkl", "addon-static.jkl");
    if (stdConffile_OpenRead(addonPath)) {
        while (stdConffile_ReadLine()) {
            if (_sscanf(stdConffile_aLine, " section: %s", addonSection) == 1
                && !__strcmpi(addonSection, "templates")) {
                if (stdConffile_ReadArgs()) { /* skip "world templates N" header */
                    while (stdConffile_ReadArgs()) {
                        if (!_memcmp(stdConffile_entry.args[0].value, "end", 4u))
                            break;
                        sithTemplate_CreateEntry(world);
                    }
                }
                break;
            }
        }
        stdConffile_Close();
    }
    sithWorld_pLoading = oldLoading;

    jk_printf("sithTemplate: Addon templates now at %d (addon start: %d)\n",
              world->numTemplatesLoaded, sithTemplate_addonStartIndex);
}

int sithTemplate_OldNew(char *fpath)
{
    return 0; // TODO unused but interesting
}

void sithTemplate_OldFree()
{
    // TODO unused but interesting
}

void sithTemplate_FreeWorld(sithWorld *world)
{
    for (int i = 0; i < world->numTemplatesLoaded; i++)
    {
        rdThing_FreeEntry(&world->templates[i].rdthing);
#ifdef STDHASHTABLE_CRC32_KEYS
        stdHashTable_FreeKeyCrc32(sithTemplate_hashmap, world->templates[i].templateNameCrc);
#else
        stdHashTable_FreeKey(sithTemplate_hashmap, world->templates[i].template_name);
#endif
    }

    if ( world->templates )
    {
        pSithHS->free(world->templates);
        world->templates = 0;
        world->numTemplates = 0;
        world->numTemplatesLoaded = 0;
    }
}

sithThing* sithTemplate_GetEntryByName(const char *name)
{
    sithThing *result;

    if ( !_memcmp(name, "none", 5u) )
        return 0;
    result = (sithThing *)stdHashTable_GetKeyVal(sithTemplate_hashmap, name);
    if ( result )
        return result;

    if ( !sithTemplate_count )
        return 0;

    // TODO interesting, but this hashtable is never initialized
#if 0
    char v6[0x400];
    const char** v3 = (const char **)stdHashTable_GetKeyVal(sithTemplate_oldHashtable, name);
    if ( !v3 )
        return 0;
    if ( v3[3] )
        sithTemplate_GetEntryByName(v3[3]);
    stdConffile_OpenRead("none");

    _strncpy(v6, v3[2], 0x3FFu);
    v6[0x3FF] = 0;

    stdConffile_ReadArgsFromStr(&v6);
    result = sithTemplate_CreateEntry(sithWorld_pLoading);
    stdConffile_Close();
    return result;
#endif
    return 0;
}

sithThing* sithTemplate_CreateEntry(sithWorld *world)
{
    sithThing *result;
    sithThing tmp;
    const char* template_name;

    result = (sithThing *)stdHashTable_GetKeyVal(sithTemplate_hashmap, (const char*)stdConffile_entry.args[0].value);
    if ( result )
        return result;

    // Added: memset for consistent behavior
    memset(&tmp, 0, sizeof(tmp));

    sithThing_DoesRdThingInit(&tmp);
    result = (sithThing *)stdHashTable_GetKeyVal(sithTemplate_hashmap, (const char*)stdConffile_entry.args[1].value);
    sithThing_InstantiateFromTemplate(&tmp, result);

    template_name = stdConffile_entry.args[0].value;
#ifdef SITH_DEBUG_STRUCT_NAMES
    stdString_SafeStrCopy(tmp.template_name, template_name, sizeof(tmp.template_name));
#endif
#ifdef STDHASHTABLE_CRC32_KEYS
    tmp.templateNameCrc = stdCrc32(template_name, strlen(template_name));
#endif

    for (int i = 2; i < stdConffile_entry.numArgs; i++)
    {
        sithThing_ParseArgs(&stdConffile_entry.args[i], &tmp);
    }

    if (!tmp.type )
        return 0;

    if ( world->numTemplatesLoaded >= world->numTemplates )
        return 0;

    result = &world->templates[world->numTemplatesLoaded++];
    tmp.thingIdx = result->thingIdx;
    _memcpy(result, &tmp, sizeof(sithThing));
#ifdef SITH_DEBUG_STRUCT_NAMES
    // The copies of names are load-bearing, SetKeyVal stores a reference
    stdHashTable_SetKeyVal(sithTemplate_hashmap, result->template_name, result);
#else
    stdHashTable_SetKeyVal(sithTemplate_hashmap, template_name, result);
#endif

    return result;
}
