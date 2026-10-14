#pragma once
// ResourceGain - shows in front of every resource amount on the resource bar how much of that resource came in
// during the last minute ("+120 3450"), and replaces the hint of each resource (shown while the mouse is over its
// icon or amount) with that minute's income per building type ("Kopalnia (ludzie): +50"). SpellForce 1.61.11213
// only.
//
// Resource bar: CUiMain refreshes it on a timer in CUiMain::UpdateResources (009F2BE0 with the default image
// base). It copies the player's 7 resource amounts into a local array ([ebp-4Ch] + 8 * i: the amount of the light
// races, group 0, then of the dark races, group 1: trolls, orcs, dark elves; the bar shows the one the player has),
// then for each resource i (in edi) formats the text and passes it to the label [CUiMain+7944h+4*i] with CMnuLabel::SetText(CUtlString*) (0092FAB0) at 009F2E77; CUiMain itself is at
// [ebp-9Ch]. The patch redirects that one call: it adds up every increase of each amount over the last 60
// seconds (spending is not subtracted) and calls SetText with a green "+N " in front of the game's own text.
//
// Income per building: every resource the game hands out goes through
// CGdResource::Add(player, group, type, amount) (006A2A80, 33 calls). The patch redirects those calls to a stub
// that notes who got what and where it came from, then continues into the original function:
//   - calls in the worker job handlers (006E0000-006F0000) run inside the job loop CGdFigureJobs::Update
//     (006F14B0), which goes through the figures with the figure index in esi and itself in edi; a hook at the top
//     of the loop (006F14F0) remembers both. The income is credited to the worker's building
//     (figure record +14h, CGdFigure at [jobs+20h], records of 2B3h bytes) - its building type
//     (CGdBuilding at [jobs+0Ch], records of 34h bytes from +4, type at +6);
//   - the two calls in the building code (006D8558, 006D88B9) have the building as ebx = index * 34h and
//     CGdBuilding at [esi+0Ch];
//     A worker of the player without a building of the player (one carrying to the monument) counts as
//     "monument";
//   - anything else (resources picked up from objects, given by other players, cheats) counts as "other".
// The bar shows resource types 1, 2, 7, 4, 12h, 13h, 5 (in that order) for the local player. Add's group is the
// side of the race that brings the resource in (0 for humans, dwarves and elves, 1 for trolls, orcs and dark
// elves: 006ACB70 of the worker's race), so the hint counts both groups.
// Shared resources (the SharedResources mod, or SpellForce_shared_11213.exe): Add ignores its player and always
// adds to players 1, 2 and 3 (it starts with "mov edx, 62h" instead of "imul edx, player, 62h"), so everyone's
// income lands on the local player's bar. Then the hint counts the income of every player, per building type (an
// ally's woodcutter's hut shows as one too). This is looked at each time, as that mod may load after this one.
// Local player: word [[CUiMain+743Ch]+8CCh]. Building names are the game's own texts (text id of the building type from
// GameData.cff category 2029, looked up with 00975740 on [CUiMain+7440h]), so they come in the game's language.
// The hint is the control's help text (vtable +8Ch, kept at +19Ch), set on the icon [CUiMain+7928h+4*i] and the
// label; with no income in the last minute it goes back to the game's own hint (00974A10, help id table
// 00C06AA4).
//
// CUtlString is the game's UTF-16 string { wchar_t* data; int length; ... } (16 bytes); new strings are built
// with the game's own constructor (00783890) and destructor (007839C0).
//
// This changes only what the local player sees, so it is safe in multiplayer.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include <stdio.h>

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

#define SET_TEXT_CALL_RVA  0x5F2E77 // call CMnuLabel::SetText in CUiMain::UpdateResources
#define SET_TEXT_RVA       0x52FAB0
#define STRING_CTOR_RVA    0x383890 // CUtlString::CUtlString(const wchar_t*)
#define STRING_EMPTY_RVA   0x383900 // CUtlString::CUtlString()
#define STRING_DTOR_RVA    0x3839C0 // CUtlString::~CUtlString()
#define ADD_RESOURCE_RVA   0x2A2A80 // CGdResource::Add(player, group, type, amount)
#define JOB_LOOP_RVA       0x2F14F0 // top of the figure loop in CGdFigureJobs::Update
#define JOB_HANDLERS_START 0x2E0000 // return addresses of the Add calls in the worker job handlers
#define JOB_HANDLERS_END   0x2F0000
#define GET_TEXT_RVA       0x575740 // (text db, text id, CUtlString* out): GameData.cff texts
#define GET_HELP_TEXT_RVA  0x574A10 // (text db, CUtlString* out, help id, 1) -> out
#define HELP_IDS_RVA       0x806AA4 // help id of each resource of the bar (bytes)

