#pragma once
// LevelPoints - sets how many ability and stat points the avatar gets per level-up in SpellForce 1.61.11213
// (the game gives 2 ability and 5 stat points). The values are read from LevelPoints.txt next to the .asi/.sfm file;
// the file is created with default values when missing.
//
// The avatar's experience/level-up code (VA 0064F6E7 with the default image base) works on a copy of the avatar
// record (experience, free ability points, free stat points - same order as in save files):
//     cmp al, 32h ; jae <max level>          ; level < 50
//     inc al                                  ; level + 1
//     add word ptr [ebp-1Bh], 2               ; free ability points
//     add word ptr [ebp-19h], 5               ; free stat points
// This patch changes the two immediates (signed 8-bit, so 0..127).
//
// Every player in a multiplayer game must use the same patch and the same values.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define VERSION_RVA   0x177E63 // Int32 11213 in build 1.61.11213
#define VERSION_VALUE 11213

// True when the main module is large enough to hold the addresses this patch reads, so loading the mod into
// another program skips the patch instead of crashing it.
static BOOL IsGameImageLargeEnough(BYTE* base)
{
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    return dos->e_magic == IMAGE_DOS_SIGNATURE && nt->OptionalHeader.SizeOfImage > 0x400000;
}

#define DEFAULT_ABILITY_POINTS 2
#define DEFAULT_STAT_POINTS    5
#define MAX_POINTS             127

#define LEVEL_UP_RVA 0x24F715

static const BYTE Original[] =
{
    0x66, 0x83, 0x45, 0xE5, 0x02, // add word ptr [ebp-1Bh], 2
    0x66, 0x83, 0x45, 0xE7, 0x05, // add word ptr [ebp-19h], 5
};

static const char DefaultConfig[] =
    "# LevelPoints (.asi or .sfm) - points the avatar gets for every new level.\r\n"
    "# The game itself gives AbilityPoints=2 and StatPoints=5. Allowed values: 0-127.\r\n"
    "# In multiplayer every player must use the same values.\r\n"
    "# !!WARNING!! for freegame avatar, you have to enable \"bypass player data validation\" in save data editor. "
    "If you dont do this, you wont be able to load the save.\r\n"
    "AbilityPoints=2\r\n"
    "StatPoints=5\r\n";

static int ClampPoints(int value)
{
    return value < 0 ? 0 : value > MAX_POINTS ? MAX_POINTS : value;
}

// Reads "AbilityPoints=" and "StatPoints=" lines; anything else (comments, empty lines) is ignored.
static void ReadConfig(HINSTANCE instance, int* abilityPoints, int* statPoints)
{
    char path[MAX_PATH];
    char line[256];
    char* slash;
    FILE* file;

    *abilityPoints = DEFAULT_ABILITY_POINTS;
    *statPoints = DEFAULT_STAT_POINTS;

    if (GetModuleFileNameA(instance, path, MAX_PATH) == 0) return;
    slash = strrchr(path, '\\');
    if (slash == NULL) return;
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "LevelPoints.txt");

    if (fopen_s(&file, path, "r") != 0)
    {
        if (fopen_s(&file, path, "wb") == 0)
        {
            fwrite(DefaultConfig, 1, sizeof(DefaultConfig) - 1, file);
            fclose(file);
        }
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL)
    {
        char* value = strchr(line, '=');
        if (line[0] == '#' || value == NULL) continue;
        *value++ = '\0';
        if (_stricmp(line, "AbilityPoints") == 0) *abilityPoints = ClampPoints(atoi(value));
        else if (_stricmp(line, "StatPoints") == 0) *statPoints = ClampPoints(atoi(value));
    }
    fclose(file);
}

static void ApplyPatchTo(BYTE* base, int abilityPoints, int statPoints)
{
    BYTE* site = base + LEVEL_UP_RVA;
    DWORD oldProtect;
    char message[128];

    if (!IsGameImageLargeEnough(base) || *(int*)(base + VERSION_RVA) != VERSION_VALUE ||
        memcmp(site, Original, sizeof(Original)) != 0)
    {
        OutputDebugStringA("LevelPoints: unsupported SpellForce.exe build or code already changed, nothing patched.\n");
        return;
    }

    VirtualProtect(site, sizeof(Original), PAGE_EXECUTE_READWRITE, &oldProtect);
    site[4] = (BYTE)abilityPoints;
    site[9] = (BYTE)statPoints;
    VirtualProtect(site, sizeof(Original), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(Original));

    sprintf_s(message, sizeof(message), "LevelPoints: level-up gives %d ability and %d stat points.\n",
        abilityPoints, statPoints);
    OutputDebugStringA(message);
}

