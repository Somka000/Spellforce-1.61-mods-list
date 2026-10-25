/*
 * Hero Autocast
 *
 * Right-clicking a spell on the bottom action bar toggles autocast for it.
 * Toggled spells are cast by the avatar / rune heroes through the same battle
 * AI that already drives the spells of every other unit; a pink outline marks
 * the toggled icons (its colour is set in the ini file). Meditation, which
 * restores all mana, is only cast while the mana is below a share of the
 * maximum set in the ini file (33% by default).
 *
 * Outside fights: some toggled spells (outside_combat in the ini file; by
 * default Healing, Greater Healing, Charisma, Enlightenment and Meditation)
 * are also cast while the avatar or hero stands idle: Meditation below the
 * mana share, the healing spells on the most wounded of the avatar and heroes
 * in range below heal_health_percent of their health, any other spell on the
 * caster once it has run out. The cast is ordered like a player's (it
 * checks only the mana for heroes: cooldowns are checked here).
 *
 * Summons: a toggled summon spell has a number of summons to keep (shown at
 * the icon's top right corner): "+" with a right click raises it, "-" with a
 * right click lowers it (0 turns autocast off). In and out of fights the
 * caster summons again while it has fewer of them; in a fight a due summon
 * goes before what the battle AI chose. Each summon is a spell running with
 * the caster as its source, which is how they are counted.
 *
 * Two things keep the vanilla battle AI from casting for the avatar and heroes:
 *  - their spells are not in the figure's action list (which the AI ranks);
 *    they live in the player's spell slots, mirrored client side in UiDbProxy
 *    together with their cooldowns;
 *  - CGdAiBattleDevelopement::CheckFigureCanDoSpellAction refuses every spell
 *    action of a main character or hero.
 * While the AI ranks actions for one of the local player's heroes, this mod
 * appends the toggled slot spells (with their cooldown) to the figure's action
 * list, lets the refusal through for exactly those spell lines, and removes
 * the injected actions again afterwards. The chosen injected spell is carried
 * out by the vanilla executor (other mods may retarget the hero's attacks), and
 * toggled auras of a hero are ended when its fight is over, like the AI does
 * for every other unit.
 */

#include "../../src/api/sfsf.h"

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <set>
#include <map>

SpellforceSpellFramework *sfsf;
SFLog *logAPI;

/* ------------------------------------------------------------------------ */
/* Game addresses (offsets relative to the image base, like ASI::AddrOf)     */
/* ------------------------------------------------------------------------ */

// CGdAiBattleDevelopement::CheckFigureCanDoSpellAction, at the
// "if (is_mainchar || is_hero) return 0;" test
#define RVA_AI_HERO_CHECK          0x35addd
#define RVA_AI_HERO_CHECK_ALLOW    0x35adfc    // continue with the normal checks
#define RVA_AI_HERO_CHECK_DENY     0x35adf5    // return 0
// CGdAiBattleDevelopement::vfunction3 (battle check), call to the action ranking
#define RVA_AI_RANK_ACTIONS_CALL   0x35b383
#define RVA_AI_RANK_ACTIONS        0x35c5c1
// ... and to the execution of the chosen action
#define RVA_AI_EXECUTE_CALL        0x35b4c6
#define RVA_AI_EXECUTE             0x35bb20
// CGdAiBattleDevelopement::figure_turn_off_battle_mode (ends auras when the fight is over)
#define RVA_AI_LEAVE_BATTLE        0x3607a7
#define AI_LEAVE_BATTLE_PROLOGUE   10
#define RVA_CGDSPELL_END_SPELL     0x34b0a0
// CGdFigureJobs::Trigger: calls to the idle jobs (JobDoNothing, JobWarriorNothing),
// (thiscall jobs, figure); they run again each time the idle animation ends
#define RVA_JOB_NOTHING_CALL       0x2f16dd
#define RVA_JOB_NOTHING            0x2ec620
#define RVA_JOB_WARRIOR_NOTHING_CALL 0x2f17e1
#define RVA_JOB_WARRIOR_NOTHING    0x2eeeb0
// CGdFigureToolBox::FigureCanCastSpell (figure, action): cooldowns of the action and mana
#define RVA_FIGURE_CAN_CAST_SPELL  0x2f82d0
// CGdFigureToolBox::doCastSpell (figure, spell, target index, target type, target
// position, check sight, any target): what a player's cast order runs
#define RVA_DO_CAST_SPELL          0x2fb310
#define RVA_GET_CURRENT_TICK       0x277290    // AutoClass14::getCurrentTick
#define TARGET_FIGURE              1

// Vtable slots
#define RVA_CUIBOTTOM_VT_CLICK     0x805d9c    // CUiBottom slot 205: action button clicked
#define RVA_CUIBOTTOM_VT_RENDER    0x805ca0    // CUiBottom slot 142: render
#define RVA_CUIMAIN_VTABLE         0x806adc    // CUiMain slot 0: scalar deleting destructor

#define RVA_UIDB_SPELL_OF_ITEM     0x5750d0    // UiDbProxy: spell item -> spell id
#define RVA_RESOURCE_SPELL_LINE    0x26e100    // CGdResource::GetSpellSpellline
#define RVA_RESOURCE_TARGET_SELF   0x26e410    // CGdResource::GetSpellLineIsTargetSelf
#define RVA_DSP_DISPLAY_PTR        0x928f5c

// CGdAiBattleDevelopement
#define AIBD_IS_MAINCHAR           0x3f2c
#define AIBD_IS_HERO               0x3f30
#define AIBD_CGDFIGURE             0x14
#define AIBD_CURRENT_FIGURE        0x129d0
#define AIBD_CURRENT_ACTION        0x3ef0

// CMnuBase
#define MNU_PARENT                 0x12c
#define MNU_SCREEN_RECT            0x144
#define MNU_VF_IS_VISIBLE          (0xf0 / 4)

// CUiBottom
#define CUIBOTTOM_ACTION_BUTTONS   0x39c
#define CUIBOTTOM_ACTION_COUNT     10

// CUiMain
#define CUIMAIN_RESOURCE           0x7428
#define CUIMAIN_CGDFIGURE          0x742c
#define CUIMAIN_CGDPLAYER          0x7434
#define CUIMAIN_CONTROLLER         0x743c
#define CUIMAIN_UIDBPROXY          0x7440
#define CUIMAIN_ELEMENTS_BEGIN     0x7558
#define CUIMAIN_ELEMENTS_END       0x755c
#define UI_ELEMENT_SIZE            0x20
#define UI_ELEMENT_FIGURES_BEGIN   0x14
#define UI_ELEMENT_FIGURES_END     0x18

#define CONTROLLER_LOCAL_PLAYER    0x8cc

// CGdPlayer / player record (AutoClass26)
#define CGDPLAYER_PLAYERS          0x6
#define PLAYER_SIZE                0x188
#define PLAYER_MAINCHAR_FIGURE     0xad
#define PLAYER_HERO_FIGURES        0xbf
#define PLAYER_HERO_COUNT          5

// UiDbProxy spell slots: 10 for the main character, 3 per hero
#define UIDB_MAINCHAR_SLOTS        0x10ff4
#define UIDB_HERO_SLOTS            0x11094
#define UIDB_SLOT_SIZE             0x10
#define UIDB_SLOT_ITEM             0x4
#define UIDB_SLOT_READY_TICK       0xc
#define MAINCHAR_SLOT_COUNT        10
#define HERO_SLOT_COUNT            3

#define EVENT_MOUSE_BUTTON         0x20

#define DSP_DISPLAY_DEVICE         0x338

#define FIGURE_ACTION_COUNT        15
// Meditation restores all mana; autocast only uses it below a share of the maximum
#define MEDITATION_DEFAULT_PERCENT 33
#define MEDITATION_KEY             "meditation_mana_percent"
#define OUTSIDE_COMBAT_KEY         "outside_combat"
#define HEAL_KEY                   "heal_health_percent"
#define HEAL_DEFAULT_PERCENT       70
// A cast ordered outside a fight is not ordered again for this long (the
// spell slots' cooldowns, mirrored for the interface, may lag behind)
#define ORDER_GUARD_TICKS          30
#define TICKS_PER_SECOND           10
#define SUMMON_COUNT_MAX           20
#define ACTION_EMPTY               0xffff

