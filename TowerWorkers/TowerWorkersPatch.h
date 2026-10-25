#pragma once
// TowerWorkers - the defence towers and the White Hand of human players only work with a worker inside, the way an
// armory needs its smith. ASI loader version for SpellForce 1.61.11213 WITHOUT the Spellforce Spell Framework
// (with SFSF use TowerWorkers.sfm instead: SFSF replaces the same game functions).
//
// Buildings (type, race): Crossbow Tower 0Dh and White Hand 20h (humans), Archer Tower 26h and Frostbringer 27h
// (elves), Firestarter 47h (orcs), Stone Thrower 54h (trolls), Tower of Sorcery 66h and Mindbreaker 67h (dark
// elves).
//
// Game side (addresses with the default image base):
//   - A finished tower gets a tower unit (CGdBuildingToolbox::AttachTowerUnit 006D9290): a figure with the TOWER
//     flag (20000h) whose building is the tower. The unit does the shooting and, for the White Hand, the healing.
//   - Figure jobs run in CGdFigureJobs::Update (006F14B0), which skips figures with the FREEZED flag (100h).
//   - A worker inside a building stands within it: kGdJobEnterBuilding (68) moves it there, and when it leaves (the
//     player takes it out, the building is destroyed: the smith's job ends in 006E60F0) the game moves it to a free
//     place next to the building. Moving a figure: CGdFigureToolBox 00702CD6(figure, x | y << 16). Only figures
//     with the TOWER flag are drawn on top of the tower (render flag, 00907E60).
//   - CGdBuilding::IsHabitable (006C7600) and IsHabitableSingle (006C7530) say by building type whether a building
//     takes workers; the most workers is CGdBuilding::SetWorkerMax (006C8660; 0 for towers).
//   - A worker arriving at its building runs 006EDA00, which picks what the worker does with a switch on the
//     building type (006EDAAC: lea eax,[ecx-1]; cmp eax,88h; ja ...). For a forge: SetJob(kGdJobSmithSearchForWork),
//     the artisan's clothes (CGdFigureToolBox 006F84C1), task "forge" (CGdFigure 006B7110, task 6) and tools
//     (006FD783: orcs 0/12h, dwarves 10h/0, the others 9/0).
//   CGdFigureJobs: +0Ch CGdBuilding (records of 34h from +4: position +4, owner +8, type +0Ah, race +0Bh, workers +31h, most
//   workers +32h), +20h CGdFigure (count word at +0, records of 2B3h from +2: building +14h, flags +16h, owner +1Ch,
//   job +20Bh), +24h CGdFigureToolBox, +38h CGdPlayer (records of 188h from +6: in use +0Ah, avatar figure +0B3h).
//
// The patch:
//   - IsHabitable and IsHabitableSingle say yes for the tower types (their first 7 bytes move to the hook);
//   - a worker arriving at a tower dresses and takes the tools like a smith at a forge and takes the smith's job;
//     with Inside=1 (TowerWorkers.txt) it goes into the middle of the tower (the building's position), where the
//     tower hides it; taking it out (building window, worker icon) and a destroyed tower work as for a forge;
//   - once per game tick (start of CGdFigureJobs::Update) the towers of human players get room for 1 worker (also
//     towers built before), and the tower unit of every human player's tower without its worker (a figure of the
//     tower with the smith's job) is frozen (FREEZED | AI_BLOCKED), and unfrozen when the worker is there. Only the
//     flags the patch set are taken away. A worker the first version took off the map (flag 2) comes back.
// A human player is a player who has (or had) an avatar; towers of the computer players work as before. The rule
// changes the game, so in multiplayer every player must use the mod (with the same settings). TowerWorkers.log next
// to the .asi lists what happens.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

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

