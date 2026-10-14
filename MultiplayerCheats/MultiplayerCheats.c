// MultiplayerCheats.asi - ASI loader entry point; the patch itself is in MultiplayerCheatsPatch.h.

#include "MultiplayerCheatsPatch.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        ApplyPatchTo((BYTE*)GetModuleHandleA(NULL));
    }
    return TRUE;
}
