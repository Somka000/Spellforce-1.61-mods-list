// AllyAuras.asi - friendly auras (Aura of Fast Fighting, Strength, Endurance, Healing, ...) also affect
// allied players and their units in SpellForce 1.61.11213 (the build SFSF calls "beta").
// Workers are not affected by auras
//
// The aura spell effect (effect handler 0x49, RVA 0x32FD40) picks targets for an allied aura
// (spell param 5 == 1) like this:
//     if (!figuresCheckHostile(source, target) && target.owner == source.owner) -> candidate
// This patch replaces the owner comparison with the game's own figuresCheckFriendly(source, target)
// (RVA 0x2FE953, true when the clans' relation is positive), so allied players' figures qualify too.
// The patch fits into the 38 bytes of the original comparison; all jumps are relative within the exe.
//
// Note: SFSF replaces effect handler 0x49 with its own copy of this function, so with SFSF loaded
// this patch has no effect. Every player in a multiplayer game must use the same version of mod.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#define VERSION_RVA     0x177E63 // Int32 11213 in build 1.61.11213 (same check as SFSF)
#define VERSION_VALUE   11213
#define OWNER_CHECK_RVA 0x33013C // VA 0x73013C with the default image base

static const BYTE Original[38] =
{
    0x8B, 0x47, 0x1C,                                           // mov  eax, [edi+1Ch]         ; figure array
    0x8B, 0x95, 0x40, 0xFF, 0xFF, 0xFF,                         // mov  edx, [ebp-0C0h]        ; source * 2B3h
    0xC7, 0x85, 0x54, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, // mov  dword [ebp-0ACh], 1
    0x0F, 0xB7, 0x4C, 0x18, 0x1C,                               // movzx ecx, word [eax+ebx+1Ch] ; target owner
    0x0F, 0xB7, 0x44, 0x10, 0x1C,                               // movzx eax, word [eax+edx+1Ch] ; source owner
    0x66, 0x3B, 0xC8,                                           // cmp  cx, ax
    0x0F, 0x85, 0xE4, 0x01, 0x00, 0x00,                         // jne  0073:0346              ; skip target
};

static const BYTE Patched[38] =
{
    0xC7, 0x85, 0x54, 0xFF, 0xFF, 0xFF, 0x01, 0x00, 0x00, 0x00, // mov  dword [ebp-0ACh], 1
    0x8B, 0x4F, 0x24,                                           // mov  ecx, [edi+24h]         ; figure toolbox
    0x56,                                                       // push esi                    ; target
    0xFF, 0xB5, 0x3C, 0xFF, 0xFF, 0xFF,                         // push dword [ebp-0C4h]       ; source
    0xE8, 0xFE, 0xE7, 0xFC, 0xFF,                               // call figuresCheckFriendly (RVA 2FE953)
    0x85, 0xC0,                                                 // test eax, eax
    0x0F, 0x84, 0xE9, 0x01, 0x00, 0x00,                         // je   0073:0346              ; skip target
    0x90, 0x90, 0x90, 0x90, 0x90,                               // nop
};

static void ApplyPatchTo(BYTE* base)
{
    DWORD oldProtect;
    BYTE* target = base + OWNER_CHECK_RVA;

    if (*(int*)(base + VERSION_RVA) != VERSION_VALUE || memcmp(target, Original, sizeof(Original)) != 0)
    {
        OutputDebugStringA("AllyAuras.asi: unsupported SpellForce.exe build or code already changed, nothing patched.\n");
        return;
    }

    VirtualProtect(target, sizeof(Patched), PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(target, Patched, sizeof(Patched));
    VirtualProtect(target, sizeof(Patched), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), target, sizeof(Patched));

    OutputDebugStringA("AllyAuras.asi: allied auras affect allied players.\n");
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        ApplyPatchTo((BYTE*)GetModuleHandleA(NULL));
    }
    return TRUE;
}
