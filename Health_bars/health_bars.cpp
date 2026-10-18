/*
 * Health Bars - SFSF mod for SpellForce Platinum Edition 1.61 (Steam beta branch)
 *
 * The game draws a health bar only over the figure under the mouse cursor and
 * the selected ones. This mod draws it over every figure (own units, allies,
 * enemies), with settings to choose which.
 *
 * How the game draws them (Ghidra addresses, image base 0x400000):
 *  - The world overlay (object at +0x70 of the world view, mesh
 *    "effect_mouseover_healthbar.msh" loaded by 0x008876c0) keeps a vector of
 *    8-byte entries { object reference, marker kind } at +0x14 (begin), +0x18
 *    (end), +0x1c (capacity). The reference is (index << 8) | type, type 1 =
 *    figure. The mouse-over and the selection fill it.
 *  - Each frame 0x00888ea0 (thiscall, 2 arguments, ret 8) walks the vector: it
 *    draws the marker of the kind (0x00887f30, nothing for kinds 0 and 12) and
 *    the health bar (0x008881f0) of each figure whose owner is not 0xffff.
 *    It is called at 0x008890aa (world render) and 0x00887f1e.
 * The mod redirects those two calls. For the call it gives the game a vector
 * holding the game's entries plus one entry of kind 12 (health bar, no marker)
 * for each figure to show, then puts the game's vector back. The game's own
 * entries keep their markers.
 *
 * Fog of war: the world view (the overlay is its member at +0x70) keeps the fog
 * of war at +0x8bc (created by 0x007a6819, NULL without fog). The game draws a
 * figure only where 0x0089913f (thiscall(fog, x, y), ret 8) returns 1, at the
 * position given by 0x0086e720 (thiscall(object reference *, float out[2]),
 * ret 4), when the fog is active (0x008990fe(fog, 1) != 0): see 0x008918e3.
 * The mod adds no bar for figures the player does not see.
 *
 * Enemy colour: the fill of a bar is coloured by 0x00886cd0 (thiscall(colour
 * gradient, float4 *out, float position, float scale), ret 0xc, gradient at
 * overlay +0x40: [+0] keys of 4 floats, [+4] key count), called at 0x008886e7
 * from the bar drawing 0x008881f0 (thiscall, 6 arguments, ret 0x18; 3rd =
 * object reference), itself called at 0x00889037. Both calls are redirected:
 * a figure hostile to the local player gets the gradient's "empty" end (red),
 * the others its "full" end (green), whatever their health (the bar's length
 * still shows it). The greener end of the gradient is the "full" one.
 *
 * Settings: sfsf\health_bars.ini (see write_default_settings).
 * GameData.cff: nothing to add.
 */

#include "../../src/api/sfsf.h"
#include "../../src/asi/sf_asi.h"

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define HEALTH_BARS_VERSION        "1.3.0"
#define MOD_ID                     "health_bars"
#define LOG_TAG                    "[health_bars] "

#define RVA_DRAW_OVERLAY           0x488ea0    // 0x00888ea0
#define RVA_DRAW_CALL_RENDER       0x4890aa    // 0x008890aa
#define RVA_DRAW_CALL_OTHER        0x487f1e    // 0x00887f1e
#define RVA_PLAYER_IS_HOSTILE      0x2fe888    // 0x006fe888 thiscall(figure toolbox, player, figure), ret 8
#define RVA_DRAW_BAR               0x4881f0    // 0x008881f0
#define RVA_DRAW_BAR_CALL          0x489037    // 0x00889037, in 0x00888ea0
#define RVA_BAR_COLOUR             0x486cd0    // 0x00886cd0
#define RVA_BAR_COLOUR_CALL        0x4886e7    // 0x008886e7, in 0x008881f0
#define OVERLAY_GRADIENT           0x40
#define RVA_FOG_ACTIVE             0x4990fe    // 0x008990fe thiscall(fog, int), ret 4
#define RVA_FOG_VISIBLE            0x49913f    // 0x0089913f thiscall(fog, x, y), ret 8
#define RVA_OBJECT_POSITION        0x46e720    // 0x0086e720 thiscall(object reference *, float out[2]), ret 4
#define OVERLAY_IN_WORLD_VIEW      0x70
#define WORLD_VIEW_FOG             0x8bc