// Owner part of an autocast key
#define OWNER_MAINCHAR             0xffff
#define OWNER_HERO_BASE            0xff00      // + hero slot of the player
#define AUTOCAST_FILE              "sfsf\\hero_autocast.ini"

#define OUTLINE_DEFAULT_COLOR      0xff40d0    // RGB
#define OUTLINE_COLOR_KEY          "outline_color"
#define OUTLINE_THICKNESS          3.0f

static_assert(sizeof(GdFigure) == 0x2b3, "GdFigure layout");
static_assert(sizeof(SF_SGtFigureAction) == 0xc, "SGtFigureAction layout");

typedef void (__thiscall *bottom_click_ptr)(void *bottom, void *button, void *event);
typedef void (__thiscall *bottom_render_ptr)(void *bottom, float param_1, float param_2);
typedef void *(__thiscall *destructor_ptr)(void *object, uint8_t flags);
typedef char (__thiscall *mnu_is_visible_ptr)(void *control);
typedef void (__thiscall *rank_actions_ptr)(void *battle);
typedef void (__thiscall *battle_step_ptr)(void *battle);
typedef void (__thiscall *end_spell_ptr)(SF_CGdSpell *spell, uint16_t spell_index);
typedef char (__thiscall *spell_of_item_ptr)(void *uidb, uint16_t item, uint16_t *spell_id);
typedef uint16_t (__thiscall *spell_line_ptr)(void *resource, uint16_t spell_id);
typedef int (__thiscall *spell_line_flag_ptr)(void *resource, uint16_t spell_line);
typedef void (__thiscall *job_ptr)(SF_CGdFigureJobs *jobs, uint32_t figure);
typedef int (__thiscall *can_cast_ptr)(SF_CGdFigureToolbox *toolbox, uint32_t figure, SF_SGtFigureAction *action);
typedef int (__thiscall *do_cast_ptr)(SF_CGdFigureToolbox *toolbox, uint32_t figure, uint32_t spell, uint32_t target,
                                      uint32_t target_type, SF_Coord position, int check_sight, int any_target);
typedef uint32_t (__thiscall *tick_ptr)(AutoClass14 *ticks);

static uintptr_t s_base;
static bottom_click_ptr s_orig_bottom_click;
static bottom_render_ptr s_orig_bottom_render;
static destructor_ptr s_orig_cuimain_destructor;
static rank_actions_ptr s_rank_actions;
static battle_step_ptr s_execute_action;          // whatever the call targeted (maybe another mod)
static battle_step_ptr s_vanilla_execute_action;
static battle_step_ptr s_leave_battle_trampoline;
static end_spell_ptr s_end_spell;
static spell_of_item_ptr s_spell_of_item;
static spell_line_ptr s_spell_line;
static spell_line_flag_ptr s_spell_line_is_target_self;
static job_ptr s_job_nothing;
static job_ptr s_job_warrior_nothing;
static can_cast_ptr s_figure_can_cast;
static do_cast_ptr s_do_cast;
static tick_ptr s_current_tick;

// The game's CUiMain, captured when the bottom bar renders. Its UiDbProxy holds
// the local player's hero spell slots and cooldowns.
static void *s_cuimain;

// (owner << 16) | spell line
static std::set<uint32_t> s_autocast;
static D3DCOLOR s_outline_color = D3DCOLOR_ARGB(0xff, 0, 0, 0) | OUTLINE_DEFAULT_COLOR;
static unsigned s_meditation_percent = MEDITATION_DEFAULT_PERCENT;
static unsigned s_heal_percent = HEAL_DEFAULT_PERCENT;
// Spell lines also cast outside fights
static std::set<uint16_t> s_outside_combat = {kGdSpellLineHealing, kGdSpellLineGreaterHealing, kGdSpellLineCharisma,
                                              kGdSpellLineEnlightenment, kGdSpellLineMeditation};
// (figure << 16) | spell line -> tick before which a cast ordered by the mod is
// not ordered again
static std::map<uint32_t, uint32_t> s_ordered;
// Autocast key -> number of summons to keep (toggled summon spells only)
static std::map<uint32_t, int> s_summon_counts;
// Keys already reported to the log by the battle AI hook
static std::set<uint32_t> s_reported;
// Spell lines injected into the last ranked hero's action list
static void *s_injected_battle;
static uint16_t s_injected_figure;
static std::set<uint16_t> s_injected_lines;

static inline uintptr_t addr_of(uintptr_t rva)
{
    return s_base + rva;
}

template <typename T>
static inline T field(void *object, uint32_t offset)
{
    return *(T *)((uint8_t *)object + offset);
}

/* ------------------------------------------------------------------------ */
/* Local player's heroes and their spell slots                               */
/* ------------------------------------------------------------------------ */

struct HeroSlots
{
    uint16_t owner;     // OWNER_MAINCHAR or OWNER_HERO_BASE + hero slot
    uint8_t *slots;     // UiDbProxy spell slots of this hero
    int count;
};

// Finds the spell slots of a figure that is the local player's main character
// or one of their heroes.
static bool get_hero_slots(void *cuimain, uint16_t figure_index, HeroSlots *hero)
{
    uint8_t *controller = field<uint8_t *>(cuimain, CUIMAIN_CONTROLLER);
    uint8_t *players = field<uint8_t *>(cuimain, CUIMAIN_CGDPLAYER);
    uint8_t *uidb = field<uint8_t *>(cuimain, CUIMAIN_UIDBPROXY);
    if (figure_index == 0 || !controller || !players || !uidb)
    {
        return false;
    }
    uint16_t player = field<uint16_t>(controller, CONTROLLER_LOCAL_PLAYER);
    uint8_t *record = players + CGDPLAYER_PLAYERS + player * PLAYER_SIZE;
    if (field<uint16_t>(record, PLAYER_MAINCHAR_FIGURE) == figure_index)
    {
        hero->owner = OWNER_MAINCHAR;
        hero->slots = uidb + UIDB_MAINCHAR_SLOTS;
        hero->count = MAINCHAR_SLOT_COUNT;
        return true;
    }
    for (int i = 0; i < PLAYER_HERO_COUNT; i++)
    {
        if (field<uint16_t>(record, PLAYER_HERO_FIGURES + i * 2) == figure_index)
        {
            hero->owner = OWNER_HERO_BASE + i;
            hero->slots = uidb + UIDB_HERO_SLOTS + i * HERO_SLOT_COUNT * UIDB_SLOT_SIZE;
            hero->count = HERO_SLOT_COUNT;
            return true;
        }
    }
    return false;
}

// The local player's avatar and heroes (figure indices, 0 = none)
static int get_party(void *cuimain, uint16_t *figures)
{
    uint8_t *controller = field<uint8_t *>(cuimain, CUIMAIN_CONTROLLER);
    uint8_t *players = field<uint8_t *>(cuimain, CUIMAIN_CGDPLAYER);
    if (!controller || !players)
    {
        return 0;
    }
    uint16_t player = field<uint16_t>(controller, CONTROLLER_LOCAL_PLAYER);
    uint8_t *record = players + CGDPLAYER_PLAYERS + player * PLAYER_SIZE;
    int count = 0;
    figures[count++] = field<uint16_t>(record, PLAYER_MAINCHAR_FIGURE);
    for (int i = 0; i < PLAYER_HERO_COUNT; i++)
    {
        figures[count++] = field<uint16_t>(record, PLAYER_HERO_FIGURES + i * 2);
    }
    return count;
}

// Spell in a hero spell slot; false when the slot is empty.
static bool get_slot_spell(void *cuimain, const HeroSlots &hero, int index,
                           uint16_t *spell_line, uint16_t *spell_id, uint32_t *ready_tick)
{
    uint8_t *slot = hero.slots + index * UIDB_SLOT_SIZE;
    uint16_t item = (uint16_t)field<uint32_t>(slot, UIDB_SLOT_ITEM);
    if (item == 0 || !s_spell_of_item(field<void *>(cuimain, CUIMAIN_UIDBPROXY), item, spell_id))
    {
        return false;
    }
    *spell_line = s_spell_line(field<void *>(cuimain, CUIMAIN_RESOURCE), *spell_id);
    *ready_tick = field<uint32_t>(slot, UIDB_SLOT_READY_TICK);
    return *spell_line != 0;
}