#define JOBS_UPDATE_RVA        0x2F14B0 // CGdFigureJobs::Update
#define IS_HABITABLE_RVA       0x2C7600 // CGdBuilding::IsHabitable(building)
#define IS_HABITABLE_SINGLE_RVA 0x2C7530 // CGdBuilding::IsHabitableSingle(building)
#define HABITABLE_RESUME       7        // after push ebp; mov ebp,esp; movzx eax,word ptr [ebp+8]
#define ENTRY_SWITCH_RVA       0x2EDAAC // worker arrives: switch on the building type in ecx
#define ENTRY_SWITCH_RESUME    0x2EDAB4 // ja default (flags of cmp eax,88h)
#define ENTRY_RETURN_RVA       0x2EDB0A // pop edi; pop esi; pop ebx; mov esp,ebp; pop ebp; ret 4
#define SET_WORKER_MAX_RVA     0x2C8660 // CGdBuilding::SetWorkerMax(building, max)
#define SET_JOB_RVA            0x2DEE90 // CGdFigureJobs::SetJob(figure, job)
#define SET_TASK_RVA           0x2B7110 // CGdFigure::SetTask(figure, task)
#define EQUIP_ARMOUR_RVA       0x2F84C1 // CGdFigureToolBox::EquipArtisanArmour(figure)
#define EQUIP_TOOLS_RVA        0x2FD783 // CGdFigureToolBox::EquipArtisanTools(figure, right, left)
#define MOVE_FIGURE_RVA        0x302CD6 // CGdFigureToolBox::MoveFigure(figure, x | y << 16)

#define JOBS_BUILDINGS   0x0C
#define JOBS_FIGURES     0x20
#define JOBS_TOOLBOX     0x24
#define JOBS_PLAYERS     0x38

#define BUILDING_SIZE    0x34
#define BUILDING_POSITION 0x04
#define BUILDING_OWNER   0x08
#define BUILDING_TYPE    0x0A
#define BUILDING_RACE    0x0B
#define BUILDING_WORKERS 0x31
#define BUILDING_WORKERS_MAX 0x32

#define FIGURE_SIZE      0x2B3
#define FIGURE_BUILDING  0x14
#define FIGURE_FLAGS     0x16
#define FIGURE_OWNER     0x1C
#define FIGURE_JOB       0x20B

#define PLAYER_SIZE      0x188
#define PLAYER_USE       0x0A
#define PLAYER_AVATAR    0xB3

#define FLAG_NOT_ON_MAP  0x02     // only the first version of the patch set it on workers
#define FLAG_DEAD        0x08
#define FLAG_FREEZED     0x100
#define FLAG_AI_BLOCKED  0x10000
#define FLAG_TOWER       0x20000
#define FLAG_DEAD_OR_GONE 0x0A     // flags the game skips figures with (2, 8)
#define FROZEN_BY_MOD    (FLAG_FREEZED | FLAG_AI_BLOCKED)

#define JOB_SMITH_SEARCH_FOR_WORK 119
#define JOB_SMITH_WORK   120
#define TASK_FORGE       6
#define RACE_DWARF       3
#define RACE_ORC         4

#define MAX_FIGURES      2000
#define MAX_BUILDINGS    1000
#define MAX_PLAYERS      200

static const BYTE TowerTypes[] = { 0x0D, 0x20, 0x26, 0x27, 0x47, 0x54, 0x66, 0x67 };

static const BYTE UpdatePrologue[] = { 0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08 };          // push ebp; mov ebp,esp; sub esp,8
static const BYTE HabitablePrologue[] = { 0x55, 0x8B, 0xEC, 0x0F, 0xB7, 0x45, 0x08 }; // ...; movzx eax,word ptr [ebp+8]
static const BYTE EntrySwitch[] = { 0x8D, 0x41, 0xFF, 0x3D, 0x88, 0x00, 0x00, 0x00 }; // lea eax,[ecx-1]; cmp eax,88h

static const char DefaultConfig[] =
    "# TowerWorkers (.asi or .sfm) - towers and the White Hand of human players only work with a worker inside.\r\n"
    "# Inside=1: the worker goes into the tower (hidden by it); Inside=0: it stays at the tower's door.\r\n"
    "# In multiplayer every player must use the same setting.\r\n"
    "Inside=1\r\n";

