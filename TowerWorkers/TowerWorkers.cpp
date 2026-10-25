/*
 * TowerWorkers - the defence towers and the White Hand of human players only work with a worker inside, the way an
 * armory needs its smith. Spellforce Spell Framework mod for SpellForce 1.61.11213 (needs SFSF: put
 * TowerWorkers.sfm into the game's "sfsf" folder; without SFSF use TowerWorkers.asi).
 *
 * Buildings (type, race): Crossbow Tower 0Dh and White Hand 20h (humans), Archer Tower 26h and Frostbringer 27h
 * (elves), Firestarter 47h (orcs), Stone Thrower 54h (trolls), Tower of Sorcery 66h and Mindbreaker 67h (dark
 * elves). Dwarves have no towers.
 *
 * How the game works (addresses with the default image base):
 *   - A finished tower gets a tower unit (SFSF done handlers, CGdBuildingToolbox::AttachTowerUnit 006D9290): a
 *     figure with the TOWER flag (20000h) on the building (figure building = the tower). The unit does the shooting
 *     and, for the White Hand, the healing.
 *   - Figure jobs run in CGdFigureJobs::Update (006F14B0), which skips figures with the FREEZED flag (100h).
 *   - A worker inside a building stands within it: kGdJobEnterBuilding (68) moves it there, and when it leaves (the
 *     player takes it out, the building is destroyed: the smith's job ends in 006E60F0) the game moves it to a free
 *     place next to the building. Moving a figure: CGdFigureToolBox 00702CD6(figure, x | y << 16). Only figures
 *     with the TOWER flag are drawn on top of the tower (render flag, 00907E60).
 *   - Workers: the most workers of a building is CGdBuilding::SetWorkerMax (006C8660, from the building type's data:
 *     0 for towers); SFSF decides with the building tags HABITABLE / HABITABLE_SINGLE whether a building takes
 *     workers, and calls the building type's entry handler when the worker starts working there.
 *
 * The mod:
 *   - adds HABITABLE and HABITABLE_SINGLE to the towers' tags (wrapping SFSF's getBuildingTags);
 *   - gives the towers of human players room for 1 worker (also towers built before the mod was installed);
 *   - registers an entry handler for the towers: the worker dresses and takes the tools like a smith at a forge and
 *     takes the smith's job; with Inside=1 (TowerWorkers.txt next to the .sfm) it goes into the middle of the tower
 *     (the building's position), where the tower hides it; taking it out (building window, worker icon) and a
 *     destroyed tower work as for a forge;
 *   - once per game tick (start of CGdFigureJobs::Update) freezes the tower unit of every human player's tower
 *     without its worker (a figure of the tower with the smith's job) (FREEZED | AI_BLOCKED), and unfreezes it when
 *     the worker is there. Only the flags the mod set are taken away again. A worker the first version took off the
 *     map (flag 2) comes back.
 * A human player is a player who has (or had) an avatar; towers of the computer players work as before. The rule
 * depends only on the game state, so in multiplayer every player must use the mod (with the same settings).
 *
 * Build with 32-bit MinGW g++ (the SFSF headers need GCC): see build.bat.
 */

#include "sfsf.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

SpellforceSpellFramework *sfsf;
SFLog *logAPI;

#define VERSION_RVA          0x177e63 // Int32 11213 in build 1.61.11213
#define VERSION_VALUE        11213
#define JOBS_UPDATE_RVA      0x2f14b0 // CGdFigureJobs::Update
#define SET_WORKER_MAX_RVA   0x2c8660 // CGdBuilding::SetWorkerMax(building, max)
#define MOVE_FIGURE_RVA      0x302cd6 // CGdFigureToolBox::MoveFigure(figure, x | y << 16)

#define FLAG_NOT_ON_MAP      0x02     // only the first version of the mod set it on workers
#define FLAG_DEAD            0x08
#define FLAG_FREEZED         0x100
#define FLAG_AI_BLOCKED      0x10000
#define FLAG_TOWER           0x20000
#define FLAG_DEAD_OR_GONE    0x0a     // flags the game skips figures with (2, 8)
#define FROZEN_BY_MOD        (FLAG_FREEZED | FLAG_AI_BLOCKED)

#define RACE_DWARF           3
#define RACE_ORC             4

#define MAX_FIGURES          2000
#define MAX_BUILDINGS        1000
#define MAX_PLAYERS          200

static const uint8_t TOWER_TYPES[] = {0x0d, 0x20, 0x26, 0x27, 0x47, 0x54, 0x66, 0x67};