/* ------------------------------------------------------------------------ */
/* Autocast registry                                                         */
/* ------------------------------------------------------------------------ */

static inline uint32_t autocast_key(uint16_t owner, uint16_t spell_line)
{
    return ((uint32_t)owner << 16) | spell_line;
}

static bool autocast_enabled(uint16_t owner, uint16_t spell_line)
{
    return s_autocast.count(autocast_key(owner, spell_line)) != 0;
}

static void save_autocast();

static void load_autocast()
{
    FILE *f = fopen(AUTOCAST_FILE, "r");
    if (!f)
    {
        return;
    }
    char line[128];
    unsigned owner, spell_line, color;
    unsigned percent;
    bool has_color = false;
    bool has_meditation = false;
    bool has_outside = false;
    bool has_heal = false;
    while (fgets(line, sizeof(line), f))
    {
        if (sscanf(line, " " OUTLINE_COLOR_KEY " = #%x", &color) == 1 ||
            sscanf(line, " " OUTLINE_COLOR_KEY " = %x", &color) == 1)
        {
            s_outline_color = D3DCOLOR_ARGB(0xff, 0, 0, 0) | (color & 0xffffff);
            has_color = true;
            continue;
        }
        if (sscanf(line, " " MEDITATION_KEY " = %u", &percent) == 1)
        {
            s_meditation_percent = percent > 100 ? 100 : percent;
            has_meditation = true;
            continue;
        }
        if (sscanf(line, " " HEAL_KEY " = %u", &percent) == 1)
        {
            s_heal_percent = percent > 100 ? 100 : percent;
            has_heal = true;
            continue;
        }
        int offset = 0;
        if (sscanf(line, " " OUTSIDE_COMBAT_KEY " =%n", &offset) == 0 && offset > 0)
        {
            s_outside_combat.clear();
            const char *at = line + offset;
            int used = 0;
            while (sscanf(at, "%u%n", &spell_line, &used) == 1)
            {
                s_outside_combat.insert((uint16_t)spell_line);
                at += used;
            }
            has_outside = true;
            continue;
        }
        unsigned summons = 0;
        if (sscanf(line, "%u %u %u", &owner, &spell_line, &summons) < 2)
        {
            continue;
        }
        // Skip entries of the 1.0.0 format, which keyed heroes by unit id
        if (owner == OWNER_MAINCHAR || (owner >= OWNER_HERO_BASE && owner < OWNER_HERO_BASE + PLAYER_HERO_COUNT))
        {
            s_autocast.insert(autocast_key(owner, spell_line));
            if (summons > 0)
            {
                s_summon_counts[autocast_key(owner, spell_line)] =
                    summons > SUMMON_COUNT_MAX ? SUMMON_COUNT_MAX : (int)summons;
            }
        }
    }
    fclose(f);
    logAPI->logInfo("[autocast] loaded %u toggled spell(s) from %s", (unsigned)s_autocast.size(), AUTOCAST_FILE);
    if (!has_color || !has_meditation || !has_outside || !has_heal)
    {
        save_autocast();    // adds missing settings to the file, so that they can be found
    }
}

static void save_autocast()
{
    FILE *f = fopen(AUTOCAST_FILE, "w");
    if (!f)
    {
        logAPI->logWarning("[autocast] could not write %s", AUTOCAST_FILE);
        return;
    }
    fprintf(f, "# Hero Autocast\n");
    fprintf(f, "# Outline colour of autocast spells as RGB hex, like #ff40d0; read when the game starts\n");
    fprintf(f, "%s = #%06x\n", OUTLINE_COLOR_KEY, (unsigned)(s_outline_color & 0xffffff));
    fprintf(f, "# Autocast Meditation only while mana is below this percentage of the maximum (0 - 100); read when the game starts\n");
    fprintf(f, "%s = %u\n", MEDITATION_KEY, s_meditation_percent);
    fprintf(f, "# Spell lines also cast outside fights, while the caster stands idle (2 Healing, 45 Greater\n");
    fprintf(f, "# Healing, 125 Charisma, 130 Enlightenment, 132 Meditation, ...); healing goes to the most\n");
    fprintf(f, "# wounded of the avatar and heroes in range, anything else to the caster once it has run out\n");
    fprintf(f, "%s =", OUTSIDE_COMBAT_KEY);
    for (std::set<uint16_t>::iterator it = s_outside_combat.begin(); it != s_outside_combat.end(); ++it)
    {
        fprintf(f, " %u", *it);
    }
    fprintf(f, "\n");
    fprintf(f, "# Healing outside fights only for health below this percentage of the maximum (0 - 100)\n");
    fprintf(f, "%s = %u\n", HEAL_KEY, s_heal_percent);
    fprintf(f, "# Toggled spells: <owner> <spell line> [summons to keep]; owner 65535 = main character,\n");
    fprintf(f, "# 65280 + n = hero slot n\n");
    for (std::set<uint32_t>::iterator it = s_autocast.begin(); it != s_autocast.end(); ++it)
    {
        std::map<uint32_t, int>::iterator count = s_summon_counts.find(*it);
        if (count != s_summon_counts.end())
        {
            fprintf(f, "%u %u %d\n", *it >> 16, *it & 0xffff, count->second);
        }
        else
        {
            fprintf(f, "%u %u\n", *it >> 16, *it & 0xffff);
        }
    }
    fclose(f);
}

/* ------------------------------------------------------------------------ */
/* Summons                                                                   */
/* ------------------------------------------------------------------------ */

static bool is_summon(uint16_t spell_line)
{
    return sfsf->spellAPI->hasSpellTag(spell_line, SpellTag::SUMMON_SPELL);
}

// Summons to keep for a toggled summon spell (1 unless set)
static int summons_wanted(uint16_t owner, uint16_t spell_line)
{
    std::map<uint32_t, int>::iterator it = s_summon_counts.find(autocast_key(owner, spell_line));
    return it != s_summon_counts.end() ? it->second : 1;
}

// Summons of the line the caster has: each is a spell of the line running
// with the caster as its source and the summoned figure as its target
static int count_summons(SF_CGdSpell *spells, SF_CGdFigure *figures, uint16_t caster, uint16_t spell_line)
{
    int count = 0;
    for (int i = 1; i <= spells->max_used && i < 800; i++)
    {
        SF_GdSpell *spell = &spells->active_spell_list[i];
        if (spell->spell_line == spell_line && spell->spell_id != 0 && spell->source.entity_type == 1 &&
            spell->source.entity_index == caster && spell->target.entity_type == 1 &&
            spell->target.entity_index != caster && sfsf->figureAPI->isAlive(figures, spell->target.entity_index))
        {
            count++;
        }
    }
    return count;
}

/* ------------------------------------------------------------------------ */
/* Battle AI hooks                                                           */
/* ------------------------------------------------------------------------ */

// Returns 1 when the vanilla checks should continue, 0 when the spell is refused.
static int __thiscall ai_allow_spell_action(void *battle, uint32_t action)
{
    if (!field<int>(battle, AIBD_IS_MAINCHAR) && !field<int>(battle, AIBD_IS_HERO))
    {
        return 1;
    }
    uint16_t figure_index = field<uint16_t>(battle, AIBD_CURRENT_FIGURE);
    uint16_t spell_line = (uint16_t)(action & 0xffff);
    HeroSlots hero;
    if (!s_cuimain || !get_hero_slots(s_cuimain, figure_index, &hero) || !autocast_enabled(hero.owner, spell_line))
    {
        return 0;
    }
    if (s_reported.insert(autocast_key(hero.owner, spell_line)).second)
    {
        logAPI->logInfo("[autocast] battle AI now considers spell line %u (spell %u) for figure %u",
                        spell_line, (unsigned)(action >> 16), figure_index);
    }
    return 1;
}

