#pragma once
// MoreWorkers - sets the worker limit (game: 20/30/40/50 depending on the headquarters) in SpellForce 1.61.11213.
// The limits are read from MoreWorkers.txt next to the .asi/.sfm file; the file is created with default values when missing.
//
// The worker limit of a player's race is computed by one function (VA 00727880 with the default image base):
// it looks at the player's headquarters of that race and returns the limit in di:
//     0072799C  mov edi, 32h   ; large headquarters
//     00727AE5  mov edi, 28h   ; medium headquarters
//     00727BF0  mov edi, 1Eh   ; small headquarters
//     00727BD6  mov edi, 14h   ; no headquarters of these kinds
// Everything uses this function: the "can produce a worker" check (006B94D5), the "limit reached" check (00728058)
// and the worker counter in the interface. The combat unit limit is a separate counter and is not changed.
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

#define MAX_LIMIT     65535 // the game returns the limit as a 16-bit value

typedef struct
{
    const char* Key;  // name in MoreWorkers.txt
    DWORD Rva;        // "mov edi, imm32" (BF xx xx xx xx)
    DWORD Original;   // original immediate
} LimitValue;

static const LimitValue Limits[] =
{
    { "NoHeadquarters",     0x327BD6, 20 },
    { "SmallHeadquarters",  0x327BF0, 30 },
    { "MediumHeadquarters", 0x327AE5, 40 },
    { "LargeHeadquarters",  0x32799C, 50 },
};

#define LIMIT_COUNT (sizeof(Limits) / sizeof(Limits[0]))

static const char DefaultConfig[] =
    "# MoreWorkers (.asi or .sfm) - maximum number of workers per race, depending on the largest headquarters you have.\r\n"
    "# The game itself uses NoHeadquarters=20, SmallHeadquarters=30, MediumHeadquarters=40, LargeHeadquarters=50.\r\n"
    "# Allowed values: 0-65535. In multiplayer every player must use the same values.\r\n"
    "NoHeadquarters=20\r\n"
    "SmallHeadquarters=30\r\n"
    "MediumHeadquarters=40\r\n"
    "LargeHeadquarters=50\r\n";

static DWORD ClampLimit(int value)
{
    return value < 0 ? 0 : value > MAX_LIMIT ? MAX_LIMIT : (DWORD)value;
}

// Reads "Key=value" lines; anything else (comments, empty lines) is ignored.
static void ReadConfig(HINSTANCE instance, DWORD values[LIMIT_COUNT])
{
    char path[MAX_PATH];
    char line[256];
    char* slash;
    FILE* file;
    int i;

    for (i = 0; i < LIMIT_COUNT; i++) values[i] = Limits[i].Original; // defaults: the game's own limits

    if (GetModuleFileNameA(instance, path, MAX_PATH) == 0) return;
    slash = strrchr(path, '\\');
    if (slash == NULL) return;
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "MoreWorkers.txt");

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
        for (i = 0; i < LIMIT_COUNT; i++)
        {
            if (_stricmp(line, Limits[i].Key) == 0) values[i] = ClampLimit(atoi(value));
        }
    }
    fclose(file);
}

static void ApplyPatchTo(BYTE* base, const DWORD values[LIMIT_COUNT])
{
    int i;
    char message[160];

    if (!IsGameImageLargeEnough(base) || *(int*)(base + VERSION_RVA) != VERSION_VALUE)
    {
        OutputDebugStringA("MoreWorkers: unsupported SpellForce.exe build, nothing patched.\n");
        return;
    }
    for (i = 0; i < LIMIT_COUNT; i++)
    {
        BYTE* site = base + Limits[i].Rva;
        if (site[0] != 0xBF || memcmp(site + 1, &Limits[i].Original, sizeof(DWORD)) != 0)
        {
            OutputDebugStringA("MoreWorkers: unexpected code, nothing patched.\n");
            return;
        }
    }

    for (i = 0; i < LIMIT_COUNT; i++)
    {
        BYTE* value = base + Limits[i].Rva + 1;
        DWORD oldProtect;
        VirtualProtect(value, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &oldProtect);
        memcpy(value, &values[i], sizeof(DWORD));
        VirtualProtect(value, sizeof(DWORD), oldProtect, &oldProtect);
        FlushInstructionCache(GetCurrentProcess(), value, sizeof(DWORD));
    }

    sprintf_s(message, sizeof(message), "MoreWorkers: worker limits %lu/%lu/%lu/%lu (none/small/medium/large).\n",
        values[0], values[1], values[2], values[3]);
    OutputDebugStringA(message);
}