typedef void (__fastcall *SetWorkerMaxFn)(BYTE* buildings, void* unused, DWORD building, DWORD max);
typedef void (__fastcall *SetJobFn)(BYTE* jobs, void* unused, DWORD figure, DWORD job);
typedef void (__fastcall *SetTaskFn)(BYTE* figures, void* unused, DWORD figure, DWORD task);
typedef void (__fastcall *EquipArmourFn)(BYTE* toolbox, void* unused, DWORD figure);
typedef void (__fastcall *EquipToolsFn)(BYTE* toolbox, void* unused, DWORD figure, DWORD right, DWORD left);
typedef void (__fastcall *MoveFigureFn)(BYTE* toolbox, void* unused, DWORD figure, DWORD position);

static BYTE* GameBase;
static SetWorkerMaxFn SetWorkerMax;
static SetJobFn SetJob;
static SetTaskFn SetTask;
static EquipArmourFn EquipArmour;
static EquipToolsFn EquipTools;
static MoveFigureFn MoveFigure;
static DWORD UpdateResume, HabitableResume, HabitableSingleResume, EntryResume, EntryReturn;
static BYTE HabitableJump[5], HabitableSingleJump[5], EntryJump[5]; // our patches, to see if another mod replaced them
static BOOL PatchesLost;
static BOOL Inside = TRUE;

static DWORD SyncCount;                 // ticks seen
static DWORD WorkerSeen[MAX_BUILDINGS]; // tick a worker of the tower was last seen
static DWORD FrozenByMod[MAX_FIGURES];   // flags the patch set on a tower unit (only these are taken away)
static BOOL Human[MAX_PLAYERS];

static char LogPath[MAX_PATH];

static void Log(const char* format, ...)
{
    FILE* file;
    va_list args;
    if (fopen_s(&file, LogPath, "a") != 0) return;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputs("\n", file);
    fclose(file);
}

static void ReadConfig(const char* path)
{
    char line[256];
    FILE* file;
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
        if (_stricmp(line, "Inside") == 0) Inside = atoi(value) != 0;
    }
    fclose(file);
}

static BOOL IsTowerType(DWORD type)
{
    int i;
    for (i = 0; i < sizeof(TowerTypes); i++)
    {
        if (TowerTypes[i] == type) return TRUE;
    }
    return FALSE;
}

static int __cdecl IsTowerTypeHook(DWORD type)
{
    return IsTowerType(type);
}

// A player who has an avatar is human; once seen, it stays human (the avatar may be dead for a while).
static BOOL IsHuman(BYTE* players, DWORD player)
{
    BYTE* record;
    if (player == 0 || player >= MAX_PLAYERS) return FALSE;
    record = players + player * PLAYER_SIZE;
    if (!Human[player] && record[PLAYER_USE] != 0 && *(WORD*)(record + PLAYER_AVATAR) != 0) Human[player] = TRUE;
    return Human[player];
}

/* ------------------------------------------------------------------------ */
/* Towers take workers                                                       */
/* ------------------------------------------------------------------------ */

// thiscall (ecx = CGdBuilding, building): 1 for the tower types, else the game's own answer
__declspec(naked) static void IsHabitableHook(void)
{
    __asm
    {
        movzx   eax, word ptr [esp+4]
        imul    eax, eax, BUILDING_SIZE
        movzx   eax, byte ptr [eax+ecx+BUILDING_TYPE]
        push    ecx
        push    eax
        call    IsTowerTypeHook
        add     esp, 4
        pop     ecx
        test    eax, eax
        jz      original
        mov     eax, 1
        ret     4
    original:
        push    ebp
        mov     ebp, esp
        movzx   eax, word ptr [ebp+8]
        jmp     dword ptr [HabitableResume]
    }
}