// Called in place of the mainchar/hero test. ECX = CGdAiBattleDevelopement*,
// [EBP + 8] = first dword of the SGtFigureAction argument (spell line | spell id << 16).
static void __attribute__((naked)) ai_hero_check_stub()
{
    asm ("pushl 8(%%ebp)  \n\t"
         "call  %P0       \n\t"
         "ret             \n\t"
         : : "i" (ai_allow_spell_action));
}

// True when a spell of the line is active on the figure.
static bool has_active_spell_line(CGdAIBattleData *data, uint16_t figure_index, uint16_t spell_line)
{
    uint16_t node = sfsf->figureAPI->getSpellJobStartNode(data->CGdFigure, figure_index);
    for (int guard = 0; node != 0 && guard < 256; guard++)
    {
        uint16_t spell_index = sfsf->toolboxAPI->getSpellIndexFromDLL((uint32_t *)data->CGdDoubleLinkList, node);
        if (data->CGdSpell->active_spell_list[spell_index].spell_line == spell_line)
        {
            return true;
        }
        node = sfsf->toolboxAPI->getNextNode((uint32_t *)data->CGdDoubleLinkList, node);
    }
    return false;
}

// Replaces the call to the action ranking of the battle check. For the local
// player's heroes the toggled slot spells are added to the figure's action
// list for the duration of the ranking.
static void __thiscall ai_rank_actions_hook(void *battle)
{
    if ((!field<int>(battle, AIBD_IS_MAINCHAR) && !field<int>(battle, AIBD_IS_HERO)) || !s_cuimain ||
        s_autocast.empty())
    {
        s_rank_actions(battle);
        return;
    }
    uint16_t figure_index = field<uint16_t>(battle, AIBD_CURRENT_FIGURE);
    HeroSlots hero;
    if (!get_hero_slots(s_cuimain, figure_index, &hero))
    {
        s_rank_actions(battle);
        return;
    }

    SF_SGtFigureAction *actions = field<SF_CGdFigure *>(battle, AIBD_CGDFIGURE)->figures[figure_index].actions;
    CGdAIBattleData *data = &((SF_CGdBattleDevelopment *)battle)->battleData;
    void *resource = field<void *>(s_cuimain, CUIMAIN_RESOURCE);
    s_injected_battle = battle;
    s_injected_figure = figure_index;
    s_injected_lines.clear();
    int injected[FIGURE_ACTION_COUNT];
    int injected_count = 0;
    int free_action = 0;
    for (int i = 0; i < hero.count; i++)
    {
        uint16_t spell_line, spell_id;
        uint32_t ready_tick;
        if (!get_slot_spell(s_cuimain, hero, i, &spell_line, &spell_id, &ready_tick) ||
            !autocast_enabled(hero.owner, spell_line))
        {
            continue;
        }
        // A spell only for the caster is cast on the hero (see ai_execute_action_hook);
        // the AI would judge it by another unit, so wait until it has run out
        if (s_spell_line_is_target_self(resource, spell_line) && has_active_spell_line(data, figure_index, spell_line))
        {
            continue;
        }
        if (is_summon(spell_line))
        {
            continue;   // cast by cast_due (in place of the AI's choice), up to the number set
        }
        if (spell_line == kGdSpellLineMeditation)
        {
            SF_CGdFigure *figures = data->CGdFigure;
            uint32_t mana = sfsf->figureAPI->getCurrentStat(figures, figure_index, MANA);
            uint32_t max_mana = sfsf->figureAPI->getMaxStat(figures, figure_index, MANA);
            if (mana * 100 >= max_mana * s_meditation_percent)
            {
                continue;
            }
        }
        while (free_action < FIGURE_ACTION_COUNT && actions[free_action].type != ACTION_EMPTY)
        {
            free_action++;
        }
        if (free_action == FIGURE_ACTION_COUNT)
        {
            break;
        }
        // FigureCanCastSpell refuses the action until the game tick reaches the cooldowns
        actions[free_action].type = spell_line;
        actions[free_action].subtype = spell_id;
        actions[free_action].cooldown_action = ready_tick;
        actions[free_action].cooldown_global = 0;
        injected[injected_count++] = free_action;
        s_injected_lines.insert(spell_line);
    }

    s_rank_actions(battle);

    for (int i = 0; i < injected_count; i++)
    {
        memset(&actions[injected[i]], 0xff, sizeof(SF_SGtFigureAction));
    }
}

// Replaces the call to the execution of the action chosen by the ranking. Another
// mod may hook this call to retarget the hero (e.g. at the nearest enemy); an
// injected spell is handed to the vanilla executor instead, so self buffs, auras
// and support spells keep the target the battle AI chose for them. The AI does
// not know spells that can only be cast on the caster (like Enlightenment) and
// may pick another unit for them; those are turned back on the caster.
static bool cast_due(SF_CGdFigureJobs *jobs, uint16_t figure, bool in_fight);

static void __thiscall ai_execute_action_hook(void *battle)
{
    CGdAIBattleData *battle_data = &((SF_CGdBattleDevelopment *)battle)->battleData;
    if ((field<int>(battle, AIBD_IS_MAINCHAR) || field<int>(battle, AIBD_IS_HERO)) &&
        cast_due(battle_data->CGdFigureJobs, battle_data->current_figure, true))
    {
        return;     // a summon is due: ordered in place of the AI's choice
    }
    uint16_t action = field<uint16_t>(battle, AIBD_CURRENT_ACTION);
    if (battle == s_injected_battle && field<uint16_t>(battle, AIBD_CURRENT_FIGURE) == s_injected_figure &&
        s_injected_lines.count(action))
    {
        CGdAIBattleData *data = &((SF_CGdBattleDevelopment *)battle)->battleData;
        if (s_cuimain && s_spell_line_is_target_self(field<void *>(s_cuimain, CUIMAIN_RESOURCE), action) &&
            (data->current_target.entity_type != 1 || data->current_target.entity_index != data->current_figure))
        {
            data->current_target.entity_type = 1;
            data->current_target.entity_index = data->current_figure;
            data->current_target.position = data->current_figure_pos;
        }
        logAPI->logInfo("[autocast] figure %u casts spell line %u (spell %u) on target type %u index %u",
                        data->current_figure, action, data->current_action.subtype,
                        data->current_target.entity_type, data->current_target.entity_index);
        s_vanilla_execute_action(battle);
        return;
    }
    s_execute_action(battle);
}

// Runs before figure_turn_off_battle_mode (replaced by SFSF). It ends the auras of
// a unit once no enemy is left, but skips heroes and main characters; do the same
// for the auras a hero has toggled for autocast.
static void __thiscall ai_leave_battle_hook(SF_CGdBattleDevelopment *battle)
{
    CGdAIBattleData *data = &battle->battleData;
    HeroSlots hero;
    if ((field<int>(battle, AIBD_IS_MAINCHAR) || field<int>(battle, AIBD_IS_HERO)) &&
        data->enemy_figures.entityCount == 0 && s_cuimain && !s_autocast.empty() &&
        get_hero_slots(s_cuimain, data->current_figure, &hero))
    {
        uint16_t auras[16];
        int aura_count = 0;
        uint16_t node = sfsf->figureAPI->getSpellJobStartNode(data->CGdFigure, data->current_figure);
        while (node != 0 && aura_count < 16)
        {
            uint16_t spell_index = sfsf->toolboxAPI->getSpellIndexFromDLL((uint32_t *)data->CGdDoubleLinkList, node);
            SF_GdSpell *spell = &data->CGdSpell->active_spell_list[spell_index];
            if (sfsf->spellAPI->hasSpellTag(spell->spell_line, SpellTag::AURA_SPELL) &&
                autocast_enabled(hero.owner, spell->spell_line))
            {
                auras[aura_count++] = spell_index;
            }
            node = sfsf->toolboxAPI->getNextNode((uint32_t *)data->CGdDoubleLinkList, node);
        }
        for (int i = 0; i < aura_count; i++)
        {
            logAPI->logInfo("[autocast] figure %u left the fight, ending aura (spell line %u)", data->current_figure,
                            data->CGdSpell->active_spell_list[auras[i]].spell_line);
            s_end_spell(data->CGdSpell, auras[i]);
        }
    }
    s_leave_battle_trampoline(battle);
}