// World overlay object
#define OVERLAY_ENTRIES_BEGIN      0x14
#define OVERLAY_ENTRIES_END        0x18
#define OVERLAY_ENTRIES_CAPACITY   0x1c
#define OVERLAY_FIGURES            0x34        // SF_CGdFigure *
#define OVERLAY_WORLD              0x38        // world: +0x38 figure toolbox
#define OVERLAY_PLAYER_INFO        0x3c        // +0x8cc local player id
#define WORLD_FIGURE_TOOLBOX       0x38
#define PLAYER_INFO_LOCAL_PLAYER   0x8cc

#define OBJECT_TYPE_FIGURE         1
#define KIND_HEALTH_BAR_ONLY       12          // no marker (0x00887f30 skips 0 and 12)
#define NO_OWNER                   0xffff
#define MAX_FIGURES                2000
#define MAX_ENTRIES                (MAX_FIGURES * 2)

#define SETTINGS_FILE              "sfsf\\health_bars.ini"
#define SETTINGS_SECTION           "health_bars"

typedef struct
{
    uint32_t object;
    uint32_t kind;
} OverlayEntry;

typedef void (__thiscall *drawOverlay_ptr)(void *overlay, uint32_t a1, uint32_t a2);
typedef uint32_t (__thiscall *playerIsHostile_ptr)(void *figure_toolbox, uint16_t player, uint16_t figure);
typedef void (__thiscall *drawBar_ptr)(void *overlay, uint32_t a1, uint32_t a2, uint32_t object, void *node,
                                       uint32_t a5, uint32_t a6);
typedef uint32_t (__thiscall *fogActive_ptr)(void *fog, uint32_t param);
typedef uint32_t (__thiscall *fogVisible_ptr)(void *fog, uint16_t x, uint16_t y);
typedef void (__thiscall *objectPosition_ptr)(const uint32_t *object, float *out);
typedef float *(__thiscall *barColour_ptr)(void *gradient, float *out, float position, float scale);

typedef struct
{
    float *keys;        // 4 floats each
    int32_t count;
} ColourGradient;

SpellforceSpellFramework *sfsf;
SFLog *logAPI;

static drawOverlay_ptr s_draw_render;           // previous targets of the two calls
static drawOverlay_ptr s_draw_other;
static playerIsHostile_ptr s_player_is_hostile;
static drawBar_ptr s_draw_bar;
static fogActive_ptr s_fog_active;
static fogVisible_ptr s_fog_visible;
static objectPosition_ptr s_object_position;
static barColour_ptr s_bar_colour;

// bar being drawn (set by drawBarHook for barColourHook)
static bool s_bar_is_figure = false;
static bool s_bar_is_enemy = false;
static float s_full_colour[4];
static float s_enemy_colour[4];
static bool s_colours_checked = false;
static bool s_colours_known = false;

static struct
{
    int own;
    int allies;
    int enemies;
    int workers;
    int only_damaged;
    int max_distance;
    int toggle_key;
    int red_enemies;
    int green_others;
} s_cfg = {1, 1, 1, 0, 0, 0, 0, 1, 1};

static bool s_enabled = true;
static bool s_key_down = false;
static OverlayEntry s_entries[MAX_ENTRIES];
static uint8_t s_listed[MAX_FIGURES];

/* ------------------------------------------------------------------------ */
/* Which figures                                                             */
/* ------------------------------------------------------------------------ */

static bool is_worker_task(uint8_t task)
{
    switch (task)
    {
        case TASK_WORKER: case TASK_WOODCUTTER: case TASK_QUARRY: case TASK_MINE: case TASK_FORGE:
        case TASK_CARPENTER: case TASK_HUNTER: case TASK_HUNTING_LODGE: case TASK_CATTLE_BREEDER:
        case TASK_FISHER: case TASK_MASTER_WOODCUTTER: case TASK_SHRINE: case TASK_PRIEST:
        case TASK_SMELTER: case TASK_STOREKEEPER: case TASK_GATHERER: case TASK_FARMER:
        case TASK_FORESTER: case TASK_SCAVENGER: case TASK_MACE_CARVER: case TASK_MASTER_FISHER:
            return true;
        default:
            return false;
    }
}

// The local player's main character, for MaxDistance (0 if none)
static uint16_t main_character(SF_CGdFigure *figures, uint16_t player)
{
    for (uint16_t i = 1; i <= figures->max_used && i < MAX_FIGURES; i++)
    {
        GdFigure &f = figures->figures[i];
        if (f.owner == player && f.current_job.task == TASK_MAINCHAR && !(f.flags & IS_DEAD))
        {
            return i;
        }
    }
    return 0;
}

