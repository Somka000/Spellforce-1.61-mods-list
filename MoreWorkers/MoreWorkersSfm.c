// MoreWorkers.sfm - Spellforce Spell Framework mod entry points; the patch itself is in MoreWorkersPatch.h.
// Put MoreWorkers.sfm into the game's "sfsf" folder; MoreWorkers.txt is created next to it.

#include "MoreWorkersPatch.h"

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
    DWORD values[LIMIT_COUNT];
    ReadConfig(ModuleInstance, values);
    ApplyPatchTo((BYTE*)GetModuleHandleA(NULL), values);
}

__declspec(dllexport) void* RegisterMod(SpellforceSpellFramework* framework)
{
    return framework->createModInfo("More Workers", "1.0.0", "Somka",
                                    "Worker limits per headquarters size are read from MoreWorkers.txt.");
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