/* ------------------------------------------------------------------------ */
/* Outside fights                                                            */
/* ------------------------------------------------------------------------ */

static bool is_heal(uint16_t spell_line)
{
    return spell_line == kGdSpellLineHealing || spell_line == kGdSpellLineGreaterHealing;
}

// The most wounded of the avatar and heroes within the spell's range, below
// the share of health set; 0 when none
static uint16_t heal_target(SF_CGdFigureJobs *jobs, uint16_t caster, uint16_t spell_id)
{
    SF_CGdResourceSpell data;
    memset(&data, 0, sizeof(data));
    sfsf->spellAPI->getResourceSpellData(jobs->CGdResource, &data, spell_id);
    uint16_t party[1 + PLAYER_HERO_COUNT];
    int count = get_party(s_cuimain, party);
    uint16_t best = 0;
    unsigned best_percent = s_heal_percent;
    for (int i = 0; i < count; i++)
    {
        uint16_t figure = party[i];
        if (figure == 0 || !sfsf->figureAPI->isAlive(jobs->CGdFigure, figure))
        {
            continue;
        }
        unsigned percent = sfsf->figureAPI->getCurrentHealthPercent(jobs->CGdFigure, figure);
        if (percent >= best_percent)
        {
            continue;
        }
        SF_Coord from = jobs->CGdFigure->figures[caster].position;
        SF_Coord to = jobs->CGdFigure->figures[figure].position;
        if (figure != caster && sfsf->toolboxAPI->getDistance(&from, &to) > data.max_range)
        {
            continue;
        }
        best = figure;
        best_percent = percent;
    }
    return best;
}

// Orders the first toggled spell (in slot order) that is due for the avatar or
// a hero of the local player: summons below the number set, and outside a fight
// the spells set for outside fights. True when a cast was ordered.
static bool cast_due(SF_CGdFigureJobs *jobs, uint16_t figure, bool in_fight)
{
    HeroSlots hero;
    if (!jobs || !s_cuimain || s_autocast.empty() || !get_hero_slots(s_cuimain, figure, &hero))
    {
        return false;
    }
    SF_CGdFigure *figures = jobs->CGdFigure;
    SF_CGdFigureToolbox *toolbox = jobs->CGdFigureToolBox;
    if (!sfsf->figureAPI->isAlive(figures, figure) ||
        sfsf->toolboxAPI->hasSpellOnIt(toolbox, figure, kGdSpellLineConfuse) ||
        sfsf->toolboxAPI->hasSpellOnIt(toolbox, figure, kGdSpellLineConfuseArea))
    {
        return false;   // (a confused hero refuses casts, with a complaint)
    }
    uint32_t now = s_current_tick(toolbox->maybe_random);
    for (int i = 0; i < hero.count; i++)
    {
        uint16_t spell_line, spell_id;
        uint32_t ready_tick;
        if (!get_slot_spell(s_cuimain, hero, i, &spell_line, &spell_id, &ready_tick) ||
            !autocast_enabled(hero.owner, spell_line))
        {
            continue;
        }
        bool summon = is_summon(spell_line);
        if (!summon && (in_fight || !s_outside_combat.count(spell_line)))
        {
            continue;
        }
        uint32_t key = ((uint32_t)figure << 16) | spell_line;
        std::map<uint32_t, uint32_t>::iterator ordered = s_ordered.find(key);
        if (ordered != s_ordered.end() && now < ordered->second)
        {
            continue;
        }
        // cooldown and mana (a hero's cast order checks only the mana, and
        // complains when it is short)
        SF_SGtFigureAction action;
        action.type = spell_line;
        action.subtype = spell_id;
        action.cooldown_action = ready_tick;
        action.cooldown_global = 0;
        if (!s_figure_can_cast(toolbox, figure, &action))
        {
            continue;
        }
        uint16_t target = figure;
        int summons = 0;
        if (summon)
        {
            summons = count_summons(jobs->CGdSpell, figures, figure, spell_line);
            if (summons >= summons_wanted(hero.owner, spell_line))
            {
                continue;
            }
        }
        else if (spell_line == kGdSpellLineMeditation)
        {
            uint32_t mana = sfsf->figureAPI->getCurrentStat(figures, figure, MANA);
            uint32_t max_mana = sfsf->figureAPI->getMaxStat(figures, figure, MANA);
            if (mana * 100 >= max_mana * s_meditation_percent)
            {
                continue;
            }
        }
        else if (is_heal(spell_line))
        {
            target = heal_target(jobs, figure, spell_id);
            if (target == 0)
            {
                continue;
            }
        }
        else if (sfsf->toolboxAPI->hasSpellOnIt(toolbox, figure, spell_line))
        {
            continue;
        }
        SF_Coord position = figures->figures[target].position;
        int ok = s_do_cast(toolbox, figure, spell_id, target, TARGET_FIGURE, position, 0, 0);
        // a summon only counts once its cast is done
        SF_CGdResourceSpell data;
        memset(&data, 0, sizeof(data));
        sfsf->spellAPI->getResourceSpellData(jobs->CGdResource, &data, spell_id);
        s_ordered[key] = now + ORDER_GUARD_TICKS + (summon ? data.cast_time * TICKS_PER_SECOND / 1000 : 0);
        if (summon)
        {
            logAPI->logInfo("[autocast] figure %u %s: summons (spell line %u, has %d of %d)%s", figure,
                            in_fight ? "battle AI" : "idle", spell_line, summons,
                            summons_wanted(hero.owner, spell_line), ok ? "" : " - refused");
        }
        else
        {
            logAPI->logInfo("[autocast] figure %u idle: casts spell line %u (spell %u) on figure %u%s", figure,
                            spell_line, spell_id, target, ok ? "" : " - refused");
        }
        return ok != 0;
    }
    return false;
}

static void __thiscall job_nothing_hook(SF_CGdFigureJobs *jobs, uint32_t figure)
{
    s_job_nothing(jobs, figure);
    cast_due(jobs, (uint16_t)figure, false);
}

static void __thiscall job_warrior_nothing_hook(SF_CGdFigureJobs *jobs, uint32_t figure)
{
    s_job_warrior_nothing(jobs, figure);
    cast_due(jobs, (uint16_t)figure, false);
}

/* ------------------------------------------------------------------------ */
/* Bottom bar                                                                */
/* ------------------------------------------------------------------------ */

static void *get_cuimain(void *bottom)
{
    void *parent = field<void *>(bottom, MNU_PARENT);
    if (parent && field<uintptr_t>(parent, 0) == addr_of(RVA_CUIMAIN_VTABLE))
    {
        return parent;
    }
    return NULL;
}

static int get_button_index(void *bottom, void *button)
{
    for (int i = 0; i < CUIBOTTOM_ACTION_COUNT; i++)
    {
        if (field<void *>(bottom, CUIBOTTOM_ACTION_BUTTONS + i * 4) == button)
        {
            return i;
        }
    }
    return -1;
}

// The UI element shown on an action button: spell line, spell id and the
// selected figures that own the action.
struct ActionSlot
{
    uint16_t spell_line;
    uint16_t spell_id;
    uint16_t *figures_begin;
    uint16_t *figures_end;
};

static bool get_action_slot(void *cuimain, int index, ActionSlot *slot)
{
    uint8_t *begin = field<uint8_t *>(cuimain, CUIMAIN_ELEMENTS_BEGIN);
    uint8_t *end = field<uint8_t *>(cuimain, CUIMAIN_ELEMENTS_END);
    if (!begin || index < 0 || index >= (end - begin) / UI_ELEMENT_SIZE)
    {
        return false;
    }
    uint8_t *element = begin + index * UI_ELEMENT_SIZE;
    slot->spell_line = *(uint16_t *)(element);
    slot->spell_id = *(uint16_t *)(element + 2);
    slot->figures_begin = *(uint16_t **)(element + UI_ELEMENT_FIGURES_BEGIN);
    slot->figures_end = *(uint16_t **)(element + UI_ELEMENT_FIGURES_END);
    // Action ids from 10000 up are UI commands, not spell lines
    return slot->spell_line != 0 && slot->spell_line < 10000;
}

