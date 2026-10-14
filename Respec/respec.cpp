/*
 * Respec
 *
 * Adds a RESPEC button to the character info screen, below the level bar. A
 * click turns it into CONFIRM?, greyed out for two seconds and then clickable for
 * two more (it turns back into RESPEC after that). Confirming resets the avatar
 * to its level 1 build: everything it wears goes to the backpack, every stat
 * goes back to 25,
 * every ability is removed, and all the points spent on them are handed back as
 * unspent stat and ability points (a level 1 avatar has 25 in every stat plus
 * 30 points, and 2 ability points; each level adds 5 and 2). The avatar's
 * spell bar is emptied (its auras end with it), the spells the combat abilities
 * gave (Trueshot, Salvo, ...) leave the spellbook, and the button is hidden while
 * the screen shows a rune hero.
 *
 * The screen keeps the unspent points in bytes, which a respec past level 46
 * overflows (275 stat points at level 50): it gets them capped at 255, which keeps
 * its buttons working, and its label shows the real count.
 *
 * The avatar lives in two places: the character database of the session
 * (CDbLocal, which is saved and carried from map to map) and its figure on the
 * map. Raising a stat goes from the screen to the database as command 0x1f87,
 * whose handler updates the database and sends the figure the new value (world
 * message 0xfd3). The button sends its own variant of that command; the hook of
 * the handler first takes off the avatar's items through the handler of the
 * equipment screen's clicks (0x1f4c), then resets the database record and sends
 * the figure one 0xfd3 per stat plus one of its own that clears the figure's
 * abilities, and tells the screen the new point counts the way the game does
 * after a raise.
 */

#include "../../src/api/sfsf.h"

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>

SpellforceSpellFramework *sfsf;
SFLog *logAPI;

/* ------------------------------------------------------------------------ */
/* Game addresses (offsets relative to the image base, like ASI::AddrOf)     */
/* ------------------------------------------------------------------------ */

// CUiCCharInfo: vtable slot 196 builds the controls of the screen
#define RVA_CHARINFO_VT_BUILD      0x800a0c
#define RVA_CHARINFO_BUILD         0x5a4610
#define CHARINFO_UIDBPROXY         0x35c
#define CHARINFO_SHOWN             0xe368      // 0: the avatar, 1 - 5: a rune hero
// CUiCCharInfo button refresh (fastcall): shows and enables the controls for
// the character shown; entry: push ebx; push esi; mov esi, ecx; push edi
#define RVA_CHARINFO_REFRESH       0x5a8960
#define CHARINFO_REFRESH_PROLOGUE  5
// CUiCCharInfo keeps the unspent points in bytes; its setter is called by the
// listener of the player's info with both counts cut to a byte
#define RVA_CHARINFO_SET_POINTS    0x5ac250    // (charinfo, ability points, stat points)
#define RVA_POINTS_LISTENER_CALL   0x5ecdc2
#define CHARINFO_STAT_POINTS       0xe411
// The label "STAT POINTS: %u": movzx ecx, byte [ebx + 0xe411]
#define RVA_STAT_POINTS_LABEL      0x5a7b5c
#define PROXY_ABILITY_POINTS       0x247       // u16, the player's info from the server
#define PROXY_STAT_POINTS          0x249
#define MNU_VF_SET_VISIBLE         (0x3c / 4)
#define MNU_VF_SET_ENABLED         (0x44 / 4)
// CUiCCharInfo vtable: render (inherited from CMnuContainer), every frame it is shown
#define RVA_CHARINFO_VT_RENDER     0x800938
#define RVA_CONTAINER_RENDER       0x5096d0

// Client: UiDbProxy sends a command to the session's database server
#define RVA_UIDB_SEND              0x579510    // (proxy, id, data, size)
#define RVA_WRITE_ABILITY_COMMAND  0x218080    // 4 bytes: type, index, spec, points


