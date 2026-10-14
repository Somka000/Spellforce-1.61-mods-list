// DropPoint.sfm - Spellforce Spell Framework mod entry points; the patch itself is in DropPointPatch.h.
// Put DropPoint.sfm into the game's "sfsf" folder.

#include "DropPointPatch.h"

// Only the first member of SFSF's SpellforceSpellFramework is used (see SFSF src/api/sfsf.h).
typedef void* (*createModInfo_ptr)(const char* mod_id, const char* mod_version,
                                   const char* mod_author, const char* mod_description);
typedef struct
{
    createModInfo_ptr createModInfo;
} SpellforceSpellFramework;

static HINSTANCE Instance;

__declspec(dllexport) void InitModule(SpellforceSpellFramework* framework)
{
    ApplyPatch(Instance);
}

__declspec(dllexport) void* RegisterMod(SpellforceSpellFramework* framework)
{
    return framework->createModInfo("Drop Point", "1.0.0", "Somka",
                                    "Lights the place workers bring their goods to on a building's blueprint.");
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        Instance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