// Number of selected heroes owning the slot's spell, and how many have it toggled on.
static void count_autocast(void *cuimain, const ActionSlot &slot, int *heroes, int *enabled)
{
    *heroes = 0;
    *enabled = 0;
    for (uint16_t *it = slot.figures_begin; it && it < slot.figures_end; ++it)
    {
        HeroSlots hero;
        if (!get_hero_slots(cuimain, *it, &hero))
        {
            continue;
        }
        (*heroes)++;
        if (autocast_enabled(hero.owner, slot.spell_line))
        {
            (*enabled)++;
        }
    }
}

// Right click on an action button: toggles autocast; with "+" or "-" held
// (delta 1 or -1), changes the number of summons to keep of a summon spell
static void toggle_autocast(void *bottom, void *button, int delta)
{
    void *cuimain = get_cuimain(bottom);
    int index = get_button_index(bottom, button);
    ActionSlot slot;
    if (!cuimain || !get_action_slot(cuimain, index, &slot))
    {
        return;
    }
    int heroes, enabled;
    count_autocast(cuimain, slot, &heroes, &enabled);
    if (heroes == 0)
    {
        logAPI->logInfo("[autocast] spell line %u on button %d has no hero caster", slot.spell_line, index);
        return;
    }
    if (delta != 0 && is_summon(slot.spell_line))
    {
        for (uint16_t *it = slot.figures_begin; it < slot.figures_end; ++it)
        {
            HeroSlots hero;
            if (!get_hero_slots(cuimain, *it, &hero))
            {
                continue;
            }
            uint32_t key = autocast_key(hero.owner, slot.spell_line);
            int count = (autocast_enabled(hero.owner, slot.spell_line) ? summons_wanted(hero.owner, slot.spell_line)
                                                                       : 0) + delta;
            count = count < 0 ? 0 : count > SUMMON_COUNT_MAX ? SUMMON_COUNT_MAX : count;
            if (count > 0)
            {
                s_autocast.insert(key);
                s_summon_counts[key] = count;
            }
            else
            {
                s_autocast.erase(key);
            }
            logAPI->logInfo("[autocast] figure %u spell line %u: keep %d summon(s)", *it, slot.spell_line, count);
        }
        save_autocast();
        return;
    }
    bool enable = enabled < heroes;
    for (uint16_t *it = slot.figures_begin; it < slot.figures_end; ++it)
    {
        HeroSlots hero;
        if (!get_hero_slots(cuimain, *it, &hero))
        {
            continue;
        }
        uint32_t key = autocast_key(hero.owner, slot.spell_line);
        if (enable)
        {
            s_autocast.insert(key);
        }
        else
        {
            s_autocast.erase(key);
        }
        logAPI->logInfo("[autocast] figure %u spell line %u (spell %u): autocast %s",
                        *it, slot.spell_line, slot.spell_id, enable ? "ON" : "OFF");
    }
    save_autocast();
}

static IDirect3DDevice9 *get_device();

// Cursor position in back buffer pixels, the space of the controls' screen rects.
static bool get_cursor(float *x, float *y)
{
    HWND window = GetForegroundWindow();
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    POINT cursor;
    RECT client;
    if (process != GetCurrentProcessId() || !GetCursorPos(&cursor) || !ScreenToClient(window, &cursor) ||
        !GetClientRect(window, &client) || client.right <= 0 || client.bottom <= 0)
    {
        return false;
    }
    float scale_x = 1.0f, scale_y = 1.0f;
    IDirect3DDevice9 *device = get_device();
    IDirect3DSurface9 *target = NULL;
    if (device && SUCCEEDED(device->GetRenderTarget(0, &target)))
    {
        D3DSURFACE_DESC desc;
        if (SUCCEEDED(target->GetDesc(&desc)))
        {
            scale_x = (float)desc.Width / client.right;
            scale_y = (float)desc.Height / client.bottom;
        }
        target->Release();
    }
    *x = cursor.x * scale_x;
    *y = cursor.y * scale_y;
    return true;
}

static int button_under_cursor(void *bottom)
{
    float x, y;
    if (!get_cursor(&x, &y))
    {
        return -1;
    }
    for (int i = 0; i < CUIBOTTOM_ACTION_COUNT; i++)
    {
        void *button = field<void *>(bottom, CUIBOTTOM_ACTION_BUTTONS + i * 4);
        if (!button || !((mnu_is_visible_ptr)(field<void **>(button, 0)[MNU_VF_IS_VISIBLE]))(button))
        {
            continue;
        }
        float *rect = (float *)((uint8_t *)button + MNU_SCREEN_RECT);
        if (x >= rect[0] && x < rect[0] + rect[2] && y >= rect[1] && y < rect[1] + rect[3])
        {
            return i;
        }
    }
    return -1;
}

// Called every frame the bar is drawn: a right click (press and release over the
// same action button) toggles autocast for its spell.
static void poll_right_click(void *bottom)
{
    static bool was_down = false;
    static int pressed_button = -1;
    bool down = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (down && !was_down)
    {
        pressed_button = button_under_cursor(bottom);
    }
    else if (!down && was_down)
    {
        if (pressed_button >= 0 && button_under_cursor(bottom) == pressed_button)
        {
            bool plus = (GetAsyncKeyState(VK_OEM_PLUS) & 0x8000) || (GetAsyncKeyState(VK_ADD) & 0x8000);
            bool minus = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) || (GetAsyncKeyState(VK_SUBTRACT) & 0x8000);
            toggle_autocast(bottom, field<void *>(bottom, CUIBOTTOM_ACTION_BUTTONS + pressed_button * 4),
                            plus == minus ? 0 : plus ? 1 : -1);
        }
        pressed_button = -1;
    }
    was_down = down;
}

static void __thiscall bottom_click_hook(void *bottom, void *button, void *event)
{
    // The vanilla handler ignores every button but the left one. Right clicks are
    // picked up by poll_right_click, which also sees disabled (cooling down) buttons.
    if (field<int>(event, EVENT_MOUSE_BUTTON) != 0)
    {
        return;
    }
    s_orig_bottom_click(bottom, button, event);
}

static void *__thiscall cuimain_destructor_hook(void *cuimain, uint8_t flags)
{
    if (cuimain == s_cuimain)
    {
        s_cuimain = NULL;
    }
    return s_orig_cuimain_destructor(cuimain, flags);
}

/* ------------------------------------------------------------------------ */
/* Outline rendering                                                         */
/* ------------------------------------------------------------------------ */

struct OutlineVertex
{
    float x, y, z, rhw;
    D3DCOLOR color;
};

static int add_quad(OutlineVertex *v, float x0, float y0, float x1, float y1, D3DCOLOR c)
{
    const OutlineVertex quad[6] = {
        {x0, y0, 0.0f, 1.0f, c}, {x1, y0, 0.0f, 1.0f, c}, {x0, y1, 0.0f, 1.0f, c},
        {x1, y0, 0.0f, 1.0f, c}, {x1, y1, 0.0f, 1.0f, c}, {x0, y1, 0.0f, 1.0f, c},
    };
    memcpy(v, quad, sizeof(quad));
    return 6;
}

static int add_outline(OutlineVertex *v, float x, float y, float w, float h)
{
    // Pixel centres sit at .5 for pre-transformed vertices
    x -= 0.5f;
    y -= 0.5f;
    const float t = OUTLINE_THICKNESS;
    int n = 0;
    const D3DCOLOR c = s_outline_color;
    n += add_quad(v + n, x, y, x + w, y + t, c);
    n += add_quad(v + n, x, y + h - t, x + w, y + h, c);
    n += add_quad(v + n, x, y + t, x + t, y + h - t, c);
    n += add_quad(v + n, x + w - t, y + t, x + w, y + h - t, c);
    return n;
}