__declspec(naked) static void IsHabitableSingleHook(void)
{
    __asm
    {
        movzx   eax, word ptr [esp+4]
        imul    eax, eax, BUILDING_SIZE
        movzx   eax, byte ptr [eax+ecx+BUILDING_TYPE]
        push    ecx
        push    eax
        call    IsTowerTypeHook
        add     esp, 4
        pop     ecx
        test    eax, eax
        jz      original
        mov     eax, 1
        ret     4
    original:
        push    ebp
        mov     ebp, esp
        movzx   eax, word ptr [ebp+8]
        jmp     dword ptr [HabitableSingleResume]
    }
}

/* ------------------------------------------------------------------------ */
/* The worker arrives                                                        */
/* ------------------------------------------------------------------------ */

// TRUE when the building is a tower: the worker dresses like a smith at a forge and waits with the smith's job
static int __cdecl OnWorkerEnters(DWORD type, BYTE* jobs, DWORD figure, DWORD building)
{
    BYTE* toolbox = *(BYTE**)(jobs + JOBS_TOOLBOX);
    BYTE* figures = *(BYTE**)(jobs + JOBS_FIGURES);
    BYTE* buildings = *(BYTE**)(jobs + JOBS_BUILDINGS);
    BYTE race;

    figure &= 0xFFFF;
    building &= 0xFFFF;
    if (!IsTowerType(type)) return FALSE;
    race = buildings[building * BUILDING_SIZE + BUILDING_RACE];
    SetJob(jobs, NULL, figure, JOB_SMITH_SEARCH_FOR_WORK);
    EquipArmour(toolbox, NULL, figure);
    SetTask(figures, NULL, figure, TASK_FORGE);
    if (race == RACE_ORC) EquipTools(toolbox, NULL, figure, 0, 0x12);
    else if (race == RACE_DWARF) EquipTools(toolbox, NULL, figure, 0x10, 0);
    else EquipTools(toolbox, NULL, figure, 9, 0);

    if (Inside)
    {
        MoveFigure(toolbox, NULL, figure, *(DWORD*)(buildings + building * BUILDING_SIZE + BUILDING_POSITION));
        Log("worker %lu went into tower %lu (type %lu)", figure, building, type);
    }
    else
    {
        Log("worker %lu works at tower %lu (type %lu)", figure, building, type);
    }
    return TRUE;
}

// At 006EDAAC: ecx = building type, edi = CGdFigureJobs, si = figure, ebx = building
__declspec(naked) static void EntryHook(void)
{
    __asm
    {
        pushad
        push    ebx
        push    esi
        push    edi
        push    ecx
        call    OnWorkerEnters
        add     esp, 16
        mov     [esp+28], eax       // eax of pushad
        popad
        test    eax, eax
        jnz     handled
        lea     eax, [ecx-1]
        cmp     eax, 88h
        jmp     dword ptr [EntryResume]
    handled:
        jmp     dword ptr [EntryReturn]
    }
}

/* ------------------------------------------------------------------------ */
/* Once per tick: room for a worker, tower units frozen or not               */
/* ------------------------------------------------------------------------ */

static void ReleaseAll(BYTE* figures)
{
    int id;
    for (id = 1; id < MAX_FIGURES; id++)
    {
        if (FrozenByMod[id])
        {
            *(DWORD*)(figures + id * FIGURE_SIZE + FIGURE_FLAGS) &= ~FrozenByMod[id];
            FrozenByMod[id] = 0;
        }
    }
}