// As the game decides to draw a figure (0x008918e3): hidden under the fog of war?
static bool hidden_by_fog(void *fog, uint16_t index)
{
    if (!fog)
    {
        return false;
    }
    uint32_t object = ((uint32_t)index << 8) | OBJECT_TYPE_FIGURE;
    float position[2] = {0, 0};
    s_object_position(&object, position);
    if (position[0] < 0 || position[1] < 0 || position[0] >= 1024 || position[1] >= 1024)
    {
        return true;
    }
    return s_fog_visible(fog, (uint16_t)(int)position[0], (uint16_t)(int)position[1]) == 0;
}

static bool wanted(SF_CGdFigure *figures, void *toolbox, void *fog, uint16_t player, uint16_t index, uint16_t centre)
{
    GdFigure &f = figures->figures[index];
    if (f.owner == NO_OWNER || (f.flags & IS_DEAD) || f.building != 0 || f.health.base_val == 0)
    {
        return false;
    }
    if (f.owner == player)
    {
        if (!s_cfg.own || (!s_cfg.workers && is_worker_task(f.current_job.task)))
        {
            return false;
        }
    }
    else if (s_player_is_hostile(toolbox, player, index))
    {
        if (!s_cfg.enemies)
        {
            return false;
        }
    }
    else if (!s_cfg.allies)
    {
        return false;
    }
    if (s_cfg.max_distance > 0 && centre != 0)
    {
        int dx = (int)f.position.X - (int)figures->figures[centre].position.X;
        int dy = (int)f.position.Y - (int)figures->figures[centre].position.Y;
        if (dx * dx + dy * dy > s_cfg.max_distance * s_cfg.max_distance)
        {
            return false;
        }
    }
    if (s_cfg.only_damaged && sfsf->figureAPI->getCurrentHealthPercent(figures, index) >= 100)
    {
        return false;
    }
    if (f.owner != player && hidden_by_fog(fog, index))
    {
        return false;
    }
    return true;
}

static void update_toggle()
{
    if (s_cfg.toggle_key == 0)
    {
        return;
    }
    bool down = (GetAsyncKeyState(s_cfg.toggle_key) & 0x8000) != 0;
    if (down && !s_key_down)
    {
        s_enabled = !s_enabled;
    }
    s_key_down = down;
}

/* ------------------------------------------------------------------------ */
/* Overlay draw                                                              */
/* ------------------------------------------------------------------------ */

static void draw_with_all_bars(drawOverlay_ptr original, void *overlay, uint32_t a1, uint32_t a2)
{
    update_toggle();
    uint8_t *base = (uint8_t *)overlay;
    OverlayEntry **begin = (OverlayEntry **)(base + OVERLAY_ENTRIES_BEGIN);
    OverlayEntry **end = (OverlayEntry **)(base + OVERLAY_ENTRIES_END);
    OverlayEntry **capacity = (OverlayEntry **)(base + OVERLAY_ENTRIES_CAPACITY);
    SF_CGdFigure *figures = *(SF_CGdFigure **)(base + OVERLAY_FIGURES);
    uint8_t *world = *(uint8_t **)(base + OVERLAY_WORLD);
    uint8_t *player_info = *(uint8_t **)(base + OVERLAY_PLAYER_INFO);
    size_t game_count = (*begin && *end > *begin) ? (size_t)(*end - *begin) : 0;
    if (!s_enabled || !figures || !world || !player_info || game_count > MAX_ENTRIES - MAX_FIGURES)
    {
        original(overlay, a1, a2);
        return;
    }
    void *toolbox = *(void **)(world + WORLD_FIGURE_TOOLBOX);
    uint16_t player = *(uint16_t *)(player_info + PLAYER_INFO_LOCAL_PLAYER);
    if (!toolbox)
    {
        original(overlay, a1, a2);
        return;
    }

    memset(s_listed, 0, sizeof(s_listed));
    for (size_t i = 0; i < game_count; i++)
    {
        s_entries[i] = (*begin)[i];
        if ((s_entries[i].object & 0xff) == OBJECT_TYPE_FIGURE)
        {
            uint16_t index = (uint16_t)(s_entries[i].object >> 8);
            if (index < MAX_FIGURES)
            {
                s_listed[index] = 1;
            }
        }
    }
    size_t count = game_count;
    uint16_t centre = s_cfg.max_distance > 0 ? main_character(figures, player) : 0;
    void *fog = *(void **)(base - OVERLAY_IN_WORLD_VIEW + WORLD_VIEW_FOG);
    if (fog && !s_fog_active(fog, 1))
    {
        fog = NULL;
    }
    for (uint16_t i = 1; i <= figures->max_used && i < MAX_FIGURES; i++)
    {
        if (!s_listed[i] && wanted(figures, toolbox, fog, player, i, centre))
        {
            s_entries[count].object = ((uint32_t)i << 8) | OBJECT_TYPE_FIGURE;
            s_entries[count].kind = KIND_HEALTH_BAR_ONLY;
            count++;
        }
    }
    if (count == game_count)
    {
        original(overlay, a1, a2);
        return;
    }

    // the game's vector for this call only, then back (the call may update
    // the kinds of the game's entries: copied back)
    OverlayEntry *saved_begin = *begin, *saved_end = *end, *saved_capacity = *capacity;
    *begin = s_entries;
    *end = s_entries + count;
    *capacity = s_entries + count;
    original(overlay, a1, a2);
    *begin = saved_begin;
    *end = saved_end;
    *capacity = saved_capacity;
    if (game_count)
    {
        memcpy(saved_begin, s_entries, game_count * sizeof(OverlayEntry));
    }
}

