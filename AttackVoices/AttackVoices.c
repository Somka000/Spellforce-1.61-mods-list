// AttackVoices.asi - ASI loader entry point; the patch itself is in AttackVoicesPatch.h.

#include "AttackVoicesPatch.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        ApplyPatchTo((BYTE*)GetModuleHandleA(NULL));
    }
    return TRUE;
}
