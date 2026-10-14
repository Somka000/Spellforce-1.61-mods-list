// MoreWorkers.asi - ASI loader entry point; the patch itself is in MoreWorkersPatch.h.

#include "MoreWorkersPatch.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DWORD values[LIMIT_COUNT];
        DisableThreadLibraryCalls(instance);
        ReadConfig(instance, values);
        ApplyPatchTo((BYTE*)GetModuleHandleA(NULL), values);
    }
    return TRUE;
}