static void __thiscall drawRenderHook(void *overlay, uint32_t a1, uint32_t a2)
{
    draw_with_all_bars(s_draw_render, overlay, a1, a2);
}

static void __thiscall drawOtherHook(void *overlay, uint32_t a1, uint32_t a2)
{
    draw_with_all_bars(s_draw_other, overlay, a1, a2);
}

/* ------------------------------------------------------------------------ */
/* Enemy colour                                                              */
/* ------------------------------------------------------------------------ */

static void __thiscall drawBarHook(void *overlay, uint32_t a1, uint32_t a2, uint32_t object, void *node,
                                   uint32_t a5, uint32_t a6)
{
    s_bar_is_figure = false;
    s_bar_is_enemy = false;
    uint8_t *base = (uint8_t *)overlay;
    SF_CGdFigure *figures = *(SF_CGdFigure **)(base + OVERLAY_FIGURES);
    uint8_t *world = *(uint8_t **)(base + OVERLAY_WORLD);
    uint8_t *player_info = *(uint8_t **)(base + OVERLAY_PLAYER_INFO);
    uint16_t index = (uint16_t)(object >> 8);
    if ((object & 0xff) == OBJECT_TYPE_FIGURE && figures && world && player_info &&
        index > 0 && index < MAX_FIGURES)
    {
        void *toolbox = *(void **)(world + WORLD_FIGURE_TOOLBOX);
        uint16_t player = *(uint16_t *)(player_info + PLAYER_INFO_LOCAL_PLAYER);
        uint16_t owner = figures->figures[index].owner;
        if (toolbox && owner != player && owner != NO_OWNER)
        {
            s_bar_is_enemy = s_player_is_hostile(toolbox, player, index) != 0;
        }
        s_bar_is_figure = true;
    }
    s_draw_bar(overlay, a1, a2, object, node, a5, a6);
    s_bar_is_figure = false;
    s_bar_is_enemy = false;
}

// The gradient's two ends: full health (green) and almost empty (red). Green is
// the middle component whatever the order of the other two (RGBA or BGRA), so
// the greener end is the full health one.
static float greenness(const float *colour)
{
    return colour[1] - (colour[0] > colour[2] ? colour[0] : colour[2]);
}

static void find_colours(ColourGradient *gradient)
{
    s_colours_checked = true;
    if (!gradient || !gradient->keys || gradient->count < 2 || gradient->count > 64)
    {
        logAPI->logError(LOG_TAG "unexpected bar colour gradient; game colours kept");
        return;
    }
    const float *first = gradient->keys;
    const float *last = gradient->keys + 4 * (gradient->count - 1);
    bool first_is_full = greenness(first) >= greenness(last);
    memcpy(s_full_colour, first_is_full ? first : last, sizeof(s_full_colour));
    memcpy(s_enemy_colour, first_is_full ? last : first, sizeof(s_enemy_colour));
    s_colours_known = true;
    logAPI->logInfo(LOG_TAG "bar colours: full %.2f %.2f %.2f, empty (enemies) %.2f %.2f %.2f",
                    s_full_colour[0], s_full_colour[1], s_full_colour[2],
                    s_enemy_colour[0], s_enemy_colour[1], s_enemy_colour[2]);
}

