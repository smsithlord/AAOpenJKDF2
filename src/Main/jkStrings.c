#include "jkStrings.h"

#include "General/stdStrTable.h"
#include "General/stdHashTable.h"
#include "Cog/jkCog.h"
#include "../jk.h"

static int jkStrings_bInitialized = 0;
static stdStrTable jkStrings_table;
#ifdef QOL_IMPROVEMENTS
static stdStrTable jkStrings_tableExt;
stdStrTable jkStrings_tableExtOver;
#endif // QOL_IMPROVEMENTS

int jkStrings_Startup()
{
    stdPlatform_Printf("OpenJKDF2: %s\n", __func__);
    
    // Added: clean reset
    _memset(&jkStrings_table, 0, sizeof(jkStrings_table));

    int result = stdStrTable_Load(&jkStrings_table, "ui\\jkstrings.uni");

    // Added: OpenJKDF2 i8n
#ifdef QOL_IMPROVEMENTS
    _memset(&jkStrings_tableExtOver, 0, sizeof(jkStrings_tableExtOver));
    _memset(&jkStrings_tableExt, 0, sizeof(jkStrings_tableExt));

    stdStrTable_Load(&jkStrings_tableExtOver, "ui\\openjkdf2_i8n.uni");
    stdStrTable_Load(&jkStrings_tableExt, "ui\\openjkdf2.uni");

    /* AArcade: Inject keybind labels into the string table so they always exist,
     * even if a mod overrides the .uni file without our entries. */
    {
        static stdStrMsg aaStrMsgs[] = {
            { "AATASKSTAB",   L"Tasks Tab",   0 },
            { "AALIBRARYTAB", L"Library Tab",  0 },
            { "AATABMENU",    L"Tab Menu",     0 },
            { "AASELECT",     L"Select",       0 },
            { "AABUILD",      L"Build",        0 },
        };
        if (!jkStrings_tableExt.hashtable) {
            jkStrings_tableExt.hashtable = stdHashTable_New(16);
        }
        for (int i = 0; i < 5; i++) {
            /* Only add if not already present (don't override mod customizations) */
            if (!stdStrTable_GetUniString(&jkStrings_tableExt, aaStrMsgs[i].key)) {
                stdHashTable_SetKeyVal(jkStrings_tableExt.hashtable, aaStrMsgs[i].key, &aaStrMsgs[i]);
            }
        }
    }
#endif // QOL_IMPROVEMENTS

    jkStrings_bInitialized = 1;
    return result;
}

void jkStrings_Shutdown()
{
    stdPlatform_Printf("OpenJKDF2: %s\n", __func__);

    // Added: OpenJKDF2 i8n
#ifdef QOL_IMPROVEMENTS
    stdStrTable_Free(&jkStrings_tableExtOver);
    stdStrTable_Free(&jkStrings_tableExt);
    _memset(&jkStrings_tableExtOver, 0, sizeof(jkStrings_tableExtOver));
    _memset(&jkStrings_tableExt, 0, sizeof(jkStrings_tableExt));
#endif

    stdStrTable_Free(&jkStrings_table);
    jkStrings_bInitialized = 0;

    // Added: clean reset
    _memset(&jkStrings_table, 0, sizeof(jkStrings_table));
}

wchar_t* jkStrings_GetUniString(const char *key)
{
    wchar_t *result; // eax

    // Added: Allow openjkdf2_i8n.uni to override everything
#ifdef QOL_IMPROVEMENTS
    result = stdStrTable_GetUniString(&jkStrings_tableExtOver, key);
    if ( !result )
#endif
    result = stdStrTable_GetUniString(&jkStrings_table, key);
    if ( !result )
        result = stdStrTable_GetUniString(&jkCog_strings, key);
#ifdef QOL_IMPROVEMENTS
    if ( !result )
        result = stdStrTable_GetUniString(&jkStrings_tableExt, key);
#endif
    return result;
}

wchar_t* jkStrings_GetUniStringWithFallback(const char *key)
{
    wchar_t *result; // eax

    // Added: Allow openjkdf2_i8n.uni to override everything
#ifdef QOL_IMPROVEMENTS
    result = stdStrTable_GetUniString(&jkStrings_tableExtOver, key);
    if ( !result )
#endif
    result = stdStrTable_GetUniString(&jkStrings_table, key);

    // Added: OpenJKDF2 i8n -- stdStrTable_GetStringWithFallback must always be the last lookup
    // because it always succeeds.
#ifdef QOL_IMPROVEMENTS
    if ( !result )
        result = stdStrTable_GetUniString(&jkCog_strings, (char *)key);
    if ( !result )
        result = stdStrTable_GetStringWithFallback(&jkStrings_tableExt, key);
#else
        if ( !result )
        result = stdStrTable_GetStringWithFallback(&jkCog_strings, (char *)key);
#endif
    return result;
}

int jkStrings_unused_sub_40B490()
{
    return 1;
}