#define RESOURCE_COUNT     7
#define AMOUNTS_IN_FRAME   (-0x4C) // ebp-relative: the 7 (light races, dark races) amount pairs of UpdateResources
#define MAIN_IN_FRAME      (-0x9C) // ebp-relative: CUiMain
#define MAIN_TEXT_DB       0x7440
#define MAIN_PLAYER_INFO   0x743C  // word [this+8CCh] = local player
#define MAIN_ICONS         0x7928
#define MAIN_LABELS        0x7944
#define CONTROL_SET_HELP   0x8C    // vtable offset of SetHelpText(CUtlString*)
#define WINDOW_SECONDS     60
#define GAIN_COLOR         0x66FF66 // light green, entry 1Ah of the game's colour table (00C0A468)
#define RESTART_GAP_MS     10000   // no refresh for this long (loading, menus): start counting again

#define JOBS_BUILDINGS     0x0C    // CGdFigureJobs: CGdBuilding*
#define JOBS_FIGURES       0x20    // CGdFigureJobs: CGdFigure*
#define FIGURE_SIZE        0x2B3
#define FIGURE_BUILDING    0x14    // from CGdFigure + index * FIGURE_SIZE
#define FIGURE_OWNER       0x1C
#define BUILDING_SIZE      0x34
#define BUILDING_OWNER     0x08    // from CGdBuilding + index * BUILDING_SIZE
#define BUILDING_TYPE      0x0A
#define BUILDING_COUNT     1000

#define SOURCE_OTHER       0xFFFF
#define SOURCE_MONUMENT    0xFFFE
#define MAX_EVENTS         8192
#define MAX_SOURCES        32
#define NAME_LENGTH        64

static const BYTE SetTextCallOriginal[] = { 0x8B, 0x09, 0xE8 }; // mov ecx,[ecx] ; call rel32
static const BYTE SharedAddStart[] = { 0xBA, 0x62, 0x00, 0x00, 0x00 }; // Add+3 in the shared resources exe
static const BYTE JobLoopOriginal[] = { 0x0F, 0xB7, 0xC6, 0x69, 0xD8, 0xB3, 0x02, 0x00, 0x00 }; // movzx eax,si ; imul ebx,eax,2B3h

// Resource type and group of each resource of the bar, in bar order
static const BYTE BarTypes[RESOURCE_COUNT] = { 1, 2, 7, 4, 0x12, 0x13, 5 };

// Calls of CGdResource::Add (RVAs)
static const DWORD AddResourceCalls[] =
{
    0x2D8558, 0x2D88B9, 0x2E6B15, 0x2E6C5B, 0x2E7922, 0x2E7B5C, 0x2E8052, 0x2E8BE2, 0x2E9674, 0x2EA045, 0x2EA972,
    0x2EB5E2, 0x2EC094, 0x2ECE07, 0x2ED6F7, 0x2EEB9E, 0x2EF89E, 0x3028CA, 0x3275D2, 0x355B37, 0x355B49, 0x355B5B,
    0x355B6D, 0x355B7F, 0x355B91, 0x355BA3, 0x355BB5, 0x355BC7, 0x355BD9, 0x355BEB, 0x355BFD, 0x355C0F, 0x355C21,
};
// Return addresses (RVAs) of the two calls in the building code, which have the building in ebx
#define BUILDING_CALL_RETURN_1 0x2D855D
#define BUILDING_CALL_RETURN_2 0x2D88BE

