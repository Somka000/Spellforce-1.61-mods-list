// LevelPoints.sfm - Spellforce Spell Framework mod entry points; the patch itself is in LevelPointsPatch.h.
// Put LevelPoints.sfm into the game's "sfsf" folder; LevelPoints.txt is created next to it.

#include "LevelPointsPatch.h"

// Only the first member of SFSF's SpellforceSpellFramework is used (see SFSF src/api/sfsf.h).
typedef void* (*createModInfo_ptr)(const char* mod_id, const char* mod_version,
                                   const char* mod_author, const char* mod_description);
typedef struct
{
    createModInfo_ptr createModInfo;
} SpellforceSpellFramework;

static HINSTANCE ModuleInstance;

__declspec(dllexport) void InitModule(SpellforceSpellFramework* framework)
{
    int abilityPoints, statPoints;
    ReadConfig(ModuleInstance, &abilityPoints, &statPoints);
    ApplyPatchTo((BYTE*)GetModuleHandleA(NULL), abilityPoints, statPoints);
}

__declspec(dllexport) void* RegisterMod(SpellforceSpellFramework* framework)
{
    return framework->createModInfo("Level Points", "1.0.0", "Somka",
                                    "Ability and stat points per level-up are read from LevelPoints.txt.");
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        ModuleInstance = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
