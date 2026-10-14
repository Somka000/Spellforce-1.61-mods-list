#pragma once
// MultiplayerCheats - lets the console cheats (Application:GiveMeGoods(x), SetGodMode, ...) work in multiplayer.
// SpellForce 1.61.11213 only.
//
// Every cheat method of CAppMain starts with the same check:
//     if (GetAppSession()->GetGameType() != 1) return;    // call 005F8ED0 ; cmp eax, 1 ; jne <end>
// Game type 1 is single player, so in multiplayer the cheat silently does nothing.
// This patch removes that jne in all 15 cheats. The cheats themselves don't change the game state directly:
// they queue a game command (CAppMain 005B88E0 -> 0069F6F0) or send one through the game controller (005FA300),
// which is how player orders reach every client, so the effect should apply on all clients.
//
// UnExplored:Enable(0) (CDrwUnexplored, 00898E49) has its own lock: when a map starts the game sets the
// "unexplored locked" flag (+400490h) to (game type != 1), and Enable(0) is ignored while the flag is set.
// That jne is removed too. This is drawing only, so it changes the view of the player who uses it.
//
// Every player in a multiplayer game must use the same patch.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#define VERSION_RVA   0x177E63 // Int32 11213 in build 1.61.11213
#define VERSION_VALUE 11213

typedef struct
{
    const char* Name;
    DWORD Rva;        // address of the compare right before the jne
    BYTE CmpLength;
    BYTE Cmp[7];      // original compare bytes (checked only)
    BYTE JumpLength;  // 2 = jne rel8, 6 = jne rel32
    BYTE Jump[6];     // original jne bytes, replaced with nops
} GateCheck;

#define CMP_EAX_1 3, { 0x83, 0xF8, 0x01 } // cmp eax, 1

static const GateCheck Gates[] =
{
    { "FastHeroCast",             0x1B5708, CMP_EAX_1, 2, { 0x75, 0x33 } },
    { "GiveMeGoods",              0x1B5878, CMP_EAX_1, 2, { 0x75, 0x34 } },
    { "MoveCameraTo",             0x1B5C98, CMP_EAX_1, 2, { 0x75, 0x41 } },
    { "SetAggroRects",            0x1B5D08, CMP_EAX_1, 6, { 0x0F, 0x85, 0x9A, 0x00, 0x00, 0x00 } },
    { "SetBuildingFastBuildMode", 0x1B5DD8, CMP_EAX_1, 2, { 0x75, 0x33 } },
    { "SetBuildingTechTreeMode",  0x1B5E38, CMP_EAX_1, 2, { 0x75, 0x33 } },
    { "SetFigureTechTreeMode",    0x1B5E98, CMP_EAX_1, 2, { 0x75, 0x33 } },
    { "SetFixedTime",             0x1B5EF8, CMP_EAX_1, 2, { 0x75, 0x3A } },
    { "SetGodMode",               0x1B5F58, CMP_EAX_1, 2, { 0x75, 0x33 } },
    { "SetNoManaUsage",           0x1B5FB8, CMP_EAX_1, 2, { 0x75, 0x33 } },
    { "TakeFigureControl",        0x1B6028, CMP_EAX_1, 2, { 0x75, 0x36 } },
    { "TeleportAvatar",           0x1B6088, CMP_EAX_1, 2, { 0x75, 0x41 } },
    { "Test_FigureVanish",        0x1B6109, CMP_EAX_1, 2, { 0x75, 0x49 } },
    { "Test_PlaceRTSUnit",        0x1B6199, CMP_EAX_1, 2, { 0x75, 0x6A } },
    { "Test_SpawnUnit",           0x1B6249, CMP_EAX_1, 2, { 0x75, 0x68 } },
    // cmp dword ptr [eax+400490h], 0 ; jne
    { "UnExplored:Enable",        0x498E59, 7, { 0x83, 0xB8, 0x90, 0x04, 0x40, 0x00, 0x00 }, 2, { 0x75, 0x0B } },
};

static void ApplyPatchTo(BYTE* base)
{
    int i;
    char message[128];

    if (*(int*)(base + VERSION_RVA) != VERSION_VALUE)
    {
        OutputDebugStringA("MultiplayerCheats: unsupported SpellForce.exe build, nothing patched.\n");
        return;
    }

    // Check everything first, so the game is either fully patched or not at all.
    for (i = 0; i < sizeof(Gates) / sizeof(Gates[0]); i++)
    {
        BYTE* site = base + Gates[i].Rva;
        if (memcmp(site, Gates[i].Cmp, Gates[i].CmpLength) != 0 ||
            memcmp(site + Gates[i].CmpLength, Gates[i].Jump, Gates[i].JumpLength) != 0)
        {
            wsprintfA(message, "MultiplayerCheats: unexpected code in %s, nothing patched.\n", Gates[i].Name);
            OutputDebugStringA(message);
            return;
        }
    }

    for (i = 0; i < sizeof(Gates) / sizeof(Gates[0]); i++)
    {
        BYTE* jump = base + Gates[i].Rva + Gates[i].CmpLength;
        DWORD oldProtect;
        VirtualProtect(jump, Gates[i].JumpLength, PAGE_EXECUTE_READWRITE, &oldProtect);
        memset(jump, 0x90, Gates[i].JumpLength); // nop: always continue with the cheat
        VirtualProtect(jump, Gates[i].JumpLength, oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), jump, Gates[i].JumpLength);
    }

    OutputDebugStringA("MultiplayerCheats: cheats enabled in multiplayer.\n");
}