static const char DEFAULT_CONFIG[] =
    "# TowerWorkers (.asi or .sfm) - towers and the White Hand of human players only work with a worker inside.\r\n"
    "# Inside=1: the worker goes into the tower (hidden by it); Inside=0: it stays at the tower's door.\r\n"
    "# In multiplayer every player must use the same setting.\r\n"
    "Inside=1\r\n";

typedef void(__thiscall *set_worker_max_ptr)(SF_CGdBuilding *buildings, uint16_t building, uint8_t max);
typedef void(__thiscall *move_figure_ptr)(SF_CGdFigureToolbox *toolbox, uint16_t figure, uint32_t position);

static uintptr_t s_base;
static set_worker_max_ptr s_set_worker_max;
static move_figure_ptr s_move_figure;
static getBuildingTags_ptr s_original_get_tags;
static uint32_t s_jobs_update_resume;
static bool s_inside = true;

static uint32_t s_sync_count;                  // ticks seen
static uint32_t s_worker_seen[MAX_BUILDINGS];  // tick a worker of the tower was last seen
static uint32_t s_frozen_by_mod[MAX_FIGURES];  // flags the mod set on a tower unit (only these are taken away)
static bool s_human[MAX_PLAYERS];

static bool is_tower_type(uint8_t type)
{
    for (size_t i = 0; i < sizeof(TOWER_TYPES); i++)
    {
        if (TOWER_TYPES[i] == type)
        {
            return true;
        }
    }
    return false;
}

static bool is_smith_job(uint16_t job)
{
    return job == kGdJobSmithSearchForWork || job == kGdJobSmithWork;
}

// A player who has an avatar is human; once seen, it stays human (the avatar may be dead for a while).
static bool is_human(SF_CGdPlayer *players, uint16_t player)
{
    if (player == 0 || player >= MAX_PLAYERS)
    {
        return false;
    }
    if (!s_human[player] && players->players[player].use != 0 && players->players[player].avatar_figure_index != 0)
    {
        s_human[player] = true;
    }
    return s_human[player];
}

/* ------------------------------------------------------------------------ */
/* Tags: towers take workers                                                 */
/* ------------------------------------------------------------------------ */

static uint32_t __thiscall get_building_tags_hook(uint8_t building_type)
{
    uint32_t tags = s_original_get_tags(building_type);
    if (is_tower_type(building_type))
    {
        tags |= BuildingTag::HABITABLE_BUILDING | BuildingTag::HABITABLE_SINGLE_BUILDING;
    }
    return tags;
}

/* ------------------------------------------------------------------------ */
/* The worker arrives                                                        */
/* ------------------------------------------------------------------------ */

static void __thiscall tower_entry_handler(SF_CGdFigureJobs *jobs, uint16_t figure, uint16_t building)
{
    uint8_t race = jobs->CGdBuilding->buildings[building].race;

    // Like a smith at a forge: the smith's job (with nothing to forge the worker waits), clothes, task and tools
    sfsf->figureAPI->setJob(jobs, figure, kGdJobSmithSearchForWork);
    sfsf->toolboxAPI->equipArtisanArmour(jobs->CGdFigureToolBox, figure);
    sfsf->figureAPI->setTask(jobs->CGdFigure, figure, CGdFigureTask::TASK_FORGE);
    switch (race)
    {
        case RACE_ORC:
            sfsf->toolboxAPI->equipArtisanTools(jobs->CGdFigureToolBox, figure, 0, 0x12);
            break;
        case RACE_DWARF:
            sfsf->toolboxAPI->equipArtisanTools(jobs->CGdFigureToolBox, figure, 0x10, 0);
            break;
        default:
            sfsf->toolboxAPI->equipArtisanTools(jobs->CGdFigureToolBox, figure, 9, 0);
            break;
    }

    if (s_inside)
    {
        // The building's position (the middle of the tower)
        s_move_figure(jobs->CGdFigureToolBox, figure, *(uint32_t *)&jobs->CGdBuilding->buildings[building].position);
        logAPI->logInfo("[TowerWorkers] worker %u went into tower %u (type %u)", figure, building,
                        jobs->CGdBuilding->buildings[building].type);
    }
    else
    {
        logAPI->logInfo("[TowerWorkers] worker %u works at tower %u (type %u)", figure, building,
                        jobs->CGdBuilding->buildings[building].type);
    }
}

/* ------------------------------------------------------------------------ */
/* Once per tick: room for a worker, tower units frozen or not               */
/* ------------------------------------------------------------------------ */

