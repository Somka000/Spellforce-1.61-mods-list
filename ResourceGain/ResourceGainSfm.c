// ResourceGain.sfm - Spellforce Spell Framework mod entry points; the patch itself is in ResourceGainPatch.h.
// Put ResourceGain.sfm into the game's "sfsf" folder.

#include "ResourceGainPatch.h"

// Only the first member of SFSF's SpellforceSpellFramework is used (see SFSF src/api/sfsf.h).
typedef void* (*createModInfo_ptr)(const char* mod_id, const char* mod_version,
                                   const char* mod_author, const char* mod_description);
typedef struct
{
    createModInfo_ptr createModInfo;
} SpellforceSpellFramework;

__declspec(dllexport) void InitModule(SpellforceSpellFramework* framework)
{
    ApplyPatchTo((BYTE*)GetModuleHandleA(NULL));
}

__declspec(dllexport) void* RegisterMod(SpellforceSpellFramework* framework)
{
    return framework->createModInfo("Resource Gain", "1.0.0", "Somka",
                                    "Shows how much of each resource came in during the last minute, left of the amount.");
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