static void SyncTowers(BYTE* jobs)
{
    BYTE* buildings = *(BYTE**)(jobs + JOBS_BUILDINGS);
    BYTE* figures = *(BYTE**)(jobs + JOBS_FIGURES);
    BYTE* players = *(BYTE**)(jobs + JOBS_PLAYERS);
    DWORD count = *(WORD*)figures, id;

    if (PatchesLost) return;
    if (memcmp(GameBase + IS_HABITABLE_RVA, HabitableJump, 5) != 0 ||
        memcmp(GameBase + IS_HABITABLE_SINGLE_RVA, HabitableSingleJump, 5) != 0 ||
        memcmp(GameBase + ENTRY_SWITCH_RVA, EntryJump, 5) != 0)
    {
        // Another mod (SFSF) replaced these functions: towers could never get a worker, so leave them alone
        PatchesLost = TRUE;
        ReleaseAll(figures);
        Log("another mod replaced the worker functions (SFSF? use TowerWorkers.sfm then): towers work as before");
        return;
    }
    if (count >= MAX_FIGURES) count = MAX_FIGURES - 1;
    SyncCount++;

    // Which towers have their worker: a figure of the tower with the smith's job
    for (id = 1; id <= count; id++)
    {
        BYTE* figure = figures + id * FIGURE_SIZE;
        DWORD flags = *(DWORD*)(figure + FIGURE_FLAGS);
        DWORD building = *(WORD*)(figure + FIGURE_BUILDING);
        DWORD job = *(WORD*)(figure + FIGURE_JOB);

        if (*(WORD*)(figure + FIGURE_OWNER) == 0xFFFF || (flags & (FLAG_DEAD | FLAG_TOWER)) != 0 ||
            (job != JOB_SMITH_SEARCH_FOR_WORK && job != JOB_SMITH_WORK) || building == 0 || building >= MAX_BUILDINGS ||
            !IsTowerType(buildings[building * BUILDING_SIZE + BUILDING_TYPE]))
        {
            continue;
        }
        if (flags & FLAG_NOT_ON_MAP)
        {
            // Taken off the map by the first version of the patch: back on the map, inside the tower
            *(DWORD*)(figure + FIGURE_FLAGS) = flags & ~FLAG_NOT_ON_MAP;
            Log("worker %lu of tower %lu is back on the map", id, building);
        }
        WorkerSeen[building] = SyncCount;
    }

    for (id = 1; id <= count; id++)
    {
        BYTE* unit = figures + id * FIGURE_SIZE;
        DWORD flags = *(DWORD*)(unit + FIGURE_FLAGS);
        DWORD building = *(WORD*)(unit + FIGURE_BUILDING);
        BYTE* tower;
        BOOL frozen = FALSE;

        if ((flags & FLAG_TOWER) == 0 || (flags & FLAG_DEAD_OR_GONE) != 0 || *(WORD*)(unit + FIGURE_OWNER) == 0xFFFF)
        {
            FrozenByMod[id] = 0;
            continue;
        }
        if (building == 0 || building >= MAX_BUILDINGS) continue;
        tower = buildings + building * BUILDING_SIZE;

        if (IsTowerType(tower[BUILDING_TYPE]) && IsHuman(players, *(WORD*)(tower + BUILDING_OWNER)))
        {
            if (tower[BUILDING_WORKERS_MAX] < 1)
            {
                SetWorkerMax(buildings, NULL, building, 1);
                Log("tower %lu (type %u, player %u) takes a worker now", building, tower[BUILDING_TYPE],
                    *(WORD*)(tower + BUILDING_OWNER));
            }
            frozen = tower[BUILDING_WORKERS] == 0 || WorkerSeen[building] != SyncCount;
        }

        if (frozen && !FrozenByMod[id])
        {
            FrozenByMod[id] = FROZEN_BY_MOD & ~flags; // flags the game had not set itself
            *(DWORD*)(unit + FIGURE_FLAGS) = flags | FROZEN_BY_MOD;
            if (FrozenByMod[id]) Log("tower %lu has no worker: tower unit %lu stops", building, id);
        }
        else if (!frozen && FrozenByMod[id])
        {
            *(DWORD*)(unit + FIGURE_FLAGS) = flags & ~FrozenByMod[id];
            FrozenByMod[id] = 0;
            Log("tower %lu is manned: tower unit %lu works", building, id);
        }
    }
}

static void __cdecl SyncTowersHook(BYTE* jobs)
{
    SyncTowers(jobs);
}

// Start of CGdFigureJobs::Update (ecx = CGdFigureJobs); the moved prologue follows
__declspec(naked) static void UpdateHook(void)
{
    __asm
    {
        push    ecx
        push    ecx
        call    SyncTowersHook
        add     esp, 4
        pop     ecx
        push    ebp
        mov     ebp, esp
        sub     esp, 8
        jmp     dword ptr [UpdateResume]
    }
}