// Server (AutoClass6): command dispatcher, call to the handler of 0x1f87
#define RVA_SERVER_RAISE_CALL      0x230ffd
#define RVA_SERVER_RAISE           0x226770    // (server, player, data, size)
#define RVA_SERVER_MOVE_ITEM       0x22ab90    // handler of 0x1f4c, same arguments
#define RVA_SERVER_DESTROY_ITEM    0x226520    // handler of 0x1f4e: the item on the cursor
#define RVA_SERVER_SEND_SLOTS      0x227040    // handler of 0x1f48: a range of a container
#define RVA_WRITE_PLAYER_ID        0x218b10    // client message 0x1fa6
#define SERVER_DB                  0x0         // CDbLocal *
#define SERVER_NET                 0x4
#define SERVER_BUFFER              0x8
#define SERVER_BUFFER_SIZE         0x7008
#define RVA_WRITE_FIGURE_STAT      0x2a7110    // world message 0xfd3
#define RVA_WRITE_PLAYER_SUMMARY   0x219c30    // client message 0x1f45
#define RVA_WRITE_RESULT           0x219550    // client message 0x1f88
#define RVA_SEND_WORLD             0x261440    // (net, id, data, size)
#define RVA_SEND_CLIENT            0x2613d0    // (net, player, id, data, size)
#define DB_VF_PLAYER_SUMMARY       (0x178 / 4) // (db, player, summary) -> found
#define DB_VF_ITEM                 (0x70 / 4)  // (db, player, container, slot, item) -> found
#define DB_VF_ADD_COUNT            (0x7c / 4)  // (db, player, container, slot, delta, item); 0 erases
#define CONTAINER_BACKPACK         1
#define CONTAINER_AVATAR_EQUIPMENT 2
#define CONTAINER_SPELLBOOK        9
#define CONTAINER_AVATAR_SPELLBAR  10
#define CONTAINER_CURSOR           0x18
#define BACKPACK_SLOTS             2000
#define EQUIPMENT_SLOTS            16
#define SPELLBAR_SLOTS             10
#define SPELLBOOK_SLOTS            1000
#define CLIENT_SPELLBOOK_CHANGED   0x1fa6

// CDbLocal: map of player id -> character record (std::map node)
#define DB_PLAYERS                 0x4
#define NODE_LEFT                  0x0
#define NODE_PARENT                0x4
#define NODE_RIGHT                 0x8
#define NODE_IS_NIL                0xd
#define NODE_KEY                   0x10
#define NODE_LEVEL                 0x18
#define NODE_STATS                 0x79        // 7 x u16, stat index 1 - 7
#define NODE_ABILITIES             0x95        // 10 x {ability, spec, level}
#define NODE_ABILITY_POINTS        0xd1
#define NODE_STAT_POINTS           0xd3

// World: message 0xfd3 handler, call to CGdFigureToolBox's update of the avatar
#define RVA_WORLD_FIGURE_STAT_CALL 0x3566a2
#define RVA_WORLD_FIGURE_STAT      0x2fb865    // (toolbox, player, type, index, value, ability)
#define TOOLBOX_LINKED_LIST        0x18
#define TOOLBOX_FIGURES            0x24
#define TOOLBOX_SPELLS             0x54
#define RVA_END_SPELL              0x34b0a0    // CGdSpell, (spells, spell index)
#define TOOLBOX_PLAYERS            0x48
#define RVA_PLAYER_FROM_GLOBAL     0x2a2640    // CGdPlayer::GetGdPlayerFromGlobalPlayer
#define RVA_PLAYER_MAINCHAR        0x2793d0    // CGdPlayer::GetMainCharFigure
#define FIGURE_SIZE                0x2b3
#define FIGURE_ABILITIES           0x16c       // from the CGdFigure base

#define RVA_SF_STRING_CTOR         0x3837e0
#define RVA_SF_STRING_DTOR         0x3839c0

#define COMMAND_MOVE_ITEM          0x1f4c
#define COMMAND_RAISE              0x1f87
#define COMMAND_RESULT             0x1f88
#define CLIENT_PLAYER_SUMMARY      0x1f45
#define WORLD_FIGURE_STAT          0xfd3
#define STAT_TYPE                  2           // 0xfd3 types: 1 ability, 2 stat
#define RESPEC_TYPE                0x52        // ours: clear the abilities
#define STAT_COUNT                 7
#define ABILITY_COUNT              10
#define BASE_STAT                  25

// Our variant of command 0x1f87: its type byte is RESPEC_TYPE (the game uses 1 and 2)
static const uint8_t RESPEC_COMMAND[4] = {RESPEC_TYPE, 'E', 'S', 'P'};

// Button below the level bar, in screen coordinates (NEW ABILITY is 144, 335)
#define BUTTON_X                   678
#define BUTTON_Y                   380
#define BUTTON_WIDTH               154
#define BUTTON_HEIGHT              32
#define BUTTON_FONT                9
#define CONFIRM_LOCK_MS            2000        // CONFIRM? is greyed out this long,
#define CONFIRM_WINDOW_MS          2000        // then can be clicked this long
#define BUTTON_CALLBACK            0x8         // object, method, invoker

static_assert(offsetof(SF_CGdFigure, figures) + offsetof(GdFigure, abilties) == FIGURE_ABILITIES, "abilities");
static_assert(sizeof(GdFigure) == FIGURE_SIZE, "figure size");