static void sync_towers(SF_CGdFigureJobs *jobs)
{
    SF_CGdFigure *figures = jobs->CGdFigure;
    SF_CGdBuilding *buildings = jobs->CGdBuilding;
    SF_CGdPlayer *players = jobs->CGdPlayer;
    uint16_t count = figures->max_used;

    if (count >= MAX_FIGURES)
    {
        count = MAX_FIGURES - 1;
    }
    s_sync_count++;

    // Which towers have their worker: a figure of the tower with the smith's job
    for (uint16_t id = 1; id <= count; id++)
    {
        GdFigure *figure = &figures->figures[id];
        uint32_t flags = figure->flags;
        uint16_t building = figure->building;

        if (figure->owner == 0xffff || (flags & (FLAG_DEAD | FLAG_TOWER)) != 0 ||
            !is_smith_job(figure->current_job.GdJobId) || building == 0 || building >= MAX_BUILDINGS ||
            !is_tower_type(buildings->buildings[building].type))
        {
            continue;
        }
        if (flags & FLAG_NOT_ON_MAP)
        {
            // Taken off the map by the first version of the mod: back on the map, inside the tower
            figure->flags = flags & ~FLAG_NOT_ON_MAP;
            logAPI->logInfo("[TowerWorkers] worker %u of tower %u is back on the map", id, building);
        }
        s_worker_seen[building] = s_sync_count;
    }

    for (uint16_t id = 1; id <= count; id++)
    {
        GdFigure *unit = &figures->figures[id];
        uint32_t flags = unit->flags;
        uint16_t building = unit->building;

        if ((flags & FLAG_TOWER) == 0 || (flags & FLAG_DEAD_OR_GONE) != 0 || unit->owner == 0xffff)
        {
            s_frozen_by_mod[id] = 0;
            continue;
        }
        if (building == 0 || building >= MAX_BUILDINGS)
        {
            continue;
        }
        GdBuilding *tower = &buildings->buildings[building];
        bool frozen = false;

        if (is_tower_type(tower->type) && is_human(players, tower->owner))
        {
            if (tower->worker_count_max < 1)
            {
                s_set_worker_max(buildings, building, 1);
                logAPI->logInfo("[TowerWorkers] tower %u (type %u, player %u) takes a worker now", building,
                                tower->type, tower->owner);
            }
            frozen = tower->worker_count == 0 || s_worker_seen[building] != s_sync_count;
        }

        if (frozen && !s_frozen_by_mod[id])
        {
            s_frozen_by_mod[id] = FROZEN_BY_MOD & ~flags; // flags the game had not set itself
            unit->flags = flags | FROZEN_BY_MOD;
            if (s_frozen_by_mod[id])
            {
                logAPI->logInfo("[TowerWorkers] tower %u has no worker: tower unit %u stops", building, id);
            }
        }
        else if (!frozen && s_frozen_by_mod[id])
        {
            unit->flags = flags & ~s_frozen_by_mod[id];
            s_frozen_by_mod[id] = 0;
            logAPI->logInfo("[TowerWorkers] tower %u is manned: tower unit %u works", building, id);
        }
    }
}

static void __cdecl jobs_update_hook_body(SF_CGdFigureJobs *jobs)
{
    sync_towers(jobs);
}

/* ------------------------------------------------------------------------ */
/* Module entry points                                                       */
/* ------------------------------------------------------------------------ */

static void *s_body_pointer = (void *)&jobs_update_hook_body;

static bool write_code(uintptr_t address, const void *data, size_t size)
{
    DWORD old_protect;
    if (!VirtualProtect((void *)address, size, PAGE_EXECUTE_READWRITE, &old_protect))
    {
        return false;
    }
    memcpy((void *)address, data, size);
    VirtualProtect((void *)address, size, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), (void *)address, size);
    return true;
}

// The hook is assembled at run time: push ecx; push ecx; call [body]; add esp,4; pop ecx; push ebp; mov ebp,esp;
// sub esp,8; jmp [resume]
static uint8_t *build_jobs_update_stub()
{
    uint8_t *code = (uint8_t *)VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!code)
    {
        return NULL;
    }
    uint8_t *p = code;
    *p++ = 0x51;                                  // push ecx
    *p++ = 0x51;                                  // push ecx
    *p++ = 0xff; *p++ = 0x15;                     // call [s_body_pointer]
    *(uint32_t *)p = (uint32_t)&s_body_pointer; p += 4;
    *p++ = 0x83; *p++ = 0xc4; *p++ = 0x04;        // add esp, 4
    *p++ = 0x59;                                  // pop ecx
    *p++ = 0x55;                                  // push ebp
    *p++ = 0x8b; *p++ = 0xec;                     // mov ebp, esp
    *p++ = 0x83; *p++ = 0xec; *p++ = 0x08;        // sub esp, 8
    *p++ = 0xff; *p++ = 0x25;                     // jmp [s_jobs_update_resume]
    *(uint32_t *)p = (uint32_t)&s_jobs_update_resume;
    return code;
}