/* ------------------------------------------------------------------------ */
/* Patching                                                                  */
/* ------------------------------------------------------------------------ */

static void WriteJump(BYTE* at, void* to, int size, BYTE* written)
{
    DWORD oldProtect;
    BYTE code[8];
    int i;
    code[0] = 0xE9;
    *(LONG*)(code + 1) = (LONG)((BYTE*)to - (at + 5));
    for (i = 5; i < size; i++) code[i] = 0x90;
    VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(at, code, size);
    VirtualProtect(at, size, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), at, size);
    if (written) memcpy(written, code, 5);
}

static void ApplyPatch(HINSTANCE instance)
{
    BYTE* base = (BYTE*)GetModuleHandleA(NULL);
    char configPath[MAX_PATH];
    char* slash;

    GetModuleFileNameA(instance, LogPath, MAX_PATH);
    slash = strrchr(LogPath, '\\');
    slash = slash ? slash + 1 : LogPath;
    strcpy_s(slash, MAX_PATH - (slash - LogPath), "TowerWorkers.txt");
    strcpy_s(configPath, MAX_PATH, LogPath);
    strcpy_s(slash, MAX_PATH - (slash - LogPath), "TowerWorkers.log");
    DeleteFileA(LogPath);
    ReadConfig(configPath);

    if (!IsGameImageLargeEnough(base) || *(int*)(base + VERSION_RVA) != VERSION_VALUE)
    {
        Log("unsupported SpellForce.exe build, nothing patched");
        return;
    }
    if (memcmp(base + JOBS_UPDATE_RVA, UpdatePrologue, sizeof(UpdatePrologue)) != 0 ||
        memcmp(base + IS_HABITABLE_RVA, HabitablePrologue, sizeof(HabitablePrologue)) != 0 ||
        memcmp(base + IS_HABITABLE_SINGLE_RVA, HabitablePrologue, sizeof(HabitablePrologue)) != 0 ||
        memcmp(base + ENTRY_SWITCH_RVA, EntrySwitch, sizeof(EntrySwitch)) != 0)
    {
        Log("game code not as expected (another mod, SFSF? use TowerWorkers.sfm then), nothing patched");
        return;
    }
    GameBase = base;
    SetWorkerMax = (SetWorkerMaxFn)(base + SET_WORKER_MAX_RVA);
    SetJob = (SetJobFn)(base + SET_JOB_RVA);
    SetTask = (SetTaskFn)(base + SET_TASK_RVA);
    EquipArmour = (EquipArmourFn)(base + EQUIP_ARMOUR_RVA);
    EquipTools = (EquipToolsFn)(base + EQUIP_TOOLS_RVA);
    MoveFigure = (MoveFigureFn)(base + MOVE_FIGURE_RVA);
    UpdateResume = (DWORD)(base + JOBS_UPDATE_RVA + sizeof(UpdatePrologue));
    HabitableResume = (DWORD)(base + IS_HABITABLE_RVA + HABITABLE_RESUME);
    HabitableSingleResume = (DWORD)(base + IS_HABITABLE_SINGLE_RVA + HABITABLE_RESUME);
    EntryResume = (DWORD)(base + ENTRY_SWITCH_RESUME);
    EntryReturn = (DWORD)(base + ENTRY_RETURN_RVA);

    WriteJump(base + IS_HABITABLE_RVA, IsHabitableHook, sizeof(HabitablePrologue), HabitableJump);
    WriteJump(base + IS_HABITABLE_SINGLE_RVA, IsHabitableSingleHook, sizeof(HabitablePrologue), HabitableSingleJump);
    WriteJump(base + ENTRY_SWITCH_RVA, EntryHook, sizeof(EntrySwitch), EntryJump);
    WriteJump(base + JOBS_UPDATE_RVA, UpdateHook, sizeof(UpdatePrologue), NULL);
    Log("patched: towers of human players need a worker (Inside=%d)", Inside);
}