typedef void (__thiscall *build_ptr)(void *charinfo);
typedef void (__thiscall *send_ptr)(void *sender, uint16_t id, const void *data, int size);
typedef int (__thiscall *write_ptr)(const void *message, void *buffer, int size);
typedef void (__thiscall *server_raise_ptr)(void *server, uint32_t player, const uint8_t *data, int size);
typedef void (__thiscall *send_world_ptr)(void *net, uint16_t id, void *data, int size);
typedef void (__thiscall *send_client_ptr)(void *net, uint32_t player, uint16_t id, void *data, int size);
typedef int (__thiscall *player_summary_ptr)(void *db, uint32_t player, void *summary);
typedef int (__thiscall *db_item_ptr)(void *db, uint32_t player, uint32_t container, uint32_t slot, void *item);
typedef int (__thiscall *db_add_count_ptr)(void *db, uint32_t player, uint32_t container, uint32_t slot, int delta,
                                           void *item);
typedef uint32_t (__thiscall *world_figure_stat_ptr)(void *toolbox, uint32_t player, char type, char index,
                                                     uint16_t value, uint8_t *ability);
typedef uint16_t (__thiscall *player_lookup_ptr)(void *players, uint32_t player);
typedef SF_String *(__thiscall *string_ctor_ptr)(SF_String *string, const char *text);
typedef void (__thiscall *string_dtor_ptr)(SF_String *string);
typedef void (__fastcall *refresh_ptr)(void *charinfo);
typedef void (__thiscall *render_ptr)(void *container, float param_1, float param_2);
typedef void (__thiscall *set_points_ptr)(void *charinfo, uint32_t ability_points, uint32_t stat_points);
typedef void (__thiscall *end_spell_ptr)(SF_CGdSpell *spells, uint16_t spell_index);

static uintptr_t s_base;
static build_ptr s_build;
static server_raise_ptr s_server_raise;
static world_figure_stat_ptr s_world_figure_stat;
static refresh_ptr s_refresh;
static set_points_ptr s_set_points;
// The stat points shown by the label: the real count, which may exceed a byte
static uint16_t s_stat_points;

static void *s_button;
static void *s_charinfo;
static render_ptr s_render;
// RESPEC -> CONFIRM? (greyed out) -> CONFIRM? (clickable) -> back to RESPEC
enum ConfirmState
{
    CONFIRM_NONE,
    CONFIRM_LOCKED,
    CONFIRM_OPEN
};
static ConfirmState s_confirm;
static DWORD s_confirm_at;

static inline uintptr_t addr_of(uintptr_t rva)
{
    return s_base + rva;
}

template <typename T>
static inline T &at(void *object, uint32_t offset)
{
    return *(T *)((uint8_t *)object + offset);
}

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

static bool hook_vtable_slot(uintptr_t rva_slot, void *replacement, void **original, uintptr_t expected_rva)
{
    uintptr_t slot = addr_of(rva_slot);
    void *current = *(void **)slot;
    if ((uintptr_t)current != addr_of(expected_rva))
    {
        logAPI->logError("[respec] vtable slot %08x holds %08x, expected %08x; hook skipped", (unsigned)slot,
                         (unsigned)(uintptr_t)current, (unsigned)addr_of(expected_rva));
        return false;
    }
    *original = current;
    return write_code(slot, &replacement, sizeof(replacement));
}

// Entry hook: the first `length` bytes (whole, position independent instructions)
// move to a trampoline that continues in the original function
static bool hook_entry(uintptr_t rva, const uint8_t *expected, size_t length, void *replacement, void **trampoline)
{
    uintptr_t at_ = addr_of(rva);
    if (memcmp((void *)at_, expected, length) != 0)
    {
        logAPI->logError("[respec] unexpected code at %08x; hook skipped", (unsigned)at_);
        return false;
    }
    uint8_t *code = (uint8_t *)VirtualAlloc(NULL, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!code)
    {
        return false;
    }
    memcpy(code, (void *)at_, length);
    code[length] = 0xe9;
    *(int32_t *)(code + length + 1) = (int32_t)((at_ + length) - ((uintptr_t)code + length + 5));
    *trampoline = code;
    uint8_t jump[5] = {0xe9};
    *(int32_t *)(jump + 1) = (int32_t)((uintptr_t)replacement - (at_ + 5));
    return write_code(at_, jump, sizeof(jump));
}

// Redirects "call rel32" at rva; another mod's redirection is chained
static bool hook_call(uintptr_t rva, void *replacement, void **original, uintptr_t vanilla_rva)
{
    uintptr_t at = addr_of(rva);
    if (*(uint8_t *)at != 0xe8)
    {
        logAPI->logError("[respec] no call at %08x; hook skipped", (unsigned)at);
        return false;
    }
    uintptr_t target = at + 5 + *(int32_t *)(at + 1);
    if (target != addr_of(vanilla_rva))
    {
        logAPI->logInfo("[respec] call at %08x already redirected by another mod; chaining it", (unsigned)at);
    }
    *original = (void *)target;
    int32_t rel = (int32_t)((uintptr_t)replacement - (at + 5));
    return write_code(at + 1, &rel, sizeof(rel));
}