// Building types (GameData.cff category 2029): id, race, text id of the name
static const struct { BYTE Id; BYTE Race; WORD NameText; } BuildingTypes[] =
{
    { 1, 1, 11873 }, // Medium Headquarters
    { 2, 1, 192 }, // Small Headquarters
    { 3, 1, 193 }, // Large Headquarters
    { 4, 1, 194 }, // Woodcutter's Hut
    { 5, 1, 196 }, // Sawmill
    { 6, 1, 195 }, // Cattle Breeding
    { 7, 1, 197 }, // Quarry
    { 8, 1, 198 }, // Food Store
    { 9, 1, 199 }, // Hunting Lodge
    { 10, 1, 200 }, // Fisher's Hut
    { 11, 1, 201 }, // Fishing Guild
    { 12, 1, 202 }, // Grainfarm
    { 13, 1, 203 }, // Crossbow Tower
    { 14, 1, 204 }, // Mine
    { 15, 1, 206 }, // Smelting Works
    { 16, 1, 207 }, // Ariashrine
    { 17, 1, 208 }, // Temple of Light
    { 18, 1, 12380 }, // Academy
    { 19, 1, 12381 }, // Master Armory
    { 20, 3, 12346 }, // Small Headquarters
    { 21, 3, 212 }, // Medium Headquarters
    { 22, 3, 213 }, // Large Headquarters
    { 23, 3, 214 }, // Forester
    { 24, 3, 215 }, // Master Woodcutter
    { 25, 3, 12344 }, // Hall of the Ancients
    { 26, 3, 217 }, // Food Store
    { 27, 3, 11678 }, // Lenyagatherer
    { 28, 3, 219 }, // Woodcutter's Hut
    { 29, 3, 222 }, // Hunting Lodge
    { 30, 3, 12345 }, // Iron Armory
    { 31, 3, 221 }, // Hunters Guild
    { 32, 1, 223 }, // White Hand
    { 33, 3, 224 }, // Gathererhut
    { 34, 3, 225 }, // Armory
    { 36, 3, 11679 }, // Ailantery
    { 37, 3, 11680 }, // Elf Druid Guild
    { 38, 3, 229 }, // Archer Tower
    { 39, 3, 230 }, // Frostbringer
    { 40, 3, 13019 }, // Archery Hall
    { 41, 2, 12392 }, // Small Headquarters
    { 42, 2, 12394 }, // Medium Headquarters
    { 43, 2, 12393 }, // Large Headquarters
    { 44, 2, 12373 }, // Quarry
    { 45, 2, 12365 }, // Great Armory
    { 46, 2, 12389 }, // Stonemason's Hut
    { 47, 2, 12388 }, // Food Store
    { 48, 2, 12306 }, // Pigfarm
    { 49, 2, 12391 }, // Mine
    { 51, 2, 13020 }, // Moonsilver Mine
    { 52, 2, 12390 }, // Smelting Works
    { 53, 2, 12375 }, // Forge
    { 55, 3, 1364 }, // Sawmill
    { 56, 6, 12355 }, // Moonsilver Armory
    { 57, 5, 12395 }, // Hall of Glory
    { 58, 5, 249 }, // Small Headquarters
    { 59, 5, 250 }, // Medium Headquarters
    { 60, 5, 251 }, // Large Headquarters
    { 61, 5, 252 }, // Woodcutter's Hut
    { 62, 5, 253 }, // Smelting Works
    { 63, 5, 254 }, // Food Store
    { 64, 5, 255 }, // Hunting Lodge
    { 65, 5, 256 }, // Fisher's Hut
    { 66, 5, 257 }, // Boar Breeding
    { 67, 5, 258 }, // Mine
    { 68, 5, 259 }, // Forge
    { 69, 5, 11713 }, // Croptent
    { 70, 5, 261 }, // Shaman's Hut
    { 71, 5, 262 }, // Firestarter
    { 72, 5, 12396 }, // Blood Shrine
    { 73, 4, 264 }, // Small Headquarters
    { 74, 4, 267 }, // Medium Headquarters
    { 75, 1, 205 }, // Forge
    { 76, 4, 265 }, // Large Headquarters
    { 77, 4, 268 }, // Woodcutter's Hut
    { 78, 4, 269 }, // Quarry
    { 79, 4, 270 }, // Food Store
    { 80, 4, 271 }, // Hunting Lodge
    { 81, 4, 12406 }, // Stonecutter
    { 82, 4, 274 }, // Corpse Collector
    { 83, 4, 276 }, // Mace Carver
    { 84, 4, 275 }, // Stone Thrower
    { 85, 6, 277 }, // Small Headquarters
    { 86, 6, 278 }, // Medium Headquarters
    { 87, 6, 279 }, // Large Headquarters
    { 88, 6, 280 }, // Quarry
    { 89, 6, 281 }, // Moonsilver Mine
    { 90, 6, 282 }, // Stonemason
    { 91, 6, 283 }, // Food Store
    { 92, 6, 284 }, // Fungus Farm
    { 93, 6, 285 }, // Breeder
    { 94, 6, 286 }, // Smelting Works
    { 95, 6, 287 }, // Mine
    { 96, 6, 288 }, // Forge
    { 98, 6, 12353 }, // Arkanum
    { 99, 6, 291 }, // Onyxshrine
    { 100, 6, 293 }, // Onyx Tower
    { 102, 6, 12332 }, // Tower of Sorcery
    { 103, 6, 12333 }, // Mindbreaker
    { 104, 6, 12354 }, // Dark Academy
    { 106, 105, 4759 }, // Small Headquarters
    { 107, 105, 4760 }, // Medium Headquarters
    { 108, 105, 4761 }, // Large Headquarters
    { 109, 105, 4775 }, // Boar Breeding
    { 110, 105, 4776 }, // Fisher's Hut
    { 111, 105, 4777 }, // Food Store
    { 112, 105, 4778 }, // Forge
    { 113, 105, 4779 }, // Mine
    { 114, 105, 4780 }, // Woodcutter's Hut
    { 115, 105, 8369 }, // Small Hut
    { 116, 105, 8370 }, // Large Hut
    { 117, 140, 8885 }, // Large Headquarters
    { 118, 140, 8886 }, // Medium Headquarters
    { 119, 140, 8887 }, // Small Headquarters
    { 120, 140, 8888 }, // Stone Thrower
    { 121, 140, 8889 }, // Food Store
    { 122, 140, 8890 }, // Quarry
    { 123, 150, 18493 }, // Hostile Crossbow Tower
    { 124, 139, 11928 }, // Wulfgar Crossbow Tower
    { 125, 140, 11930 }, // Mace Carver
    { 126, 140, 11931 }, // Hunting Lodge
    { 127, 140, 11933 }, // Corpse Collector
    { 128, 150, 12414 }, // Forge
    { 129, 150, 11955 }, // Large Headquarters
    { 130, 150, 11956 }, // Medium Headquarters
    { 131, 150, 11957 }, // Smelting Works
    { 132, 150, 11958 }, // Food Store
    { 133, 6, 12356 }, // Shadowforge
    { 134, 2, 12366 }, // Sanctuary
    { 135, 2, 13021 }, // Moonsilver Hall
    { 136, 1, 13018 }, // Shooting Range
    { 137, 5, 12397 }, // Mace Carver
    { 138, 5, 12398 }, // Dark Forge
    { 139, 4, 12407 }, // Ironhouse
    { 140, 4, 12408 }, // Blacksmith
    { 141, 137, 12411 }, // Small headquarters
    { 142, 137, 12412 }, // Forge
    { 143, 137, 12413 }, // Onyx Shrine
    { 144, 137, 12415 }, // Food Store
    { 145, 137, 12416 }, // Large Headquarters
    { 146, 139, 23714 }, // Wulfgar Crossbow´Tower
    { 147, 105, 18518 }, // Firestarter
    { 148, 114, 19668 }, // Grave
    { 149, 114, 21420 }, // Bone Tower
    { 150, 121, 22135 }, // Demon Gate
    { 151, 150, 22999 }, // Hostile White Hand
    { 152, 162, 24210 }, // Eyrie
    { 153, 114, 24211 }, // Crypt
    { 154, 178, 24212 }, // Elemental Node Air
    { 155, 178, 24213 }, // Elemental Node Earth
    { 156, 178, 24214 }, // Elemental Node Fire
    { 157, 178, 24215 }, // Elemental Node Ice
    { 158, 122, 24216 }, // Basilisk's Lair
    { 159, 124, 24217 }, // Blade Nexus
    { 160, 124, 25837 }, // Black Tower
    { 161, 124, 25838 }, // Spitflower
    { 162, 163, 25972 }, // Academy
    { 163, 163, 26034 }, // Frostbringer
    { 164, 124, 26809 }, // Damaged Black Tower
    { 165, 183, 27196 }, // Dark Crystal
    { 166, 183, 27198 }, // Dark Mananode
    { 167, 137, 12333 }, // Mindbreaker
    { 168, 135, 28126 }, // Holy Statue
    { 169, 137, 28479 }, // Sun Tower
    { 170, 137, 28481 }, // Moon Tower
    { 171, 137, 28618 }, // Darkelf Towngate
    { 172, 124, 30729 }, // Great Spitflower
    { 173, 137, 212 }, // Medium Headquarters
    { 174, 109, 36268 }, // Small Spider Nest
    { 175, 109, 36269 }, // Medium Spider Nest
    { 176, 109, 36270 }, // Large Spider Nest
    { 177, 137, 12353 }, // Arkanum
    { 178, 137, 12332 }, // Tower of Sorcery
    { 179, 137, 204 }, // Mine
    { 180, 137, 281 }, // Moonsilver Mine
    { 181, 137, 12355 }, // Moonsilver Armory
    { 182, 137, 293 }, // Onyx Tower
    { 183, 137, 284 }, // Fungus Farm
    { 184, 137, 12356 }, // Shadowforge
    { 185, 137, 206 }, // Smelting Works
    { 186, 137, 12354 }, // Dark Academy
    { 187, 137, 197 }, // Quarry
    { 188, 137, 282 }, // Stonemason
    { 189, 137, 285 }, // Breeder
    { 190, 188, 36591 }, // Hideout
    { 191, 188, 36592 }, // Hideout
    { 192, 105, 37598 }, // Desert Tent
    { 193, 105, 37598 }, // Desert Tent
    { 194, 105, 37598 }, // Desert Tent
    { 195, 105, 37598 }, // Desert Tent
    { 196, 105, 37604 }, // Market Tent
    { 197, 6, 38174 }, // Shadow Tower
    { 198, 2, 38175 }, // Stone Ram House
    { 199, 3, 38176 }, // Pixie House
    { 200, 1, 38177 }, // Dog Kennels
    { 201, 5, 38178 }, // Warboar Pit
    { 202, 4, 38179 }, // Nest
    { 203, 1, 38315 }, // Star Monastery
    { 204, 3, 38316 }, // Wind Temple
    { 205, 2, 38317 }, // Hall of Valor
    { 206, 5, 38318 }, // Fire Temple
    { 207, 4, 38319 }, // Siege Workshop
    { 208, 6, 38321 }, // Academy of Destruction
    { 209, 137, 41377 }, // Great Tower of Sorcery
    { 210, 114, 41378 }, // Great Bone Tower
    { 211, 114, 41379 }, // Great Bone Tower
    { 212, 105, 41380 }, // Great Firestarter
    { 213, 124, 41381 }, // Old Dark Tower
};

