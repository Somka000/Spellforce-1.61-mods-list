// HotbarKeys.sfm - Spellforce Spell Framework mod entry points; the patch itself is in HotbarKeysPatch.h.
// Put HotbarKeys.sfm into the game's "sfsf" folder.

#include "HotbarKeysPatch.h"

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
    return framework->createModInfo("Hotbar Keys", "1.1.0", "Somka",
                                    "F2-F6 use the first five slots of the spell hotbar, Shift+F2-F6 the spells shown "
                                    "under the avatar portrait for the selected enemy (configurable in HotbarKeys.txt).");
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