#ifdef RESPEC_DIAG
// Logs the commands the client sends
static void __thiscall uidb_send_hook(void *proxy, uint16_t id, const uint8_t *data, int size)
{
    char hex[3 * 40 + 1] = "";
    for (int i = 0; i < size && i < 40; i++)
    {
        sprintf(hex + 3 * i, "%02x ", data[i]);
    }
    logAPI->logInfo("[respec] diag command %04x size %d: %s", id, size, hex);
    ((send_ptr)addr_of(0x1fa310))(at<void *>(proxy, 0x1c0), id, data, size);
}

static void hook_uidb_send()
{
    uintptr_t at_ = addr_of(RVA_UIDB_SEND);
    uint8_t jump[5] = {0xe9};
    *(int32_t *)(jump + 1) = (int32_t)((uintptr_t)&uidb_send_hook - (at_ + 5));
    write_code(at_, jump, sizeof(jump));
}
#endif

/* ------------------------------------------------------------------------ */
/* Server side: the respec itself                                            */
/* ------------------------------------------------------------------------ */

// Character record node of a player in the database, or NULL
static uint8_t *find_player_node(void *db, uint32_t player)
{
    uint8_t *head = at<uint8_t *>(db, DB_PLAYERS);
    uint8_t *found = head;
    uint8_t *node = at<uint8_t *>(head, NODE_PARENT);
    while (!at<uint8_t>(node, NODE_IS_NIL))
    {
        if (at<uint32_t>(node, NODE_KEY) < player)
        {
            node = at<uint8_t *>(node, NODE_RIGHT);
        }
        else
        {
            found = node;
            node = at<uint8_t *>(node, NODE_LEFT);
        }
    }
    if (found == head || player < at<uint32_t>(found, NODE_KEY))
    {
        return NULL;
    }
    return found;
}

#pragma pack(push, 1)
struct FigureStatMessage
{
    uint32_t player;
    uint8_t type;
    uint8_t index;
    uint16_t value;
    uint8_t ability[3];
};
#pragma pack(pop)

static void send_world(void *server, uint16_t id, const void *message, uintptr_t writer_rva)
{
    void *buffer = (uint8_t *)server + SERVER_BUFFER;
    int size = ((write_ptr)addr_of(writer_rva))(message, buffer, at<int>(server, SERVER_BUFFER_SIZE));
    ((send_world_ptr)addr_of(RVA_SEND_WORLD))(at<void *>(server, SERVER_NET), id, buffer, size);
}

static void send_client(void *server, uint32_t player, uint16_t id, const void *message, uintptr_t writer_rva)
{
    void *buffer = (uint8_t *)server + SERVER_BUFFER;
    int size = ((write_ptr)addr_of(writer_rva))(message, buffer, at<int>(server, SERVER_BUFFER_SIZE));
    ((send_client_ptr)addr_of(RVA_SEND_CLIENT))(at<void *>(server, SERVER_NET), player, id, buffer, size);
}

#pragma pack(push, 1)
struct ItemStack
{
    uint16_t item;
    uint16_t count;
    uint8_t flag;
};

// Command 0x1f4c as the equipment screen sends it: a click on a slot, with the
// stack in that slot and the stack on the cursor
struct ItemClick
{
    uint8_t container;
    uint16_t slot;
    ItemStack slot_stack;
    ItemStack cursor_stack;
};
#pragma pack(pop)

static void move_item_click(void *server, uint32_t player, uint8_t container, uint16_t slot, const ItemStack &in_slot,
                            const ItemStack &on_cursor)
{
    ItemClick click = {container, slot, in_slot, on_cursor};
    ((server_raise_ptr)addr_of(RVA_SERVER_MOVE_ITEM))(server, player, (const uint8_t *)&click, sizeof(click));
}

// Every item the avatar wears goes to an empty backpack slot: picked up from its
// equipment slot and put down, as two clicks on the equipment screen would
static void unequip_all(void *server, uint32_t player)
{
    void *db = at<void *>(server, SERVER_DB);
    db_item_ptr get_item = (db_item_ptr)(*(void ***)db)[DB_VF_ITEM];
    int backpack_slot = 0;
    int moved = 0, kept = 0;
    for (int slot = 0; slot < EQUIPMENT_SLOTS; slot++)
    {
        ItemStack worn = {}, there = {};
        if (!get_item(db, player, CONTAINER_AVATAR_EQUIPMENT, slot, &worn) || worn.item == 0)
        {
            continue;
        }
        while (backpack_slot < BACKPACK_SLOTS && get_item(db, player, CONTAINER_BACKPACK, backpack_slot, &there))
        {
            backpack_slot++;
        }
        if (backpack_slot == BACKPACK_SLOTS)
        {
            kept++;
            continue;
        }
        ItemStack none = {};
        move_item_click(server, player, CONTAINER_AVATAR_EQUIPMENT, slot, worn, none);
        move_item_click(server, player, CONTAINER_BACKPACK, backpack_slot, none, worn);
        if (get_item(db, player, CONTAINER_AVATAR_EQUIPMENT, slot, &there) && there.item != 0)
        {
            logAPI->logWarning("[respec] item %u in equipment slot %d could not be taken off", worn.item, slot);
            kept++;
            continue;
        }
        logAPI->logInfo("[respec] item %u from equipment slot %d to backpack slot %d", worn.item, slot,
                        backpack_slot);
        moved++;
    }
    logAPI->logInfo("[respec] unequipped %d item(s), %d kept", moved, kept);
}