// Races 1-6 of the building types (other values are special buildings and get no race)
static const wchar_t* const RaceNames[] = { L"", L"ludzie", L"krasnoludy", L"elfy", L"trolle", L"orki", L"mroczne elfy" };
static const wchar_t OtherSourceName[] = L"Inne";
static const wchar_t MonumentSourceName[] = L"Monument";

typedef struct
{
    wchar_t* Data;
    int Length;
    int Reserved[2];
} GameString;

typedef void (__fastcall *SetTextFn)(void* label, void* unused, GameString* text);
typedef void (__fastcall *StringCtorFn)(GameString* self, void* unused, const wchar_t* text);
typedef void (__fastcall *StringEmptyFn)(GameString* self, void* unused);
typedef void (__fastcall *StringDtorFn)(GameString* self, void* unused);
typedef void (__fastcall *GetTextFn)(void* textDb, void* unused, DWORD textId, GameString* out);
typedef GameString* (__fastcall *GetHelpTextFn)(void* textDb, void* unused, GameString* out, DWORD helpId, DWORD one);
typedef void (__fastcall *SetHelpFn)(void* control, void* unused, GameString* text);

static BYTE* GameBase;
static SetTextFn SetText;
static StringCtorFn StringCtor;
static StringEmptyFn StringEmpty;
static StringDtorFn StringDtor;
static GetTextFn GetText;
static GetHelpTextFn GetHelpText;
static DWORD AddResourceTarget;
static DWORD JobLoopResume;