static float *__thiscall barColourHook(void *gradient, float *out, float position, float scale)
{
    float *result = s_bar_colour(gradient, out, position, scale);
    if (!result || !s_bar_is_figure)
    {
        return result;
    }
    if (!s_colours_checked)
    {
        find_colours((ColourGradient *)gradient);
    }
    if (!s_colours_known)
    {
        return result;
    }
    const float *colour = NULL;
    if (s_bar_is_enemy && s_cfg.red_enemies)
    {
        colour = s_enemy_colour;
    }
    else if (!s_bar_is_enemy && s_cfg.green_others)
    {
        colour = s_full_colour;
    }
    if (colour)
    {
        result[0] = colour[0];
        result[1] = colour[1];
        result[2] = colour[2];      // alpha (fade) kept
    }
    return result;
}

/* ------------------------------------------------------------------------ */
/* Settings and patching                                                     */
/* ------------------------------------------------------------------------ */

static void write_default_settings(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f)
    {
        return;
    }
    fprintf(f, "[" SETTINGS_SECTION "]\n");
    fprintf(f, "; Health bars always shown over (1 = yes, 0 = no):\n");
    fprintf(f, "Own=1\n");
    fprintf(f, "Allies=1\n");
    fprintf(f, "Enemies=1\n");
    fprintf(f, "; your workers (woodcutters, miners, ...), when Own=1\n");
    fprintf(f, "Workers=0\n");
    fprintf(f, "\n; 1 = only figures that have lost health\n");
    fprintf(f, "OnlyDamaged=0\n");
    fprintf(f, "\n; Only figures within this distance of your main character (0 = no limit)\n");
    fprintf(f, "MaxDistance=0\n");
    fprintf(f, "\n; Key that switches the bars on and off in game: a Windows virtual-key code,\n");
    fprintf(f, "; e.g. 0x48 = H, 0x75 = F6 (0 = no key, always on)\n");
    fprintf(f, "ToggleKey=0\n");
    fprintf(f, "\n; 1 = enemy bars in red (the colour of an almost empty bar), 0 = game colours\n");
    fprintf(f, "RedEnemies=1\n");
    fprintf(f, "; 1 = your bars and allied bars always green (full bar colour), 0 = game colours\n");
    fprintf(f, "GreenOthers=1\n");
    fprintf(f, "\n; Read when the game starts.\n");
    fclose(f);
}

static void load_settings()
{
    char path[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, path, MAX_PATH);
    char *slash = length ? strrchr(path, '\\') : NULL;
    if (!slash || (size_t)(slash - path) + 1 + sizeof(SETTINGS_FILE) > MAX_PATH)
    {
        return;
    }
    strcpy(slash + 1, SETTINGS_FILE);
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES)
    {
        write_default_settings(path);
    }
    s_cfg.own = GetPrivateProfileIntA(SETTINGS_SECTION, "Own", 1, path) != 0;
    s_cfg.allies = GetPrivateProfileIntA(SETTINGS_SECTION, "Allies", 1, path) != 0;
    s_cfg.enemies = GetPrivateProfileIntA(SETTINGS_SECTION, "Enemies", 1, path) != 0;
    s_cfg.workers = GetPrivateProfileIntA(SETTINGS_SECTION, "Workers", 0, path) != 0;
    s_cfg.only_damaged = GetPrivateProfileIntA(SETTINGS_SECTION, "OnlyDamaged", 0, path) != 0;
    s_cfg.max_distance = GetPrivateProfileIntA(SETTINGS_SECTION, "MaxDistance", 0, path);
    if (s_cfg.max_distance < 0)
    {
        s_cfg.max_distance = 0;
    }
    s_cfg.red_enemies = GetPrivateProfileIntA(SETTINGS_SECTION, "RedEnemies", 1, path) != 0;
    s_cfg.green_others = GetPrivateProfileIntA(SETTINGS_SECTION, "GreenOthers", 1, path) != 0;
    char key[32];
    GetPrivateProfileStringA(SETTINGS_SECTION, "ToggleKey", "0", key, sizeof(key), path);
    s_cfg.toggle_key = (int)strtol(key, NULL, 0) & 0xff;
}

static bool write_code(uintptr_t address, const void *data, size_t size)
{
    ASI::MemoryRegion region((int)address, (int)size);
    if (!ASI::BeginRewrite(region))
    {
        return false;
    }
    memcpy((void *)address, data, size);
    ASI::EndRewrite(region);
    return true;
}