// The spells on the avatar's bar: each is taken off as a click on its slot would;
// what lands on the cursor (the bar's copy of the spell) is thrown away
static void clear_spellbar(void *server, uint32_t player)
{
    void *db = at<void *>(server, SERVER_DB);
    db_item_ptr get_item = (db_item_ptr)(*(void ***)db)[DB_VF_ITEM];
    int cleared = 0;
    for (int slot = 0; slot < SPELLBAR_SLOTS; slot++)
    {
        ItemStack spell = {}, held = {};
        if (!get_item(db, player, CONTAINER_AVATAR_SPELLBAR, slot, &spell) || spell.item == 0)
        {
            continue;
        }
        ItemStack none = {};
        move_item_click(server, player, CONTAINER_AVATAR_SPELLBAR, slot, spell, none);
        if (get_item(db, player, CONTAINER_CURSOR, 0, &held) && held.item == spell.item)
        {
            ((server_raise_ptr)addr_of(RVA_SERVER_DESTROY_ITEM))(server, player, (const uint8_t *)&held,
                                                                 sizeof(held));
        }
        if (get_item(db, player, CONTAINER_AVATAR_SPELLBAR, slot, &held) && held.item != 0)
        {
            logAPI->logWarning("[respec] spell %u in bar slot %d could not be taken off", spell.item, slot);
            continue;
        }
        cleared++;
    }
    logAPI->logInfo("[respec] took %d spell(s) off the bar", cleared);
}

// Spells the combat abilities hand out at ability levels 1, 2, 4, 6, 14 and 17
// (AutoClass6::learnSkillOnLvlUp): Trueshot, Salvo, Steelskin, ...
static bool is_ability_spell(uint16_t item)
{
    return (item >= 0xc87 && item <= 0xc91) || (item >= 0x1c92 && item <= 0x1c94);
}

// The spells the abilities gave are taken out of the spellbook; the client then
// gets the spellbook again, as it asks for it when a game is loaded
static void remove_ability_spells(void *server, uint32_t player)
{
    void *db = at<void *>(server, SERVER_DB);
    void **db_vtable = *(void ***)db;
    db_item_ptr get_item = (db_item_ptr)db_vtable[DB_VF_ITEM];
    db_add_count_ptr add_count = (db_add_count_ptr)db_vtable[DB_VF_ADD_COUNT];
    int removed = 0, last_slot = -1;
    for (int slot = 0; slot < SPELLBOOK_SLOTS; slot++)
    {
        ItemStack spell = {};
        if (!get_item(db, player, CONTAINER_SPELLBOOK, slot, &spell) || spell.item == 0)
        {
            continue;
        }
        last_slot = slot;
        if (!is_ability_spell(spell.item))
        {
            continue;
        }
        ItemStack left = {};
        add_count(db, player, CONTAINER_SPELLBOOK, slot, -(int)spell.count, &left);
        logAPI->logInfo("[respec] removed spell %u from spellbook slot %d", spell.item, slot);
        removed++;
    }
    if (removed == 0)
    {
        return;
    }
    // 0x1f48: container, first slot, slot count (up to 255 a time)
    for (int first = 0; first <= last_slot; first += 255)
    {
        uint8_t request[4] = {CONTAINER_SPELLBOOK, (uint8_t)first, (uint8_t)(first >> 8), 255};
        ((server_raise_ptr)addr_of(RVA_SERVER_SEND_SLOTS))(server, player, request, sizeof(request));
    }
    send_client(server, player, CLIENT_SPELLBOOK_CHANGED, &player, RVA_WRITE_PLAYER_ID);
}

