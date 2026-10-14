// LevelPoints.asi - ASI loader entry point; the patch itself is in LevelPointsPatch.h.

#include "LevelPointsPatch.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        int abilityPoints, statPoints;
        DisableThreadLibraryCalls(instance);
        ReadConfig(instance, &abilityPoints, &statPoints);
        ApplyPatchTo((BYTE*)GetModuleHandleA(NULL), abilityPoints, statPoints);
    }
    return TRUE;
}