// Redirects "call rel32" at rva; chains another mod's redirection.
static bool hook_call(uintptr_t rva, void *replacement, drawOverlay_ptr *original)
{
    uintptr_t at = (uintptr_t)ASI::AddrOf(rva);
    if (*(uint8_t *)at != 0xe8)
    {
        logAPI->logError(LOG_TAG "no call at %08x; hook skipped", (unsigned)at);
        return false;
    }
    uintptr_t target = at + 5 + *(int32_t *)(at + 1);
    if (target != (uintptr_t)ASI::AddrOf(RVA_DRAW_OVERLAY))
    {
        logAPI->logInfo(LOG_TAG "call at %08x already redirected by another mod; chaining it", (unsigned)at);
    }
    *original = (drawOverlay_ptr)target;
    int32_t rel = (int32_t)((uintptr_t)replacement - (at + 5));
    return write_code(at + 1, &rel, sizeof(rel));
}

// Redirects "call rel32" at rva to replacement when it still calls expected
// (or chains another mod); returns the previous target, NULL on failure.
static void *hook_generic_call(uintptr_t rva, uintptr_t expected_rva, void *replacement)
{
    uintptr_t at = (uintptr_t)ASI::AddrOf(rva);
    if (*(uint8_t *)at != 0xe8)
    {
        logAPI->logError(LOG_TAG "no call at %08x; hook skipped", (unsigned)at);
        return NULL;
    }
    uintptr_t target = at + 5 + *(int32_t *)(at + 1);
    if (target != (uintptr_t)ASI::AddrOf(expected_rva))
    {
        logAPI->logInfo(LOG_TAG "call at %08x already redirected by another mod; chaining it", (unsigned)at);
    }
    int32_t rel = (int32_t)((uintptr_t)replacement - (at + 5));
    return write_code(at + 1, &rel, sizeof(rel)) ? (void *)target : NULL;
}

extern "C" __declspec(dllexport) void InitModule(SpellforceSpellFramework *framework)
{
    sfsf = framework;
    logAPI = sfsf->logAPI;
    if (ASI::GAME_BASE == 0)
    {
        logAPI->logError(LOG_TAG "SpellForce.exe not found; not installed");
        return;
    }
    load_settings();
    s_player_is_hostile = (playerIsHostile_ptr)ASI::AddrOf(RVA_PLAYER_IS_HOSTILE);
    s_fog_active = (fogActive_ptr)ASI::AddrOf(RVA_FOG_ACTIVE);
    s_fog_visible = (fogVisible_ptr)ASI::AddrOf(RVA_FOG_VISIBLE);
    s_object_position = (objectPosition_ptr)ASI::AddrOf(RVA_OBJECT_POSITION);
    bool render = hook_call(RVA_DRAW_CALL_RENDER, (void *)&drawRenderHook, &s_draw_render);
    bool other = hook_call(RVA_DRAW_CALL_OTHER, (void *)&drawOtherHook, &s_draw_other);
    if (s_cfg.red_enemies || s_cfg.green_others)
    {
        s_draw_bar = (drawBar_ptr)hook_generic_call(RVA_DRAW_BAR_CALL, RVA_DRAW_BAR, (void *)&drawBarHook);
        if (s_draw_bar)
        {
            s_bar_colour = (barColour_ptr)hook_generic_call(RVA_BAR_COLOUR_CALL, RVA_BAR_COLOUR, (void *)&barColourHook);
            if (!s_bar_colour)
            {
                logAPI->logError(LOG_TAG "colour hook FAILED; enemy bars keep the game colours");
            }
        }
    }
    if (render)
    {
        logAPI->logInfo(LOG_TAG "health bars always shown: own %d, workers %d, allies %d, enemies %d, "
                        "only damaged %d, max distance %d, toggle key 0x%02x, red enemies %d, green others %d%s",
                        s_cfg.own, s_cfg.workers, s_cfg.allies, s_cfg.enemies, s_cfg.only_damaged,
                        s_cfg.max_distance, s_cfg.toggle_key, s_cfg.red_enemies, s_cfg.green_others,
                        other ? "" : " (second call not hooked)");
    }
    else
    {
        logAPI->logError(LOG_TAG "hook FAILED; health bars unchanged");
    }
}

extern "C" __declspec(dllexport) SFMod *RegisterMod(SpellforceSpellFramework *framework)
{
    return framework->createModInfo(MOD_ID, HEALTH_BARS_VERSION, "Angry Catster",
                                    "Health bars always shown over units (own, allies, enemies).");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH)
    {
        ASI::Init(hinstDLL);
    }
    return TRUE;
}