// Digits 0 - 9 of 3 x 5 pixels, a row of 3 bits each (top row first)
static const uint8_t DIGITS[10][5] = {
    {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7}, {5, 5, 7, 1, 1},
    {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 1, 1, 1}, {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
};
#define COUNT_VERTICES_MAX  (6 * (1 + 2 * 15))

// A number (0 - 99) in the top right corner of a button: light digits on a dark box
static int add_count(OutlineVertex *v, float x, float y, float w, float h, int number)
{
    char text[4];
    int length = snprintf(text, sizeof(text), "%d", number < 0 ? 0 : number > 99 ? 99 : number);
    float p = floorf(h / 18.0f);
    if (p < 2.0f)
    {
        p = 2.0f;
    }
    float right = floorf(x + w - OUTLINE_THICKNESS - 1.0f);
    float top = floorf(y + OUTLINE_THICKNESS + 1.0f);
    float width = length * 4.0f * p + p;
    int n = add_quad(v, right - width - 0.5f, top - 0.5f, right - 0.5f, top + 7.0f * p - 0.5f,
                     D3DCOLOR_ARGB(0xff, 0x10, 0x10, 0x10));
    for (int c = 0; c < length; c++)
    {
        const uint8_t *rows = DIGITS[text[c] - '0'];
        float left = right - width + p + c * 4.0f * p;
        for (int row = 0; row < 5; row++)
        {
            for (int bit = 0; bit < 3; bit++)
            {
                if (rows[row] & (4 >> bit))
                {
                    float px = left + bit * p - 0.5f, py = top + p + row * p - 0.5f;
                    n += add_quad(v + n, px, py, px + p, py + p, D3DCOLOR_ARGB(0xff, 0xff, 0xff, 0xff));
                }
            }
        }
    }
    return n;
}

static IDirect3DDevice9 *get_device()
{
    uint8_t *display = *(uint8_t **)addr_of(RVA_DSP_DISPLAY_PTR);
    return display ? field<IDirect3DDevice9 *>(display, DSP_DISPLAY_DEVICE) : NULL;
}

// Render and texture stage states the outline pass sets
static const D3DRENDERSTATETYPE OUTLINE_RENDER_STATES[] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_STENCILENABLE,
    D3DRS_SCISSORTESTENABLE, D3DRS_CULLMODE, D3DRS_LIGHTING, D3DRS_FOGENABLE, D3DRS_FILLMODE,
    D3DRS_COLORWRITEENABLE, D3DRS_SRGBWRITEENABLE,
};
static const DWORD OUTLINE_RENDER_VALUES[] = {
    D3DZB_FALSE, FALSE, FALSE, FALSE, FALSE, FALSE, D3DCULL_NONE, FALSE, FALSE, D3DFILL_SOLID, 0xf, FALSE,
};
static const struct
{
    DWORD stage;
    D3DTEXTURESTAGESTATETYPE type;
    DWORD value;
} OUTLINE_STAGE_STATES[] = {
    {0, D3DTSS_COLOROP, D3DTOP_SELECTARG1}, {0, D3DTSS_COLORARG1, D3DTA_DIFFUSE},
    {0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1}, {0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE},
    {1, D3DTSS_COLOROP, D3DTOP_DISABLE},    {1, D3DTSS_ALPHAOP, D3DTOP_DISABLE},
};
#define OUTLINE_RENDER_STATE_COUNT (sizeof(OUTLINE_RENDER_STATES) / sizeof(OUTLINE_RENDER_STATES[0]))
#define OUTLINE_STAGE_STATE_COUNT  (sizeof(OUTLINE_STAGE_STATES) / sizeof(OUTLINE_STAGE_STATES[0]))

static void set_outline_states(IDirect3DDevice9 *device)
{
    device->SetVertexShader(NULL);
    device->SetPixelShader(NULL);
    device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    device->SetTexture(0, NULL);
    device->SetStreamSource(0, NULL, 0, 0);     // DrawPrimitiveUP unbinds stream 0
    for (size_t i = 0; i < OUTLINE_RENDER_STATE_COUNT; i++)
    {
        device->SetRenderState(OUTLINE_RENDER_STATES[i], OUTLINE_RENDER_VALUES[i]);
    }
    for (size_t i = 0; i < OUTLINE_STAGE_STATE_COUNT; i++)
    {
        device->SetTextureStageState(OUTLINE_STAGE_STATES[i].stage, OUTLINE_STAGE_STATES[i].type,
                                     OUTLINE_STAGE_STATES[i].value);
    }
}

// The game's device is a pure device, which cannot report its state, so the
// state the outline pass changes is saved in a state block. Recording the
// changes makes a block of just those states: much cheaper to capture and
// restore than a block of the whole device state. It only lives for the draw,
// so nothing is held across a device reset.
static void draw_outlines(OutlineVertex *vertices, int count)
{
    IDirect3DDevice9 *device = get_device();
    IDirect3DStateBlock9 *saved = NULL;
    if (!device || count == 0 || FAILED(device->BeginStateBlock()))
    {
        return;
    }
    set_outline_states(device);     // recorded, not applied
    if (FAILED(device->EndStateBlock(&saved)) || !saved)
    {
        return;
    }
    saved->Capture();
    set_outline_states(device);
    device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, count / 3, vertices, sizeof(OutlineVertex));
    saved->Apply();
    saved->Release();
}

static void __thiscall bottom_render_hook(void *bottom, float param_1, float param_2)
{
    s_orig_bottom_render(bottom, param_1, param_2);

    void *cuimain = get_cuimain(bottom);
    if (cuimain)
    {
        s_cuimain = cuimain;
    }
    mnu_is_visible_ptr bottom_visible = (mnu_is_visible_ptr)(field<void **>(bottom, 0)[MNU_VF_IS_VISIBLE]);
    if (!cuimain || !bottom_visible(bottom))
    {
        return;
    }
    poll_right_click(bottom);
    if (s_autocast.empty())
    {
        return;
    }

    static OutlineVertex vertices[CUIBOTTOM_ACTION_COUNT * (24 + COUNT_VERTICES_MAX)];
    int count = 0;
    for (int i = 0; i < CUIBOTTOM_ACTION_COUNT; i++)
    {
        void *button = field<void *>(bottom, CUIBOTTOM_ACTION_BUTTONS + i * 4);
        ActionSlot slot;
        if (!button || !get_action_slot(cuimain, i, &slot))
        {
            continue;
        }
        mnu_is_visible_ptr visible = (mnu_is_visible_ptr)(field<void **>(button, 0)[MNU_VF_IS_VISIBLE]);
        if (!visible(button))
        {
            continue;
        }
        int heroes, enabled;
        count_autocast(cuimain, slot, &heroes, &enabled);
        if (heroes == 0 || enabled < heroes)
        {
            continue;
        }
        // Screen-space rect of the control (the one at +0x134 is relative to the bar)
        float *rect = (float *)((uint8_t *)button + MNU_SCREEN_RECT);
        count += add_outline(vertices + count, rect[0], rect[1], rect[2], rect[3]);
        if (is_summon(slot.spell_line))
        {
            // the number to keep of the first selected hero with it toggled
            for (uint16_t *it = slot.figures_begin; it < slot.figures_end; ++it)
            {
                HeroSlots hero;
                if (get_hero_slots(cuimain, *it, &hero) && autocast_enabled(hero.owner, slot.spell_line))
                {
                    count += add_count(vertices + count, rect[0], rect[1], rect[2], rect[3],
                                       summons_wanted(hero.owner, slot.spell_line));
                    break;
                }
            }
        }
    }
    draw_outlines(vertices, count);
}

/* ------------------------------------------------------------------------ */
/* Patching                                                                  */
/* ------------------------------------------------------------------------ */

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

static bool hook_vtable_slot(uintptr_t slot, void *replacement, void **original, uintptr_t expected_rva)
{
    void *current = *(void **)slot;
    if ((uintptr_t)current != addr_of(expected_rva))
    {
        logAPI->logError("[autocast] vtable slot %08x holds %08x, expected %08x; hook skipped",
                         (unsigned)slot, (unsigned)(uintptr_t)current, (unsigned)addr_of(expected_rva));
        return false;
    }
    *original = current;
    return write_code(slot, &replacement, sizeof(replacement));
}