static bool respec(void *server, uint32_t player)
{
    void *db = at<void *>(server, SERVER_DB);
    uint8_t *node = find_player_node(db, player);
    if (!node)
    {
        logAPI->logWarning("[respec] no character record for player %u", player);
        return false;
    }
    unequip_all(server, player);
    clear_spellbar(server, player);
    remove_ability_spells(server, player);
    uint16_t *stats = (uint16_t *)(node + NODE_STATS);
    uint8_t *abilities = node + NODE_ABILITIES;
    uint16_t &ability_points = at<uint16_t>(node, NODE_ABILITY_POINTS);
    uint16_t &stat_points = at<uint16_t>(node, NODE_STAT_POINTS);
    logAPI->logInfo("[respec] player %u level %u: stats %u %u %u %u %u %u %u, %u stat points, %u ability points",
                    player, at<uint8_t>(node, NODE_LEVEL), stats[0], stats[1], stats[2], stats[3], stats[4],
                    stats[5], stats[6], stat_points, ability_points);

    unsigned stat_refund = 0;
    for (int i = 0; i < STAT_COUNT; i++)
    {
        if (stats[i] > BASE_STAT)
        {
            stat_refund += stats[i] - BASE_STAT;
            stats[i] = BASE_STAT;
        }
    }
    unsigned ability_refund = 0;
    for (int i = 0; i < ABILITY_COUNT; i++)
    {
        uint8_t *ability = abilities + i * 3;
        if (ability[0] != 0xff)
        {
            logAPI->logInfo("[respec] ability %u spec %u level %u", ability[0], ability[1], ability[2]);
            ability_refund += ability[2];
        }
        memset(ability, 0xff, 3);
    }
    stat_points += stat_refund;
    ability_points += ability_refund;
    logAPI->logInfo("[respec] refunded %u stat points and %u ability points: now %u and %u", stat_refund,
                    ability_refund, stat_points, ability_points);

    // The avatar's figure on the map: every stat, then its abilities
    FigureStatMessage message;
    memset(&message, 0xff, sizeof(message));
    message.player = player;
    message.type = STAT_TYPE;
    message.value = BASE_STAT;
    for (int i = 0; i < STAT_COUNT; i++)
    {
        message.index = i + 1;
        send_world(server, WORLD_FIGURE_STAT, &message, RVA_WRITE_FIGURE_STAT);
    }
    message.type = RESPEC_TYPE;
    message.index = 0;
    send_world(server, WORLD_FIGURE_STAT, &message, RVA_WRITE_FIGURE_STAT);

    // The screen: point counts, as after a raise, and the result of the command
    uint8_t summary[0x100];
    memset(summary, 0, sizeof(summary));
    void **db_vtable = *(void ***)db;
    if (((player_summary_ptr)db_vtable[DB_VF_PLAYER_SUMMARY])(db, player, summary))
    {
        summary[0x77] = 1;
        send_client(server, player, CLIENT_PLAYER_SUMMARY, summary, RVA_WRITE_PLAYER_SUMMARY);
    }
    return true;
}

static void __thiscall server_raise_hook(void *server, uint32_t player, const uint8_t *data, int size)
{
    if (size >= (int)sizeof(RESPEC_COMMAND) && memcmp(data, RESPEC_COMMAND, sizeof(RESPEC_COMMAND)) == 0)
    {
        uint8_t result = respec(server, player) ? 1 : 0;
        send_client(server, player, COMMAND_RESULT, &result, RVA_WRITE_RESULT);
        return;
    }
    s_server_raise(server, player, data, size);
}

/* ------------------------------------------------------------------------ */
/* World side: the figure's abilities                                        */
/* ------------------------------------------------------------------------ */

