// MultiplayerCheats.sfm - Spellforce Spell Framework mod entry points; the patch itself is in MultiplayerCheatsPatch.h.
// Put MultiplayerCheats.sfm into the game's "sfsf" folder.

#include "MultiplayerCheatsPatch.h"

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
    return framework->createModInfo("Multiplayer Cheats", "1.0.0", "Somka",
                                    "Console cheats (GiveMeGoods, SetGodMode, Test_SpawnUnit, UnExplored:Enable, ...) also work in multiplayer.");
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
