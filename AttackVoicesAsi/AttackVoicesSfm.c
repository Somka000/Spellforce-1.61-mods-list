// AttackVoices.sfm - Spellforce Spell Framework mod entry points; the patch itself is in AttackVoicesPatch.h.
// Put AttackVoices.sfm into the game's "sfsf" folder.

#include "AttackVoicesPatch.h"

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
    return framework->createModInfo("Attack Voices", "1.0.0", "Somka",
                                    "Combat units answer attack orders on enemy units with their own voice lines.");
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