// The game's Add puts every income into one pool (players 1-3): the SharedResources mod or the shared exe.
static BOOL IsShared(void)
{
    return GameBase != NULL && memcmp(GameBase + ADD_RESOURCE_RVA + 3, SharedAddStart, sizeof(SharedAddStart)) == 0;
}

// Increases per resource in one-second buckets; Second[i][b] tells which second bucket b belongs to.
static DWORD Gain[RESOURCE_COUNT][WINDOW_SECONDS];
static DWORD Second[RESOURCE_COUNT][WINDOW_SECONDS];
static DWORD LastAmount[RESOURCE_COUNT];
static BOOL HasLastAmount[RESOURCE_COUNT];
static DWORD LastUpdateTick;

static void ResetHistory(void)
{
    memset(Gain, 0, sizeof(Gain));
    memset(Second, 0, sizeof(Second));
    memset(HasLastAmount, 0, sizeof(HasLastAmount));
}

// Records the current amount of resource <index> and returns how much came in during the last minute.
static DWORD RecordAmount(int index, DWORD amount, DWORD now)
{
    DWORD second = now / 1000;
    DWORD bucket = second % WINDOW_SECONDS;
    DWORD total = 0;
    int b;

    if (index == 0)
    {
        if (LastUpdateTick != 0 && now - LastUpdateTick > RESTART_GAP_MS) ResetHistory();
        LastUpdateTick = now;
    }
    if (Second[index][bucket] != second)
    {
        Second[index][bucket] = second;
        Gain[index][bucket] = 0;
    }
    if (HasLastAmount[index] && amount > LastAmount[index]) Gain[index][bucket] += amount - LastAmount[index];
    LastAmount[index] = amount;
    HasLastAmount[index] = TRUE;

    for (b = 0; b < WINDOW_SECONDS; b++)
    {
        if (second - Second[index][b] < WINDOW_SECONDS) total += Gain[index][b];
    }
    return total;
}

/* ------------------------------------------------------------------------ */
/* Income per source                                                         */
/* ------------------------------------------------------------------------ */

typedef struct
{
    DWORD Tick;
    DWORD Amount;
    WORD Player;
    WORD Source;  // building type, SOURCE_MONUMENT or SOURCE_OTHER
    BYTE Group;
    BYTE Type;
} IncomeEvent;