// Redirects "call rel32" at rva and returns the previous target through original.
// Another mod may already have redirected the call; its target is chained then.
static bool hook_call(uintptr_t rva, void *replacement, void **original, uintptr_t vanilla_rva)
{
    uintptr_t at = addr_of(rva);
    if (*(uint8_t *)at != 0xe8)
    {
        logAPI->logError("[autocast] no call at %08x; hook skipped", (unsigned)at);
        return false;
    }
    uintptr_t target = at + 5 + *(int32_t *)(at + 1);
    if (target != addr_of(vanilla_rva))
    {
        logAPI->logInfo("[autocast] call at %08x already redirected by another mod; chaining it", (unsigned)at);
    }
    *original = (void *)target;
    int32_t rel = (int32_t)((uintptr_t)replacement - (at + 5));
    return write_code(at + 1, &rel, sizeof(rel));
}

// Entry hook: the first `length` bytes (whole, position independent instructions)
// move to a trampoline that continues in the original function. When the entry
// already jumps to another replacement (SFSF replaces some vanilla functions),
// that replacement is chained instead.
static bool hook_entry(uintptr_t rva, const uint8_t *expected, size_t length, void *replacement, void **trampoline)
{
    uintptr_t at = addr_of(rva);
    if (*(uint8_t *)at == 0xe9)
    {
        *trampoline = (void *)(at + 5 + *(int32_t *)(at + 1));
        int32_t rel = (int32_t)((uintptr_t)replacement - (at + 5));
        return write_code(at + 1, &rel, sizeof(rel));
    }
    if (length < 5 || length > 16 || memcmp((void *)at, expected, length) != 0)
    {
        const uint8_t *b = (const uint8_t *)at;
        logAPI->logError("[autocast] unexpected code at %08x (%02x %02x %02x %02x %02x %02x); hook skipped",
                         (unsigned)at, b[0], b[1], b[2], b[3], b[4], b[5]);
        return false;
    }
    uint8_t *code = (uint8_t *)VirtualAlloc(NULL, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!code)
    {
        return false;
    }
    memcpy(code, (void *)at, length);
    code[length] = 0xe9;
    *(int32_t *)(code + length + 1) = (int32_t)((at + length) - ((uintptr_t)code + length + 5));
    *trampoline = code;

    uint8_t patch[16];
    memset(patch, 0x90, length);
    patch[0] = 0xe9;
    *(int32_t *)(patch + 1) = (int32_t)((uintptr_t)replacement - (at + 5));
    return write_code(at, patch, length);
}

static bool install_ai_check_hook()
{
    static const uint8_t expected[] = {
        0x8b, 0x45, 0xfc, 0x83, 0xb8, 0x2c, 0x3f, 0x00, 0x00, 0x00, 0x75, 0x0c,
        0x8b, 0x45, 0xfc, 0x83, 0xb8, 0x30, 0x3f, 0x00, 0x00, 0x00, 0x74, 0x07,
    };
    uintptr_t at = addr_of(RVA_AI_HERO_CHECK);
    if (memcmp((void *)at, expected, sizeof(expected)) != 0)
    {
        logAPI->logError("[autocast] unexpected code at %08x; AI hook skipped", (unsigned)at);
        return false;
    }
    uint8_t patch[sizeof(expected)];
    memset(patch, 0x90, sizeof(patch));
    int32_t rel = (int32_t)((uintptr_t)&ai_hero_check_stub - (at + 5));
    patch[0] = 0xe8;                                                            // call ai_hero_check_stub
    memcpy(patch + 1, &rel, 4);
    patch[5] = 0x85;                                                            // test eax, eax
    patch[6] = 0xc0;
    patch[7] = 0x74;                                                            // jz deny
    patch[8] = (uint8_t)(addr_of(RVA_AI_HERO_CHECK_DENY) - (at + 9));
    patch[9] = 0xeb;                                                            // jmp allow
    patch[10] = (uint8_t)(addr_of(RVA_AI_HERO_CHECK_ALLOW) - (at + 11));
    return write_code(at, patch, sizeof(patch));
}

extern "C" __declspec(dllexport) void InitModule(SpellforceSpellFramework *framework)
{
    sfsf = framework;
    logAPI = sfsf->logAPI;
    s_base = (uintptr_t)GetModuleHandleA(NULL);

    s_spell_of_item = (spell_of_item_ptr)addr_of(RVA_UIDB_SPELL_OF_ITEM);
    s_spell_line = (spell_line_ptr)addr_of(RVA_RESOURCE_SPELL_LINE);
    s_spell_line_is_target_self = (spell_line_flag_ptr)addr_of(RVA_RESOURCE_TARGET_SELF);
    s_figure_can_cast = (can_cast_ptr)addr_of(RVA_FIGURE_CAN_CAST_SPELL);
    s_do_cast = (do_cast_ptr)addr_of(RVA_DO_CAST_SPELL);
    s_current_tick = (tick_ptr)addr_of(RVA_GET_CURRENT_TICK);

    load_autocast();

    s_vanilla_execute_action = (battle_step_ptr)addr_of(RVA_AI_EXECUTE);
    s_end_spell = (end_spell_ptr)addr_of(RVA_CGDSPELL_END_SPELL);

    // push ebp; mov ebp, esp; push -1; push <SEH handler, relocated>
    uint8_t leave_battle_prologue[AI_LEAVE_BATTLE_PROLOGUE] = {0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68};
    memcpy(leave_battle_prologue + 6, (void *)(addr_of(RVA_AI_LEAVE_BATTLE) + 6), 4);

    // Install the AI hooks together: the check alone would never see a hero spell
    bool ai = install_ai_check_hook() &&
              hook_call(RVA_AI_RANK_ACTIONS_CALL, (void *)&ai_rank_actions_hook, (void **)&s_rank_actions,
                        RVA_AI_RANK_ACTIONS) &&
              hook_call(RVA_AI_EXECUTE_CALL, (void *)&ai_execute_action_hook, (void **)&s_execute_action,
                        RVA_AI_EXECUTE);
    bool auras = hook_entry(RVA_AI_LEAVE_BATTLE, leave_battle_prologue, AI_LEAVE_BATTLE_PROLOGUE,
                            (void *)&ai_leave_battle_hook, (void **)&s_leave_battle_trampoline);
    bool ui = hook_vtable_slot(addr_of(RVA_CUIMAIN_VTABLE), (void *)&cuimain_destructor_hook,
                               (void **)&s_orig_cuimain_destructor, 0x5de3c0) &&
              hook_vtable_slot(addr_of(RVA_CUIBOTTOM_VT_CLICK), (void *)&bottom_click_hook,
                               (void **)&s_orig_bottom_click, 0x5ccff0);
    bool render = hook_vtable_slot(addr_of(RVA_CUIBOTTOM_VT_RENDER), (void *)&bottom_render_hook,
                                   (void **)&s_orig_bottom_render, 0x5096d0);
    bool idle = hook_call(RVA_JOB_NOTHING_CALL, (void *)&job_nothing_hook, (void **)&s_job_nothing,
                          RVA_JOB_NOTHING) &&
                hook_call(RVA_JOB_WARRIOR_NOTHING_CALL, (void *)&job_warrior_nothing_hook,
                          (void **)&s_job_warrior_nothing, RVA_JOB_WARRIOR_NOTHING);
    logAPI->logInfo("[autocast] hooks: battle AI %s, aura shutdown %s, right click %s, outline %s, outside fights %s",
                    ai ? "ok" : "FAILED", auras ? "ok" : "FAILED", ui ? "ok" : "FAILED", render ? "ok" : "FAILED",
                    idle ? "ok" : "FAILED");
}

extern "C" __declspec(dllexport) SFMod *RegisterMod(SpellforceSpellFramework *framework)
{
    return framework->createModInfo("Hero Autocast", "1.4.0", "Angry Catster",
                                    "Right-click a spell on the action bar to let the avatar or a hero cast it automatically.");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    return TRUE;
}
