// BetterIcons.sfm - Spellforce Spell Framework mod entry points; the patch itself is in BetterIconsPatch.h.
// Put BetterIcons.sfm into the game's "sfsf" folder.

#include "BetterIconsPatch.h"

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
    return framework->createModInfo("Better Icons", "1.0.0", "Somka",
                                    "Up to 18 effect icons a side next to the portrait, with a bar of the time left.");
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
