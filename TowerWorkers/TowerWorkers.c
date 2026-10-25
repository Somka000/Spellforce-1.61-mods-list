// TowerWorkers.asi - ASI loader entry point; the patch itself is in TowerWorkersPatch.h.

#include "TowerWorkersPatch.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        ApplyPatch(instance);
    }
    return TRUE;
}