static IncomeEvent Events[MAX_EVENTS];
static DWORD NextEvent;

// The figure the job loop is working on, and the loop's CGdFigureJobs
static volatile WORD CurrentFigure;
static BYTE* volatile CurrentJobs;

// Building type of building <index> in <buildings> (CGdBuilding) when <player> owns it, else SOURCE_OTHER.
static WORD BuildingSource(BYTE* buildings, DWORD index, WORD player)
{
    BYTE* record;
    if (buildings == NULL || index == 0 || index >= BUILDING_COUNT) return SOURCE_OTHER;
    record = buildings + index * BUILDING_SIZE;
    if (*(WORD*)(record + BUILDING_OWNER) != player) return SOURCE_OTHER;
    return *(record + BUILDING_TYPE);
}

// Called before CGdResource::Add with its arguments (args[0] = player, ...), the caller's return address and
// the caller's ebx and esi.
static void __cdecl RecordIncome(DWORD* args, DWORD returnAddress, DWORD callerEbx, BYTE* callerEsi)
{
    IncomeEvent* event;
    WORD player = (WORD)args[0];
    WORD source = SOURCE_OTHER;
    DWORD rva = returnAddress - (DWORD)GameBase;

    if ((int)args[3] <= 0) return;
    __try
    {
        if (rva == BUILDING_CALL_RETURN_1 || rva == BUILDING_CALL_RETURN_2)
        {
            if (callerEbx % BUILDING_SIZE == 0)
            {
                source = BuildingSource(*(BYTE**)(callerEsi + JOBS_BUILDINGS), callerEbx / BUILDING_SIZE, player);
            }
        }
        else if (rva >= JOB_HANDLERS_START && rva < JOB_HANDLERS_END && CurrentJobs != NULL)
        {
            BYTE* figure = *(BYTE**)(CurrentJobs + JOBS_FIGURES) + CurrentFigure * FIGURE_SIZE;
            if (*(WORD*)(figure + FIGURE_OWNER) == player)
            {
                source = BuildingSource(*(BYTE**)(CurrentJobs + JOBS_BUILDINGS), *(WORD*)(figure + FIGURE_BUILDING),
                                        player);
                // Our worker without a building of ours: it carries to the monument
                if (source == SOURCE_OTHER) source = SOURCE_MONUMENT;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        source = SOURCE_OTHER;
    }

    event = &Events[NextEvent++ % MAX_EVENTS];
    event->Tick = GetTickCount();
    event->Amount = args[3];
    event->Player = player;
    event->Source = source;
    event->Group = (BYTE)args[1];
    event->Type = (BYTE)args[2];
}

// Replaces "call CGdResource::Add" (thiscall, ecx = resources, 4 arguments): notes the income, then goes on
// into the original function with everything as it was.
__declspec(naked) static void AddResourceHook(void)
{
    __asm
    {
        push    ecx
        push    edx
        push    esi                 // caller's esi
        push    ebx                 // caller's ebx
        push    dword ptr [esp+10h] // return address
        lea     eax, [esp+18h]      // arguments
        push    eax
        call    RecordIncome
        add     esp, 10h
        pop     edx
        pop     ecx
        jmp     dword ptr [AddResourceTarget]
    }
}

// Top of the figure loop in CGdFigureJobs::Update: esi = figure index, edi = CGdFigureJobs.
__declspec(naked) static void JobLoopHook(void)
{
    __asm
    {
        mov     CurrentFigure, si
        mov     CurrentJobs, edi
        movzx   eax, si
        imul    ebx, eax, 2B3h
        jmp     dword ptr [JobLoopResume]
    }
}

/* ------------------------------------------------------------------------ */
/* Hint                                                                      */
/* ------------------------------------------------------------------------ */

static wchar_t BuildingNames[256][NAME_LENGTH];
static BOOL BuildingNameLoaded[256];

static const wchar_t* SourceName(BYTE* main, WORD source)
{
    GameString text;
    int i;

    if (source == SOURCE_MONUMENT) return MonumentSourceName;
    if (source == SOURCE_OTHER || source > 255) return OtherSourceName;
    if (BuildingNameLoaded[source]) return BuildingNames[source];
    BuildingNameLoaded[source] = TRUE;
    swprintf_s(BuildingNames[source], NAME_LENGTH, L"#%u", source);
    for (i = 0; i < sizeof(BuildingTypes) / sizeof(BuildingTypes[0]); i++)
    {
        if (BuildingTypes[i].Id != source) continue;
        StringEmpty(&text, NULL);
        GetText(*(void**)(main + MAIN_TEXT_DB), NULL, BuildingTypes[i].NameText, &text);
        if (text.Data != NULL && text.Length > 0)
        {
            int length = text.Length < 40 ? text.Length : 40;
            wchar_t name[48];
            memcpy(name, text.Data, length * sizeof(wchar_t));
            name[length] = L'\0';
            if (BuildingTypes[i].Race >= 1 && BuildingTypes[i].Race <= 6)
            {
                swprintf_s(BuildingNames[source], NAME_LENGTH, L"%s (%s)", name, RaceNames[BuildingTypes[i].Race]);
            }
            else
            {
                wcscpy_s(BuildingNames[source], NAME_LENGTH, name);
            }
        }
        StringDtor(&text, NULL);
        break;
    }
    return BuildingNames[source];
}

// Writes the income of resource <index> during the last minute per source into <buffer>; returns the number of
// sources. With shared resources every player's income counts.
static int BuildBreakdown(BYTE* main, int index, WORD player, wchar_t* buffer, int bufferLength)
{
    WORD sources[MAX_SOURCES];
    DWORD totals[MAX_SOURCES];
    BOOL shared = IsShared();
    int count = 0, i, j, used = 0;
    DWORD now = GetTickCount();

    buffer[0] = L'\0';
    for (i = 0; i < MAX_EVENTS; i++)
    {
        IncomeEvent* e = &Events[i];
        if (e->Tick == 0 || now - e->Tick >= WINDOW_SECONDS * 1000 || (e->Player != player && !shared) ||
            e->Type != BarTypes[index])
        {
            continue;
        }
        for (j = 0; j < count && sources[j] != e->Source; j++)
        {
        }
        if (j == count)
        {
            if (count == MAX_SOURCES) continue;
            sources[count] = e->Source;
            totals[count] = 0;
            count++;
        }
        totals[j] += e->Amount;
    }
    // Largest income first
    for (i = 1; i < count; i++)
    {
        for (j = i; j > 0 && totals[j] > totals[j - 1]; j--)
        {
            DWORD t = totals[j]; WORD s = sources[j];
            totals[j] = totals[j - 1]; sources[j] = sources[j - 1];
            totals[j - 1] = t; sources[j - 1] = s;
        }
    }
    for (i = 0; i < count; i++)
    {
        int written = swprintf_s(buffer + used, bufferLength - used, L"%s%s: %%c%x+%lu%%d", i ? L"\n" : L"",
                                 SourceName(main, sources[i]), GAIN_COLOR, totals[i]);
        if (written < 0) break;
        used += written;
    }
    return count;
}

static void SetHelp(void* control, GameString* text)
{
    if (control != NULL) (*(SetHelpFn*)(*(BYTE**)control + CONTROL_SET_HELP))(control, NULL, text);
}

// Sets the hint of resource <index> (its icon and its amount) to the income per source, or back to the game's
// own hint when nothing came in.
static void UpdateHint(BYTE* main, int index)
{
    wchar_t buffer[1024];
    GameString hint;
    WORD player = *(WORD*)(*(BYTE**)(main + MAIN_PLAYER_INFO) + 0x8CC);
    void* icon = *(void**)(main + MAIN_ICONS + 4 * index);
    void* label = *(void**)(main + MAIN_LABELS + 4 * index);

    if (BuildBreakdown(main, index, player, buffer, sizeof(buffer) / sizeof(buffer[0])) > 0)
    {
        StringCtor(&hint, NULL, buffer);
    }
    else
    {
        StringEmpty(&hint, NULL);
        GetHelpText(*(void**)(main + MAIN_TEXT_DB), NULL, &hint, GameBase[HELP_IDS_RVA + index], 1);
    }
    SetHelp(icon, &hint);
    SetHelp(label, &hint);
    StringDtor(&hint, NULL);
}

/* ------------------------------------------------------------------------ */
/* Resource bar                                                              */
/* ------------------------------------------------------------------------ */

// Called instead of label->SetText(text) for resource <index>; <frame> is the ebp of UpdateResources.
static void __cdecl SetResourceText(void* label, GameString* text, DWORD index, BYTE* frame)
{
    wchar_t buffer[256];
    GameString withGain;
    BOOL built = FALSE;

    index &= 0xFFFF;
    __try
    {
        if (index < RESOURCE_COUNT && text->Length >= 0 && text->Length < 200)
        {
            // Each pair: the amount of the light races (group 0), then of the dark races (group 1: trolls, orcs,
            // dark elves); the bar shows whichever the player has, so both count.
            DWORD amount = *(DWORD*)(frame + AMOUNTS_IN_FRAME + 8 * index) +
                           *(DWORD*)(frame + AMOUNTS_IN_FRAME + 8 * index + 4);
            DWORD gain = RecordAmount((int)index, amount, GetTickCount());
            // "%cRRGGBB" starts a colour and "%d" goes back to the default (format of 00A19670).
            int prefix = swprintf_s(buffer, 48, L"%%c%x+%lu %%d", GAIN_COLOR, gain);
            if (prefix > 0)
            {
                memcpy(buffer + prefix, text->Data, text->Length * sizeof(wchar_t));
                buffer[prefix + text->Length] = L'\0';
                built = TRUE;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        built = FALSE;
    }
    // The hint has its own guard, so a problem there never costs the bar its "+N".
    __try
    {
        if (index < RESOURCE_COUNT) UpdateHint(*(BYTE**)(frame + MAIN_IN_FRAME), (int)index);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }

    if (!built)
    {
        SetText(label, NULL, text);
        return;
    }
    StringCtor(&withGain, NULL, buffer);
    SetText(label, NULL, &withGain);
    StringDtor(&withGain, NULL);
}

// Replaces "call CMnuLabel::SetText" (thiscall, ecx = label, one argument: the text).
__declspec(naked) static void SetTextHook(void)
{
    __asm
    {
        push    ebp                 // frame of UpdateResources (holds the amounts)
        push    edi                 // resource index
        push    dword ptr [esp+0Ch] // text
        push    ecx                 // label
        call    SetResourceText
        add     esp, 10h
        ret     4
    }
}

/* ------------------------------------------------------------------------ */
/* Patching                                                                  */
/* ------------------------------------------------------------------------ */

static void WriteCode(BYTE* address, const void* bytes, SIZE_T size)
{
    DWORD oldProtect;
    VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(address, bytes, size);
    VirtualProtect(address, size, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), address, size);
}

// Points "call rel32" at <call> to <target>.
static void RedirectCall(BYTE* call, void* target)
{
    LONG rel = (LONG)((BYTE*)target - (call + 5));
    WriteCode(call + 1, &rel, sizeof(rel));
}

// Income per source: the Add calls and the job loop. Skipped as a whole when any of it is not as expected.
static BOOL PatchIncome(BYTE* base)
{
    BYTE* loop = base + JOB_LOOP_RVA;
    BYTE jump[sizeof(JobLoopOriginal)];
    int i;

    if (memcmp(loop, JobLoopOriginal, sizeof(JobLoopOriginal)) != 0) return FALSE;
    for (i = 0; i < sizeof(AddResourceCalls) / sizeof(AddResourceCalls[0]); i++)
    {
        BYTE* call = base + AddResourceCalls[i];
        if (call[0] != 0xE8 || call + 5 + *(LONG*)(call + 1) != base + ADD_RESOURCE_RVA) return FALSE;
    }

    AddResourceTarget = (DWORD)(base + ADD_RESOURCE_RVA);
    for (i = 0; i < sizeof(AddResourceCalls) / sizeof(AddResourceCalls[0]); i++)
    {
        RedirectCall(base + AddResourceCalls[i], AddResourceHook);
    }

    JobLoopResume = (DWORD)(loop + sizeof(JobLoopOriginal));
    memset(jump, 0x90, sizeof(jump));
    jump[0] = 0xE9;
    *(LONG*)(jump + 1) = (LONG)((BYTE*)JobLoopHook - (loop + 5));
    WriteCode(loop, jump, sizeof(jump));
    return TRUE;
}

static void ApplyPatchTo(BYTE* base)
{
    BYTE* call = base + SET_TEXT_CALL_RVA; // the E8 byte

    if (!IsGameImageLargeEnough(base) || *(int*)(base + VERSION_RVA) != VERSION_VALUE ||
        memcmp(call - 2, SetTextCallOriginal, sizeof(SetTextCallOriginal)) != 0 ||
        call + 5 + *(LONG*)(call + 1) != base + SET_TEXT_RVA)
    {
        OutputDebugStringA("ResourceGain: unsupported SpellForce.exe build or code already changed, nothing patched.\n");
        return;
    }

    GameBase = base;
    SetText = (SetTextFn)(base + SET_TEXT_RVA);
    StringCtor = (StringCtorFn)(base + STRING_CTOR_RVA);
    StringEmpty = (StringEmptyFn)(base + STRING_EMPTY_RVA);
    StringDtor = (StringDtorFn)(base + STRING_DTOR_RVA);
    GetText = (GetTextFn)(base + GET_TEXT_RVA);
    GetHelpText = (GetHelpTextFn)(base + GET_HELP_TEXT_RVA);

    if (!PatchIncome(base))
    {
        OutputDebugStringA("ResourceGain: resource income code not as expected, hints per building disabled.\n");
    }
    RedirectCall(call, SetTextHook);

    OutputDebugStringA("ResourceGain: resource bar shows the gain of the last minute.\n");
}