static uint32_t __thiscall world_figure_stat_hook(void *toolbox, uint32_t player, char type, char index,
                                                  uint16_t value, uint8_t *ability)
{
    if ((uint8_t)type != RESPEC_TYPE)
    {
        return s_world_figure_stat(toolbox, player, type, index, value, ability);
    }
    void *players = at<void *>(toolbox, TOOLBOX_PLAYERS);
    uint16_t local_player = ((player_lookup_ptr)addr_of(RVA_PLAYER_FROM_GLOBAL))(players, player);
    uint16_t figure = local_player ? ((player_lookup_ptr)addr_of(RVA_PLAYER_MAINCHAR))(players, local_player) : 0;
    if (figure == 0)
    {
        logAPI->logWarning("[respec] no avatar figure for player %u", player);
        return 0;
    }
    SF_CGdFigure *figures = at<SF_CGdFigure *>(toolbox, TOOLBOX_FIGURES);
    memset(figures->figures[figure].abilties, 0xff, sizeof(figures->figures[figure].abilties));
    logAPI->logInfo("[respec] cleared the abilities of figure %u", figure);

    // Auras the avatar keeps up end, as when a unit leaves a fight
    SF_CGdSpell *spells = at<SF_CGdSpell *>(toolbox, TOOLBOX_SPELLS);
    uint32_t *links = at<uint32_t *>(toolbox, TOOLBOX_LINKED_LIST);
    uint16_t auras[32];
    int aura_count = 0;
    uint16_t node = sfsf->figureAPI->getSpellJobStartNode(figures, figure);
    for (int guard = 0; node != 0 && guard < 256 && aura_count < 32; guard++)
    {
        uint16_t spell_index = sfsf->toolboxAPI->getSpellIndexFromDLL(links, node);
        if (sfsf->spellAPI->hasSpellTag(spells->active_spell_list[spell_index].spell_line, SpellTag::AURA_SPELL))
        {
            auras[aura_count++] = spell_index;
        }
        node = sfsf->toolboxAPI->getNextNode(links, node);
    }
    for (int i = 0; i < aura_count; i++)
    {
        logAPI->logInfo("[respec] ending aura (spell line %u)", spells->active_spell_list[auras[i]].spell_line);
        ((end_spell_ptr)addr_of(RVA_END_SPELL))(spells, auras[i]);
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Client side: the button                                                   */
/* ------------------------------------------------------------------------ */

static void set_button_label(void *button, const char *text)
{
    SF_String label;
    ((string_ctor_ptr)addr_of(RVA_SF_STRING_CTOR))(&label, text);
    sfsf->uiAPI->setButtonName(button, &label);
    ((string_dtor_ptr)addr_of(RVA_SF_STRING_DTOR))(&label);
}

static void send_command(void *proxy, uint16_t id, const void *message, uintptr_t writer_rva)
{
    static uint8_t buffer[0xe000];
    int size = ((write_ptr)addr_of(writer_rva))(message, buffer, sizeof(buffer));
    ((send_ptr)addr_of(RVA_UIDB_SEND))(proxy, id, buffer, size);
}

static void set_enabled(void *control, bool enabled)
{
    ((void (__thiscall *)(void *, int))(*(void ***)control)[MNU_VF_SET_ENABLED])(control, enabled);
}

static void set_confirm(ConfirmState state)
{
    s_confirm = state;
    s_confirm_at = GetTickCount();
    if (s_button)
    {
        set_button_label(s_button, state == CONFIRM_NONE ? "RESPEC" : "CONFIRM?");
        set_enabled(s_button, state != CONFIRM_LOCKED);
    }
}

static void __thiscall respec_click(void *charinfo, void *button, void *event)
{
    if (s_confirm == CONFIRM_NONE)
    {
        set_confirm(CONFIRM_LOCKED);
        return;
    }
    if (s_confirm != CONFIRM_OPEN)
    {
        return;
    }
    set_confirm(CONFIRM_NONE);
    void *proxy = at<void *>(s_charinfo, CHARINFO_UIDBPROXY);
    logAPI->logInfo("[respec] sending respec");
    send_command(proxy, COMMAND_RAISE, RESPEC_COMMAND, RVA_WRITE_ABILITY_COMMAND);
}

// Every frame the screen is shown: the confirmation steps along in time. A
// confirmation left when the screen was closed has run out when it is opened again
static void __thiscall charinfo_render_hook(void *charinfo, float param_1, float param_2)
{
    if (charinfo == s_charinfo && s_confirm != CONFIRM_NONE)
    {
        DWORD elapsed = GetTickCount() - s_confirm_at;
        if (s_confirm == CONFIRM_LOCKED && elapsed >= CONFIRM_LOCK_MS)
        {
            set_confirm(elapsed >= CONFIRM_LOCK_MS + CONFIRM_WINDOW_MS ? CONFIRM_NONE : CONFIRM_OPEN);
        }
        else if (s_confirm == CONFIRM_OPEN && elapsed >= CONFIRM_WINDOW_MS)
        {
            set_confirm(CONFIRM_NONE);
        }
    }
    s_render(charinfo, param_1, param_2);
}

static void set_visible(void *control, bool visible)
{
    ((void (__thiscall *)(void *, int))(*(void ***)control)[MNU_VF_SET_VISIBLE])(control, visible);
}

// After the screen sets its controls up for the character shown: the button is
// only there for the avatar
static void __fastcall charinfo_refresh_hook(void *charinfo)
{
    s_refresh(charinfo);
    if (charinfo == s_charinfo && s_button)
    {
        bool avatar = at<int>(charinfo, CHARINFO_SHOWN) == 0;
        if (!avatar && s_confirm != CONFIRM_NONE)
        {
            set_confirm(CONFIRM_NONE);
        }
        set_visible(s_button, avatar);
    }
}

// The listener cuts the counts to a byte (a respec at level 47 and above gives
// more than 255 stat points): the screen gets them capped at 255, which keeps its
// buttons enabled, and the label shows the real count
static void __thiscall set_points_hook(void *charinfo, uint32_t ability_points, uint32_t stat_points)
{
    uint8_t *proxy = at<uint8_t *>(charinfo, CHARINFO_UIDBPROXY);
    uint16_t abilities = at<uint16_t>(proxy, PROXY_ABILITY_POINTS);
    uint16_t stats = at<uint16_t>(proxy, PROXY_STAT_POINTS);
    s_stat_points = stats;
    s_set_points(charinfo, abilities > 255 ? 255 : abilities, stats > 255 ? 255 : stats);
}

static bool patch_stat_points_label()
{
    // movzx ecx, byte [ebx + 0xe411] -> movzx ecx, word [s_stat_points], both 7 bytes
    static const uint8_t expected[] = {0x0f, 0xb6, 0x8b, 0x11, 0xe4, 0x00, 0x00};
    uintptr_t at_ = addr_of(RVA_STAT_POINTS_LABEL);
    if (memcmp((void *)at_, expected, sizeof(expected)) != 0)
    {
        return false;
    }
    uint8_t code[7] = {0x0f, 0xb7, 0x0d};
    *(uint32_t *)(code + 3) = (uint32_t)(uintptr_t)&s_stat_points;
    return write_code(at_, code, sizeof(code));
}

static void __thiscall charinfo_build_hook(void *charinfo)
{
    s_build(charinfo);
    char mesh_default[] = "ui_btn_standardgold_default.msh";
    char mesh_pressed[] = "ui_btn_standardgold_pressed.msh";
    char mesh_highlight[] = "";
    char mesh_disabled[] = "ui_btn_standardgold_disabled.msh";
    char label[] = "RESPEC";
    s_charinfo = charinfo;
    s_confirm = CONFIRM_NONE;
    s_button = sfsf->uiAPI->attachNewButton((CMnuContainer *)charinfo, mesh_default, mesh_pressed, mesh_highlight,
                                            mesh_disabled, label, BUTTON_FONT, BUTTON_X, BUTTON_Y, BUTTON_WIDTH,
                                            BUTTON_HEIGHT, 0, (uint32_t)&respec_click);
    if (s_button)
    {
        // SFSF stores the callback as {invoker, object, method} from +8; the game
        // calls +0x10 as the invoker, with +8 and &+0xc (CUiCCharInfo's own buttons)
        uint32_t *callback = (uint32_t *)((uint8_t *)s_button + BUTTON_CALLBACK);
        uint32_t invoker = callback[0], object = callback[1], method = callback[2];
        callback[0] = object;
        callback[1] = method;
        callback[2] = invoker;
    }
    logAPI->logInfo("[respec] button %p added to the character info screen %p", s_button, charinfo);
}

/* ------------------------------------------------------------------------ */
/* Module entry points                                                       */
/* ------------------------------------------------------------------------ */

extern "C" __declspec(dllexport) void InitModule(SpellforceSpellFramework *framework)
{
    sfsf = framework;
    logAPI = sfsf->logAPI;
    s_base = (uintptr_t)GetModuleHandleA(NULL);

    bool button = hook_vtable_slot(RVA_CHARINFO_VT_BUILD, (void *)&charinfo_build_hook, (void **)&s_build,
                                   RVA_CHARINFO_BUILD);
    bool server = hook_call(RVA_SERVER_RAISE_CALL, (void *)&server_raise_hook, (void **)&s_server_raise,
                            RVA_SERVER_RAISE);
    bool world = hook_call(RVA_WORLD_FIGURE_STAT_CALL, (void *)&world_figure_stat_hook,
                           (void **)&s_world_figure_stat, RVA_WORLD_FIGURE_STAT);
#ifdef RESPEC_DIAG
    hook_uidb_send();
#endif
    bool render = hook_vtable_slot(RVA_CHARINFO_VT_RENDER, (void *)&charinfo_render_hook, (void **)&s_render,
                                   RVA_CONTAINER_RENDER);
    static const uint8_t refresh_prologue[] = {0x53, 0x56, 0x8b, 0xf1, 0x57};
    bool refresh = hook_entry(RVA_CHARINFO_REFRESH, refresh_prologue, CHARINFO_REFRESH_PROLOGUE,
                              (void *)&charinfo_refresh_hook, (void **)&s_refresh);
    bool points = hook_call(RVA_POINTS_LISTENER_CALL, (void *)&set_points_hook, (void **)&s_set_points,
                            RVA_CHARINFO_SET_POINTS) &&
                  patch_stat_points_label();
    logAPI->logInfo("[respec] hooks: button %s, confirmation %s, server %s, world %s, hide for heroes %s, points "
                    "above 255 %s",
                    button ? "ok" : "FAILED", render ? "ok" : "FAILED", server ? "ok" : "FAILED",
                    world ? "ok" : "FAILED", refresh ? "ok" : "FAILED", points ? "ok" : "FAILED");
}

extern "C" __declspec(dllexport) SFMod *RegisterMod(SpellforceSpellFramework *framework)
{
    return framework->createModInfo("Respec", "1.0.0", "Angry Catster",
                                    "RESPEC button on the character info screen: back to the level 1 build.");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    return TRUE;
}