static bool is_game_image(uintptr_t base)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    return dos->e_magic == IMAGE_DOS_SIGNATURE && nt->OptionalHeader.SizeOfImage > 0x400000;
}

// TowerWorkers.txt next to the .sfm (written with the defaults when missing)
static void read_config()
{
    char path[MAX_PATH];
    char line[256];
    HMODULE self = NULL;
    FILE *file;

    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)&read_config, &self) ||
        GetModuleFileNameA(self, path, MAX_PATH) == 0)
    {
        return;
    }
    char *slash = strrchr(path, '\\');
    if (slash == NULL || (size_t)(slash + 1 - path) + sizeof("TowerWorkers.txt") > MAX_PATH)
    {
        return;
    }
    strcpy(slash + 1, "TowerWorkers.txt");
    file = fopen(path, "r");
    if (file == NULL)
    {
        file = fopen(path, "wb");
        if (file != NULL)
        {
            fwrite(DEFAULT_CONFIG, 1, sizeof(DEFAULT_CONFIG) - 1, file);
            fclose(file);
        }
        return;
    }
    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *value = strchr(line, '=');
        if (line[0] == '#' || value == NULL)
        {
            continue;
        }
        *value++ = '\0';
        if (_stricmp(line, "Inside") == 0)
        {
            s_inside = atoi(value) != 0;
        }
    }
    fclose(file);
}

extern "C" __declspec(dllexport) void InitModule(SpellforceSpellFramework *framework)
{
    static const uint8_t update_prologue[] = {0x55, 0x8b, 0xec, 0x83, 0xec, 0x08}; // push ebp; mov ebp,esp; sub esp,8

    sfsf = framework;
    logAPI = sfsf->logAPI;
    s_base = (uintptr_t)GetModuleHandleA(NULL);
    if (!is_game_image(s_base) || *(int *)(s_base + VERSION_RVA) != VERSION_VALUE)
    {
        logAPI->logError("[TowerWorkers] unsupported SpellForce.exe build, nothing changed");
        return;
    }
    uint8_t *update = (uint8_t *)(s_base + JOBS_UPDATE_RVA);
    if (memcmp(update, update_prologue, sizeof(update_prologue)) != 0)
    {
        logAPI->logError("[TowerWorkers] CGdFigureJobs::Update is not as expected (another mod?), nothing changed");
        return;
    }
    uint8_t *stub = build_jobs_update_stub();
    if (!stub)
    {
        return;
    }
    read_config();
    s_set_worker_max = (set_worker_max_ptr)(s_base + SET_WORKER_MAX_RVA);
    s_move_figure = (move_figure_ptr)(s_base + MOVE_FIGURE_RVA);

    // Towers take workers
    s_original_get_tags = sfsf->buildingAPI->getBuildingTags;
    sfsf->buildingAPI->getBuildingTags = &get_building_tags_hook;

    // The worker's arrival
    for (size_t i = 0; i < sizeof(TOWER_TYPES); i++)
    {
        SFBuilding *tower = sfsf->registrationAPI->registerBuilding(TOWER_TYPES[i]);
        sfsf->registrationAPI->linkBuildingEntryHandler(tower, &tower_entry_handler);
    }

    // Once per tick
    s_jobs_update_resume = (uint32_t)(update + sizeof(update_prologue));
    uint8_t jump[sizeof(update_prologue)] = {0xe9, 0, 0, 0, 0, 0x90};
    *(int32_t *)(jump + 1) = (int32_t)((uintptr_t)stub - ((uintptr_t)update + 5));
    write_code((uintptr_t)update, jump, sizeof(jump));

    logAPI->logInfo("[TowerWorkers] towers of human players need a worker (Inside=%d)", s_inside ? 1 : 0);
}

extern "C" __declspec(dllexport) SFMod *RegisterMod(SpellforceSpellFramework *framework)
{
    return framework->createModInfo("TowerWorkers", "1.2.0", "Somka",
                                    "Towers and the White Hand of human players only work with a worker inside.");
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
