#pragma once
// BetterIcons - more effect icons next to a unit's portrait (bottom left), with a bar showing how long each effect
// still lasts. SpellForce 1.61.11213. The game shows at most 4 positive effects (left of the portrait) and 4
// negative ones (right of it).
//
// Layout (each side on its own): up to 4 effects as in the game; a 5th and 6th make the icons of that column
// smaller so 5 or 6 fit in the same height; more effects start a new column of 6 towards the portrait (positive
// effects to the right of their column, negative ones to the left of theirs), at most 3 columns a side. Every icon
// gets a bar at its bottom with the share of the effect's time left, and below 10 seconds the seconds left (below
// 1 second as 0.5).
//
// Game side (addresses with the default image base):
//   The panel (class vtable 00C0D5C8, destructor 00A43480) makes in 00A43510 two columns of 4 buttons (new 238h,
//   00905A70; 26 x 26 at x 6 and x 141, 29 apart; layers "checkmark1.msh" and the border
//   "ui_btn_detailview_border_buff.msh" / "..._debuff.msh" with 009075E0; help text 0AAh / 0ABh; added with
//   00906F30): buttons [panel+39Ch+4*i] (positive) and [panel+3ACh+4*i] (negative). 00A49B10 (thiscall, 1 byte
//   argument) puts the figure's effects on them: for every spell on the figure (figure = handle [panel+38Ch] >> 8
//   when [panel+390h] is 1 and the handle's kind byte is 1) of type t (game object [panel+36Ch]: spells listed
//   with [obj+18h] 00679C30 / 006B2DE0, [obj+2Ch] 006FE240 / 006FDD90, type [obj+48h] 0072A960) shown when
//   u = [panel+384h] 0066E100(t) passes [panel+384h] 0066E470(u) (the panel shows the player's own figure:
//   word [[panel+358h] 009748B0 () + 2]) or [panel+36Ch] 0096AC13(u); side from [panel+384h] 0066DCF0(t)
//   (2 = positive, 1 = negative); the icon is set with 00A46F30(button, t) (texture, help, type in the high word
//   of the button's id).
//   Buttons (vtable 00BE5C70): +8 SetRect(x, y, w, h) relative to the parent (kept at +134h; it also makes the
//   button visible), +3Ch Show(bool), +0D0h IsShown, +0ECh id, flags +128h (2 = visible), parent +12Ch,
//   rectangle on the screen +144h. Spells: [obj+48h] = CGdSpell, records of 1Bh from +56h: ticks to the spell's
//   next step +0 (word), spell id +2; 10 ticks a second (the game tick, [CUiMain+7500h]).
// The patch:
//   - hooks the start of 00A49B10 (5 bytes move to a trampoline): after the game's own work it lays out all the
//     effects again with its own buttons beyond the 4 (made like the game's) and remembers each icon's spell;
//     the same runs once per frame from CUiMain's update (vtable 00C06ADC +238h);
//   - forgets a panel in its destructor (vtable slot 0);
//   - time left: an effect whose ticks to the next step only go down is over when they reach 0 (its bar is the
//     share of the most ticks seen). Effects that work in steps (damage over time) count their steps in the
//     spell's xdata (CGdXDataList [CGdSpell+50h] 00754210(xdata key at record +16h, key)) with the spell's data
//     (resource [CGdSpell+3Ch] 0066DC40(out, spell id), parameters as milliseconds, ticks = ms * 10 / 1000):
//       ice strike (spell job 8, 62h, 0073DD50): key 12h = the next hit's damage, from +20h down by 1 a step; it
//       ends when that reaches 0 (key 5 only counts the frozen steps), step +24h;
//       fire burst (1) and fireball (67h): key 12h = steps done, they go on while it is at most +28h, step +2Ch;
//       poison (4): key 12h = steps done, on while below +28h, step +24h;
//       roots (0A3h, 00745880): key 5 = steps done, on while at most +34h, step +20h;
//       feet of clay (9Bh): key 5 = steps done, on while at most +28h, step +14h;
//       hypnotize (76h): key 5 = steps done, on while below +24h, step from the caster's level (the ticks seen);
//       pestilence (19h), summons (17h) and raise dead (34h) have no end: no bar.
//     The time left is then the ticks to the next step plus the steps left times the step. Other stepping effects
//     (hallow 31h, area roots 90h, area hypnotize 9Ah, and any other effect seen to start over)
//     show a bar once one effect of the same spell was watched from start to end: its length is learned and kept
//     in BetterIcons.dat next to the .asi/.sfm for the next games;
//   - a panel that shows no figure loses the patch's icons and bars;
//   - draws bars and seconds at the end of the frame (Direct3D EndScene, found like CooldownClock).
// Only what is shown changes, so it is safe in multiplayer. Settings in BetterIcons.txt next to the .asi/.sfm.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define UPDATE_ICONS_RVA     0x649B10 // panel: put the effects on the buttons
#define UPDATE_ICONS_MOVED   5        // push ebp; mov ebp,esp; push -1
#define PANEL_VTABLE_RVA     0x80D5C8
#define PANEL_DESTRUCTOR_RVA 0x643480
#define MAIN_VTABLE_RVA      0x806ADC
#define MAIN_VT_UPDATE       0x238
#define GET_PROC_ADDRESS_IAT 0x788060 // KERNEL32!GetProcAddress in the game's import table

#define NEW_RVA              0x675A9D // operator new (cdecl)
#define BUTTON_CTOR_RVA      0x505A70
#define STRING_EMPTY_RVA     0x383900 // CUtlString()
#define STRING_CTOR_RVA      0x3837E0 // CUtlString(const char*)
#define STRING_DTOR_RVA      0x3839C0
#define STRING_FORMAT_RVA    0x384170 // (CUtlString*, format, ...) cdecl
#define ADD_LAYER_RVA        0x5075E0 // button: (texture, name, 0, rect, 0, 0)
#define HELP_TEXT_RVA        0x574A10 // text db: (CUtlString* out, id, 1)
#define ADD_CHILD_RVA        0x506F30 // panel: (control, 1, 1, 0)
#define SET_ICON_RVA         0x646F30 // panel: (button, type)
#define TYPE_INFO_RVA        0x26E100 // [panel+384h]: (type) -> u
#define TYPE_SIDE_RVA        0x26DCF0 // [panel+384h]: (type) -> 1 negative, 2 positive
#define OWN_SHOWN_RVA        0x26E470 // [panel+384h]: (u)
#define OTHER_SHOWN_RVA      0x56AC13 // [panel+36Ch]: (u)
#define SHOWN_FIGURE_RVA     0x5748B0 // [panel+358h]: () -> word [+2]
#define FIGURE_EXISTS_RVA    0x279C30
#define FIRST_SPELL_RVA      0x2B2DE0
#define NEXT_SPELL_RVA       0x2FE240
#define NODE_SPELL_RVA       0x2FDD90
#define SPELL_TYPE_RVA       0x32A960
#define SPELL_DATA_RVA       0x26DC40 // resource: (out, spell id) -> out (spell data, parameters from +24h)
#define XDATA_GET_RVA        0x354210 // xdata list: (xdata key, key) -> value

#define BUTTON_SIZE          0x238
#define BUTTON_NAME_FORMAT_A 0x80DCB0 // the game's name formats of the two columns
#define BUTTON_NAME_FORMAT_B 0x80DCF4
#define STRING_EMPTY_TEXT    0x78FA00 // ""
#define STRING_CHECKMARK     0x794A04 // "checkmark1.msh"
#define STRING_BORDER_A      0x80DCD0 // "ui_btn_detailview_border_buff.msh"
#define STRING_BORDER_B      0x80DD18 // "ui_btn_detailview_border_debuff.msh"
#define HELP_ID_A            0xAA
#define HELP_ID_B            0xAB

#define OBJ_FIGURES          0x18
#define OBJ_NODES            0x2C
#define OBJ_SPELLS           0x48
#define SPELL_RECORDS        0x56
#define SPELL_RECORD_SIZE    0x1B
#define SPELL_ID             0x02
#define SPELL_JOB            0x06
#define SPELL_XDATA_KEY      0x16
#define SPELLS_RESOURCE      0x3C
#define SPELLS_XDATA         0x50
#define XDATA_STEPS_LEFT     0x05
#define XDATA_STEPS_DONE     0x12
#define PANEL_TEXTS          0x358
#define PANEL_GAME           0x36C
#define PANEL_TYPE_DATA      0x384
#define PANEL_HANDLE         0x38C
#define PANEL_HAS_HANDLE     0x390
#define PANEL_BUTTONS_A      0x39C
#define PANEL_BUTTONS_B      0x3AC
#define PANEL_TYPES_A        0x4B8
#define PANEL_TYPES_B        0x4C0
#define CONTROL_FLAGS        0x128
#define CONTROL_PARENT       0x12C
#define CONTROL_REL_RECT     0x134
#define CONTROL_ABS_RECT     0x144
#define FLAG_VISIBLE         2
#define VT_SET_RECT          0x08
#define VT_SHOW              0x3C
#define VT_SET_ID            0x48
#define VT_UNKNOWN_54        0x54
#define VT_SET_NAME          0x84
#define VT_SET_HELP          0x8C
#define VT_IS_SHOWN          0xD0
#define VT_GET_ID            0xEC
#define MAIN_NOW             0x7500
#define HANDLE_FIGURE        1
#define SIDE_NEGATIVE        1
#define SIDE_POSITIVE        2

#define GAME_BUTTONS         4      // per side
#define ROWS_MAX             6
#define COLUMNS_MAX          3
#define SLOTS                (ROWS_MAX * COLUMNS_MAX)
#define ICON_GAP             3.0f
#define MAX_PANELS           4
#define MAX_SPELLS           512    // guard against a broken list
#define SPELL_INDEXES        800
#define SPELL_IDS            0x10000
#define MAX_PARENT_DEPTH     32
#define D3D_CREATE_DEVICE    16     // IDirect3D9 vtable index
#define D3D_END_SCENE        42     // IDirect3DDevice9 vtable index
#define SNAPSHOT_MAX_AGE_MS  250

static const char DefaultConfig[] =
    "# BetterIcons (.asi or .sfm) - up to 18 effect icons a side next to the portrait, with a bar of the time left.\r\n"
    "# Height of the bar in percent of the icon's height (0 = no bar).\r\n"
    "BarHeight=14\r\n"
    "# The seconds left are written in the icon below this many seconds (0 = never).\r\n"
    "ShowSecondsBelow=10\r\n"
    "# Log=1 writes BetterIcons.log next to this file.\r\n"
    "Log=0\r\n";

/* ------------------------------------------------------------------------ */
/* Settings and log                                                          */
/* ------------------------------------------------------------------------ */

static HINSTANCE ModuleInstance;
static char LearnedPath[MAX_PATH];
static int BarHeight = 14;
static int ShowSecondsBelow = 10;
static BOOL LogEnabled;
static char LogPath[MAX_PATH];

static void Log(const char* format, ...)
{
    FILE* file;
    va_list args;
    if (!LogEnabled || fopen_s(&file, LogPath, "a") != 0) return;
    va_start(args, format);
    vfprintf(file, format, args);
    va_end(args);
    fputs("\n", file);
    fclose(file);
}

static void ReadConfig(void)
{
    char path[MAX_PATH];
    char line[256];
    char* slash;
    FILE* file;

    if (GetModuleFileNameA(ModuleInstance, path, MAX_PATH) == 0) return;
    slash = strrchr(path, '\\');
    if (slash == NULL) return;
    strcpy_s(LogPath, MAX_PATH, path);
    strcpy_s(LogPath + (slash + 1 - path), MAX_PATH - (slash + 1 - path), "BetterIcons.log");
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "BetterIcons.dat");
    strcpy_s(LearnedPath, MAX_PATH, path);
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "BetterIcons.txt");

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
        if (_stricmp(line, "BarHeight") == 0) BarHeight = atoi(value);
        else if (_stricmp(line, "ShowSecondsBelow") == 0) ShowSecondsBelow = atoi(value);
        else if (_stricmp(line, "Log") == 0) LogEnabled = atoi(value) != 0;
    }
    fclose(file);
    if (BarHeight < 0) BarHeight = 0;
    if (BarHeight > 50) BarHeight = 50;
    if (ShowSecondsBelow < 0) ShowSecondsBelow = 0;
}

/* ------------------------------------------------------------------------ */
/* Game functions                                                            */
/* ------------------------------------------------------------------------ */

typedef struct { BYTE Bytes[64]; } GameString; // CUtlString (smaller than this)
typedef struct { float Left, Top, Right, Bottom; } GameRect;

typedef void* (__cdecl *NewFn)(size_t size);
typedef void (__fastcall *ThisOnlyFn)(void* self, void* unused);
typedef void (__fastcall *StringCtorFn)(GameString* string, void* unused, const char* text);
typedef GameString* (__cdecl *StringFormatFn)(GameString* string, const void* format, ...);
typedef void (__fastcall *AddLayerFn)(BYTE* button, void* unused, GameString* texture, GameString* name, DWORD a,
                                      GameRect rect, DWORD b, DWORD c);
typedef GameString* (__fastcall *HelpTextFn)(BYTE* texts, void* unused, GameString* out, DWORD id, DWORD one);
typedef void (__fastcall *AddChildFn)(BYTE* panel, void* unused, BYTE* child, DWORD a, DWORD b, DWORD c);
typedef void (__fastcall *SetIconFn)(BYTE* panel, void* unused, BYTE* button, DWORD type);
typedef WORD (__fastcall *WordOfWordFn)(BYTE* self, void* unused, DWORD value);
typedef BYTE (__fastcall *ByteOfWordFn)(BYTE* self, void* unused, DWORD value);
typedef int (__fastcall *IntOfWordFn)(BYTE* self, void* unused, DWORD value);
typedef BYTE* (__fastcall *PointerFn)(BYTE* self, void* unused);
typedef void (__fastcall *UpdateIconsFn)(BYTE* panel, void* unused, DWORD rebuild);
typedef BYTE* (__fastcall *SpellDataFn)(BYTE* resource, void* unused, BYTE* out, DWORD spellId);
typedef DWORD (__fastcall *XDataGetFn)(BYTE* xdata, void* unused, DWORD xdataKey, DWORD key);
typedef void* (__fastcall *DestructorFn)(BYTE* panel, void* unused, DWORD flags);
typedef void (__fastcall *MainUpdateFn)(BYTE* main, void* unused, float a, float b);
// Button virtuals
typedef void (__fastcall *SetRectFn)(BYTE* button, void* unused, float x, float y, float w, float h);
typedef void (__fastcall *DwordArgFn)(BYTE* button, void* unused, DWORD value);
typedef void (__fastcall *StringArgFn)(BYTE* button, void* unused, GameString* text);
typedef BYTE (__fastcall *IsShownFn)(BYTE* button, void* unused);
typedef DWORD (__fastcall *GetIdFn)(BYTE* button, void* unused);

static BYTE* GameBase;
static UpdateIconsFn OriginalUpdateIcons;
static DestructorFn OriginalDestructor;
static MainUpdateFn OriginalMainUpdate;

static NewFn GameNew;
static ThisOnlyFn ButtonCtor;
static ThisOnlyFn StringEmpty;
static StringCtorFn StringCtor;
static ThisOnlyFn StringDtor;
static StringFormatFn StringFormat;
static AddLayerFn AddLayer;
static HelpTextFn HelpText;
static AddChildFn AddChild;
static SetIconFn SetIcon;
static WordOfWordFn TypeInfo;
static ByteOfWordFn TypeSide;
static IntOfWordFn OwnShown;
static ByteOfWordFn OtherShown;
static PointerFn ShownFigure;
static IntOfWordFn FigureExists;
static WordOfWordFn FirstSpell;
static WordOfWordFn NextSpell;
static WordOfWordFn NodeSpell;
static WordOfWordFn SpellType;
static SpellDataFn SpellData;
static XDataGetFn XDataGet;
static BYTE* NameFormat[2];
static const char* BorderTexture[2];
static const char* EmptyText;
static const char* CheckmarkTexture;
static DWORD PanelVtable;

static void SetUpGame(BYTE* base)
{
    GameNew = (NewFn)(base + NEW_RVA);
    ButtonCtor = (ThisOnlyFn)(base + BUTTON_CTOR_RVA);
    StringEmpty = (ThisOnlyFn)(base + STRING_EMPTY_RVA);
    StringCtor = (StringCtorFn)(base + STRING_CTOR_RVA);
    StringDtor = (ThisOnlyFn)(base + STRING_DTOR_RVA);
    StringFormat = (StringFormatFn)(base + STRING_FORMAT_RVA);
    AddLayer = (AddLayerFn)(base + ADD_LAYER_RVA);
    HelpText = (HelpTextFn)(base + HELP_TEXT_RVA);
    AddChild = (AddChildFn)(base + ADD_CHILD_RVA);
    SetIcon = (SetIconFn)(base + SET_ICON_RVA);
    TypeInfo = (WordOfWordFn)(base + TYPE_INFO_RVA);
    TypeSide = (ByteOfWordFn)(base + TYPE_SIDE_RVA);
    OwnShown = (IntOfWordFn)(base + OWN_SHOWN_RVA);
    OtherShown = (ByteOfWordFn)(base + OTHER_SHOWN_RVA);
    ShownFigure = (PointerFn)(base + SHOWN_FIGURE_RVA);
    FigureExists = (IntOfWordFn)(base + FIGURE_EXISTS_RVA);
    FirstSpell = (WordOfWordFn)(base + FIRST_SPELL_RVA);
    NextSpell = (WordOfWordFn)(base + NEXT_SPELL_RVA);
    NodeSpell = (WordOfWordFn)(base + NODE_SPELL_RVA);
    SpellType = (WordOfWordFn)(base + SPELL_TYPE_RVA);
    SpellData = (SpellDataFn)(base + SPELL_DATA_RVA);
    XDataGet = (XDataGetFn)(base + XDATA_GET_RVA);
    NameFormat[0] = base + BUTTON_NAME_FORMAT_A;
    NameFormat[1] = base + BUTTON_NAME_FORMAT_B;
    BorderTexture[0] = (const char*)(base + STRING_BORDER_A);
    BorderTexture[1] = (const char*)(base + STRING_BORDER_B);
    EmptyText = (const char*)(base + STRING_EMPTY_TEXT);
    CheckmarkTexture = (const char*)(base + STRING_CHECKMARK);
    PanelVtable = (DWORD)(base + PANEL_VTABLE_RVA);
}
#define VIRTUAL(type, object, offset) ((type)(*(BYTE***)(object))[(offset) / 4])

/* ------------------------------------------------------------------------ */
/* Layout                                                                    */
/* ------------------------------------------------------------------------ */

// Where icon <i> of <n> on a side goes, relative to the panel: <first> is the game's first icon of that side
// (x, y, size); the left side grows to the right, the right side to the left.
static void IconPlace(int positive, int n, int i, float firstX, float firstY, float size, float* x, float* y, float* s)
{
    float height = (GAME_BUTTONS - 1) * (size + ICON_GAP) + size; // the game's column of 4
    int rows = n <= GAME_BUTTONS ? GAME_BUTTONS : (n < ROWS_MAX ? n : ROWS_MAX);
    float side = n <= GAME_BUTTONS ? size : (float)(int)((height - (rows - 1) * ICON_GAP) / rows);
    int column = i / rows, row = i % rows;

    *s = side;
    *y = firstY + row * (side + ICON_GAP);
    if (positive) *x = firstX + column * (side + ICON_GAP);
    else *x = firstX + size - side - column * (side + ICON_GAP);
}

/* ------------------------------------------------------------------------ */
/* Panels                                                                    */
/* ------------------------------------------------------------------------ */

typedef struct
{
    BYTE* Panel;
    BOOL HavePlaces;
    float FirstX[2], FirstY[2], Size[2];  // [0] positive (left), [1] negative (right)
    BYTE* Extra[2][SLOTS - GAME_BUTTONS]; // the patch's buttons
    int ExtraCount[2];
    WORD Spell[2][SLOTS];                 // spell of each shown icon
    int Shown[2];
    DWORD Figure;                         // figure the bars were last worked out for
    WORD Before[2][SLOTS];                // its spells then
    int BeforeCount[2];
    BOOL HaveBefore;
    float Placed[2][SLOTS][4];            // last rectangle given to each button
} PanelState;

static PanelState Panels[MAX_PANELS];

static PanelState* FindPanel(BYTE* panel, BOOL add)
{
    int i, free = -1;
    for (i = 0; i < MAX_PANELS; i++)
    {
        if (Panels[i].Panel == panel) return &Panels[i];
        if (Panels[i].Panel == NULL && free < 0) free = i;
    }
    if (!add || free < 0) return NULL;
    memset(&Panels[free], 0, sizeof(PanelState));
    Panels[free].Panel = panel;
    Log("effect panel %p", panel);
    return &Panels[free];
}

static BYTE* SideButton(PanelState* state, int side, int i)
{
    if (i < GAME_BUTTONS) return *(BYTE**)(state->Panel + (side ? PANEL_BUTTONS_B : PANEL_BUTTONS_A) + 4 * i);
    return i - GAME_BUTTONS < state->ExtraCount[side] ? state->Extra[side][i - GAME_BUTTONS] : NULL;
}

// A button like the game's own (00A43510), as icon <index> of <side>.
static BYTE* MakeButton(BYTE* panel, int side, int index)
{
    GameString name, texture, empty, help;
    GameRect none = { 0, 0, 0, 0 };
    BYTE* button = GameNew(BUTTON_SIZE);
    if (button == NULL) return NULL;
    ButtonCtor(button, NULL);

    memset(&name, 0, sizeof(name));
    StringEmpty(&name, NULL);
    StringFormat(&name, NameFormat[side], index);
    VIRTUAL(StringArgFn, button, VT_SET_NAME)(button, NULL, &name);
    StringDtor(&name, NULL);

    VIRTUAL(SetRectFn, button, VT_SET_RECT)(button, NULL, 0, 0, 26, 26);
    StringCtor(&empty, NULL, EmptyText);
    StringCtor(&texture, NULL, CheckmarkTexture);
    AddLayer(button, NULL, &texture, &empty, 0, none, 0, 0);
    StringDtor(&texture, NULL);
    StringCtor(&texture, NULL, BorderTexture[side]);
    AddLayer(button, NULL, &texture, &empty, 0, none, 0, 0);
    StringDtor(&texture, NULL);
    StringDtor(&empty, NULL);

    VIRTUAL(DwordArgFn, button, VT_SET_ID)(button, NULL, (DWORD)index);
    VIRTUAL(DwordArgFn, button, VT_UNKNOWN_54)(button, NULL, 0);
    HelpText(*(BYTE**)(panel + PANEL_TEXTS), NULL, &help, side ? HELP_ID_B : HELP_ID_A, 1);
    VIRTUAL(StringArgFn, button, VT_SET_HELP)(button, NULL, &help);
    StringDtor(&help, NULL);

    AddChild(panel, NULL, button, 1, 1, 0);
    VIRTUAL(DwordArgFn, button, VT_SHOW)(button, NULL, 0);
    Log("button %d of side %d made: %p", index, side, button);
    return button;
}

typedef struct { WORD Type, Spell; } ShownEffect;

// The effects the game shows on the panel's figure, by side, in the game's order.
static void CollectEffects(BYTE* panel, DWORD figure, ShownEffect lists[2][SLOTS], int counts[2])
{
    BYTE* game = *(BYTE**)(panel + PANEL_GAME);
    BYTE* figures = *(BYTE**)(game + OBJ_FIGURES);
    BYTE* nodes = *(BYTE**)(game + OBJ_NODES);
    BYTE* spells = *(BYTE**)(game + OBJ_SPELLS);
    BYTE* typeData = *(BYTE**)(panel + PANEL_TYPE_DATA);
    BOOL own = *(WORD*)(ShownFigure(*(BYTE**)(panel + PANEL_TEXTS), NULL) + 2) == figure;
    WORD node;
    int guard;

    counts[0] = counts[1] = 0;
    if (!FigureExists(figures, NULL, figure)) return;
    for (node = FirstSpell(figures, NULL, figure), guard = 0; node != 0 && guard < MAX_SPELLS;
         node = NextSpell(nodes, NULL, node), guard++)
    {
        WORD spell = NodeSpell(nodes, NULL, node);
        WORD type = SpellType(spells, NULL, spell);
        WORD info = TypeInfo(typeData, NULL, type);
        BOOL shown = own ? OwnShown(typeData, NULL, info) != 0
                         : OtherShown(*(BYTE**)(panel + PANEL_GAME), NULL, info) != 0;
        BYTE kind;
        int side;
        if (!shown) continue;
        kind = TypeSide(typeData, NULL, type);
        if (kind == SIDE_POSITIVE) side = 0;
        else if (kind == SIDE_NEGATIVE) side = 1;
        else continue;
        if (counts[side] == SLOTS) continue;
        lists[side][counts[side]].Type = type;
        lists[side][counts[side]].Spell = spell;
        counts[side]++;
    }
}

static void PlaceButton(PanelState* state, int side, int i, BYTE* button, float x, float y, float s)
{
    float* placed = state->Placed[side][i];
    if (placed[0] == x && placed[1] == y && placed[2] == s && placed[3] == s) return;
    VIRTUAL(SetRectFn, button, VT_SET_RECT)(button, NULL, x, y, s, s);
    placed[0] = x; placed[1] = y; placed[2] = s; placed[3] = s;
}

static void HideButton(BYTE* button)
{
    if (button != NULL && VIRTUAL(IsShownFn, button, VT_IS_SHOWN)(button, NULL)) VIRTUAL(DwordArgFn, button, VT_SHOW)(button, NULL, 0);
}

static void LayOutSide(PanelState* state, int side, const ShownEffect* list, int n)
{
    BYTE* panel = state->Panel;
    int i;
    for (i = 0; i < SLOTS; i++)
    {
        BYTE* button = SideButton(state, side, i);
        float x, y, s;
        if (i >= n)
        {
            HideButton(button);
            if (i < GAME_BUTTONS) *(WORD*)(panel + (side ? PANEL_TYPES_B : PANEL_TYPES_A) + 2 * i) = 0;
            continue;
        }
        if (button == NULL)
        {
            button = MakeButton(panel, side, i);
            if (button == NULL) break;
            state->Extra[side][state->ExtraCount[side]++] = button;
            memset(state->Placed[side][i], 0, sizeof(state->Placed[side][i]));
        }
        IconPlace(side == 0, n, i, state->FirstX[side], state->FirstY[side], state->Size[side], &x, &y, &s);
        PlaceButton(state, side, i, button, x, y, s);
        if ((VIRTUAL(GetIdFn, button, VT_GET_ID)(button, NULL) >> 16) != list[i].Type)
        {
            SetIcon(panel, NULL, button, list[i].Type);
        }
        if (!VIRTUAL(IsShownFn, button, VT_IS_SHOWN)(button, NULL)) VIRTUAL(DwordArgFn, button, VT_SHOW)(button, NULL, 1);
        if (i < GAME_BUTTONS) *(WORD*)(panel + (side ? PANEL_TYPES_B : PANEL_TYPES_A) + 2 * i) = list[i].Type;
        state->Spell[side][i] = list[i].Spell;
    }
    state->Shown[side] = n < i ? n : i;
}

static BOOL IsPanel(BYTE* panel)
{
    return panel != NULL && *(DWORD*)panel == PanelVtable;
}

static void LayOutPanel(BYTE* panel)
{
    PanelState* state;
    ShownEffect lists[2][SLOTS];
    int counts[2] = { 0, 0 }, side;
    DWORD handle;

    if (!IsPanel(panel)) return;
    if (*(DWORD*)(panel + PANEL_HAS_HANDLE) != 1)
    {
        // Nothing shown on the panel: the game empties its own buttons, the patch its own
        state = FindPanel(panel, FALSE);
        if (state != NULL)
        {
            for (side = 0; side < 2; side++)
            {
                int i;
                for (i = 0; i < state->ExtraCount[side]; i++) HideButton(state->Extra[side][i]);
                state->Shown[side] = 0;
            }
        }
        return;
    }
    state = FindPanel(panel, TRUE);
    if (state == NULL) return;
    if (!state->HavePlaces)
    {
        for (side = 0; side < 2; side++)
        {
            BYTE* first = SideButton(state, side, 0);
            if (first == NULL) return;
            state->FirstX[side] = *(float*)(first + CONTROL_REL_RECT);
            state->FirstY[side] = *(float*)(first + CONTROL_REL_RECT + 4);
            state->Size[side] = *(float*)(first + CONTROL_REL_RECT + 8);
            if (state->Size[side] < 4.0f) return;
        }
        state->HavePlaces = TRUE;
        Log("panel %p: positive icons from %.0f/%.0f, negative from %.0f/%.0f, size %.0f", panel, state->FirstX[0],
            state->FirstY[0], state->FirstX[1], state->FirstY[1], state->Size[0]);
    }
    handle = *(DWORD*)(panel + PANEL_HANDLE);
    if ((handle & 0xFF) == HANDLE_FIGURE && handle >= 0x100) CollectEffects(panel, handle >> 8, lists, counts);
    for (side = 0; side < 2; side++) LayOutSide(state, side, lists[side], counts[side]);
}

static void __fastcall UpdateIconsHook(BYTE* panel, void* unused, DWORD rebuild)
{
    OriginalUpdateIcons(panel, NULL, rebuild);
    __try
    {
        LayOutPanel(panel);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static void* __fastcall DestructorHook(BYTE* panel, void* unused, DWORD flags)
{
    PanelState* state = FindPanel(panel, FALSE);
    if (state != NULL)
    {
        state->Panel = NULL; // its buttons go with it
        Log("effect panel %p destroyed", panel);
    }
    return OriginalDestructor(panel, NULL, flags);
}

/* ------------------------------------------------------------------------ */
/* Time left                                                                 */
/* ------------------------------------------------------------------------ */

typedef struct
{
    WORD Id;          // spell id watched in this spell slot (0 = none)
    BOOL Periodic;    // its ticks went up again: it works in steps
    BOOL FromStart;   // watched since it began
    float First;      // game tick first seen
    float Last;       // game tick last seen
    int LastToDo;
    int MostToDo;
    int MostLeft;     // most ticks left seen (stepping effects)
} SpellWatch;

static SpellWatch Watches[SPELL_INDEXES];
static float Learned[SPELL_IDS]; // length in ticks of spells that work in steps, by spell id
static float TicksPerSecond = 10.0f;
static float RateTickStart;
static DWORD RateMsStart;

static void MeasureTickRate(float now)
{
    DWORD ms = GetTickCount();
    if (RateMsStart == 0 || now < RateTickStart)
    {
        RateMsStart = ms;
        RateTickStart = now;
        return;
    }
    if (ms - RateMsStart >= 2000)
    {
        float ticks = now - RateTickStart;
        float rate = ticks * 1000.0f / (float)(ms - RateMsStart);
        if (ticks > 0.0f && rate > 0.5f && rate < 1000.0f) TicksPerSecond = rate; // paused: keep the last rate
        RateMsStart = ms;
        RateTickStart = now;
    }
}

// Effects that count their steps in xdata (see the top of the file)
#define HITS_LEFT_KEY12      1 // key 12h = hits left, down by 1 a step, starts at the limit
#define STEPS_DONE_AT_MOST   2 // key 12h = steps done, on while <= limit
#define STEPS_DONE_BELOW     3 // key 12h = steps done, on while < limit
#define RUNS_KEY5            4 // key 5 = steps done, as many steps as the limit
#define STEP_SEEN            0 // StepParam: the step is the most ticks seen
typedef struct { BYTE Job, Rule, StepParam, LimitParam; } StepRule;
static const StepRule StepRules[] =
{
    { 0x08, HITS_LEFT_KEY12, 0x24, 0x20 },    // ice strike: step, first hit's damage (= number of hits)
    { 0x62, HITS_LEFT_KEY12, 0x24, 0x20 },    // ice strike (tower)
    { 0x01, STEPS_DONE_AT_MOST, 0x2C, 0x28 }, // fire burst: step, steps
    { 0x67, STEPS_DONE_AT_MOST, 0x2C, 0x28 }, // fireball
    { 0x04, STEPS_DONE_BELOW, 0x24, 0x28 },   // poison
    { 0xA3, RUNS_KEY5, 0x20, 0x34 },          // roots
    { 0x9B, RUNS_KEY5, 0x14, 0x28 },          // feet of clay
    { 0x76, RUNS_KEY5, STEP_SEEN, 0x24 },     // hypnotize (step from the caster's level)
};

// Effects known to work in steps whose length is not worked out: no bar until it is learned
static const BYTE SteppingJobsLearned[] = { 0x31, 0x90, 0x9A }; // hallow, roots / hypnotize (area)
// Effects without an end: pestilence (goes on while the figure lives), summons (wolf, bear, skeleton, demon...)
// and raise dead (the creature stays): no bar
static const BYTE EndlessJobs[] = { 0x19, 0x17, 0x34 };

static BOOL IsEndlessJob(WORD job)
{
    int i;
    for (i = 0; i < sizeof(EndlessJobs); i++)
    {
        if (EndlessJobs[i] == job) return TRUE;
    }
    return FALSE;
}

static BOOL IsSteppingJobLearned(WORD job)
{
    int i;
    for (i = 0; i < sizeof(SteppingJobsLearned); i++)
    {
        if (SteppingJobsLearned[i] == job) return TRUE;
    }
    return FALSE;
}

static int MsToTicks(DWORD ms)
{
    return (int)(ms * 10 / 1000);
}

// Ticks left and whole length of a stepping effect from its xdata and its spell's data; FALSE for other effects.
static BOOL SteppingTimeLeft(BYTE* spells, BYTE* record, int toDo, int stepSeen, int* left, int* total)
{
    BYTE data[0x80];
    BYTE* params;
    const StepRule* rule = NULL;
    DWORD key = *(WORD*)(record + SPELL_XDATA_KEY), counter;
    int i, step, limit;

    for (i = 0; i < sizeof(StepRules) / sizeof(StepRules[0]); i++)
    {
        if (StepRules[i].Job == *(WORD*)(record + SPELL_JOB)) rule = &StepRules[i];
    }
    if (rule == NULL || key == 0) return FALSE;
    memset(data, 0, sizeof(data));
    params = SpellData(*(BYTE**)(spells + SPELLS_RESOURCE), NULL, data, *(WORD*)(record + SPELL_ID));
    if (params == NULL) return FALSE;
    step = rule->StepParam == STEP_SEEN ? stepSeen : MsToTicks(*(DWORD*)(params + rule->StepParam));
    limit = (int)*(DWORD*)(params + rule->LimitParam);
    if (step <= 0) return FALSE;
    switch (rule->Rule)
    {
    case HITS_LEFT_KEY12:
        counter = XDataGet(*(BYTE**)(spells + SPELLS_XDATA), NULL, key, XDATA_STEPS_DONE);
        limit &= 0xFFFF; // a word
        *left = toDo + ((int)counter > 1 ? ((int)counter - 1) * step : 0);
        *total = limit * step;
        break;
    case RUNS_KEY5:
        counter = XDataGet(*(BYTE**)(spells + SPELLS_XDATA), NULL, key, XDATA_STEPS_LEFT);
        *left = toDo + ((int)counter < limit ? (limit - (int)counter) * step : 0);
        *total = limit * step;
        break;
    case STEPS_DONE_AT_MOST:
        counter = XDataGet(*(BYTE**)(spells + SPELLS_XDATA), NULL, key, XDATA_STEPS_DONE);
        *left = toDo + ((int)counter <= limit ? (limit - (int)counter + 1) * step : 0);
        *total = (limit + 1) * step;
        break;
    default:
        counter = XDataGet(*(BYTE**)(spells + SPELLS_XDATA), NULL, key, XDATA_STEPS_DONE);
        *left = toDo + ((int)counter < limit ? (limit - (int)counter) * step : 0);
        *total = limit * step;
        break;
    }
    if (*total < *left) *total = *left;
    return TRUE;
}

// Ticks left and the effect's whole length in ticks for spell slot <index>; FALSE when not known. <started>: the
// spell has just come on a figure that was being watched.
static BOOL TimeLeft(BYTE* spells, WORD index, float now, BOOL started, float* left, float* total)
{
    BYTE* record;
    SpellWatch* watch;
    WORD id;
    int toDo;

    if (index == 0 || index >= SPELL_INDEXES) return FALSE;
    record = spells + SPELL_RECORDS + index * SPELL_RECORD_SIZE;
    id = *(WORD*)(record + SPELL_ID);
    toDo = *(WORD*)record;
    watch = &Watches[index];
    if (IsEndlessJob(*(WORD*)(record + SPELL_JOB))) return FALSE;
    if (started || watch->Id != id)
    {
        watch->Id = id;
        watch->Periodic = IsSteppingJobLearned(*(WORD*)(record + SPELL_JOB));
        watch->FromStart = started;
        watch->First = now;
        watch->MostToDo = toDo;
        watch->MostLeft = 0;
    }
    else if (toDo > watch->LastToDo + 1 && now - watch->First > 5.0f)
    {
        watch->Periodic = TRUE; // started over: it works in steps
    }
    watch->Last = now;
    watch->LastToDo = toDo;
    if (toDo > watch->MostToDo) watch->MostToDo = toDo;

    {
        int stepLeft, stepTotal;
        if (SteppingTimeLeft(spells, record, toDo, watch->MostToDo, &stepLeft, &stepTotal))
        {
            if (stepLeft > watch->MostLeft) watch->MostLeft = stepLeft;
            *left = (float)stepLeft;
            *total = (float)(stepTotal > watch->MostLeft ? stepTotal : watch->MostLeft);
            return TRUE;
        }
    }
    if (!watch->Periodic)
    {
        if (watch->MostToDo <= 0) return FALSE;
        *left = (float)toDo;
        *total = (float)watch->MostToDo;
        return TRUE;
    }
    if (Learned[id] <= 0.0f) return FALSE;
    *total = Learned[id];
    *left = Learned[id] - (now - watch->First);
    if (*left < 0.0f) *left = 0.0f;
    return TRUE;
}

// Learned lengths, kept for the next games

static void LoadLearned(void)
{
    FILE* file;
    unsigned id;
    float ticks;
    if (LearnedPath[0] == 0 || fopen_s(&file, LearnedPath, "r") != 0) return;
    while (fscanf_s(file, "%u %f", &id, &ticks) == 2)
    {
        if (id < SPELL_IDS && ticks > 0.0f) Learned[id] = ticks;
    }
    fclose(file);
}

static void SaveLearned(void)
{
    FILE* file;
    int id;
    if (LearnedPath[0] == 0 || fopen_s(&file, LearnedPath, "w") != 0) return;
    for (id = 1; id < SPELL_IDS; id++)
    {
        if (Learned[id] > 0.0f) fprintf(file, "%d %.0f\n", id, Learned[id]);
    }
    fclose(file);
}

// The spell left the watched figure: a stepping effect watched from its start teaches its spell's length.
static void SpellEnded(WORD index)
{
    SpellWatch* watch;
    if (index == 0 || index >= SPELL_INDEXES) return;
    watch = &Watches[index];
    if (watch->Id != 0 && watch->Periodic && watch->FromStart && watch->Last > watch->First)
    {
        Learned[watch->Id] = watch->Last - watch->First;
        Log("spell %u lasts %.0f ticks", watch->Id, Learned[watch->Id]);
        SaveLearned();
    }
    watch->Id = 0;
}

static BOOL Contains(const WORD* list, int count, WORD value)
{
    int i;
    for (i = 0; i < count; i++)
    {
        if (list[i] == value) return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------ */
/* Once per frame                                                            */
/* ------------------------------------------------------------------------ */

typedef struct
{
    float X, Y, Width, Height; // UI coordinates
    float Share;               // time left, 0-1
    float Seconds;
    int Negative;
} Bar;

#define MAX_BARS (MAX_PANELS * 2 * SLOTS)
static Bar Bars[MAX_BARS];
static int BarCount;
static float RootWidth, RootHeight;
static volatile DWORD SnapshotTick;

// Visible when the control and all its parents are; also gives the top control's size (the screen).
static BOOL IsShown(BYTE* control, float* rootWidth, float* rootHeight)
{
    BYTE* node = control;
    int depth;
    for (depth = 0; depth < MAX_PARENT_DEPTH; depth++)
    {
        BYTE* parent = *(BYTE**)(node + CONTROL_PARENT);
        if (parent == NULL)
        {
            *rootWidth = *(float*)(node + CONTROL_ABS_RECT + 8);
            *rootHeight = *(float*)(node + CONTROL_ABS_RECT + 12);
            return TRUE;
        }
        if ((*(DWORD*)(node + CONTROL_FLAGS) & FLAG_VISIBLE) == 0) return FALSE;
        node = parent;
    }
    return FALSE;
}

static void CollectBars(PanelState* state, float now)
{
    BYTE* spells = *(BYTE**)(*(BYTE**)(state->Panel + PANEL_GAME) + OBJ_SPELLS);
    DWORD handle = *(DWORD*)(state->Panel + PANEL_HANDLE);
    DWORD figure = (handle & 0xFF) == HANDLE_FIGURE ? handle >> 8 : 0;
    BOOL sameFigure = state->HaveBefore && state->Figure == figure;
    int side, i;

    for (side = 0; side < 2; side++)
    {
        for (i = 0; i < state->Shown[side]; i++)
        {
            WORD spell = state->Spell[side][i];
            BYTE* button = SideButton(state, side, i);
            BOOL started = sameFigure && !Contains(state->Before[side], state->BeforeCount[side], spell);
            float left, total;
            Bar* bar = &Bars[BarCount];
            if (!TimeLeft(spells, spell, now, started, &left, &total)) continue;
            if (BarCount >= MAX_BARS || button == NULL || !IsShown(button, &RootWidth, &RootHeight)) continue;
            bar->X = *(float*)(button + CONTROL_ABS_RECT);
            bar->Y = *(float*)(button + CONTROL_ABS_RECT + 4);
            bar->Width = *(float*)(button + CONTROL_ABS_RECT + 8);
            bar->Height = *(float*)(button + CONTROL_ABS_RECT + 12);
            bar->Share = total > 0.0f ? left / total : 0.0f;
            if (bar->Share > 1.0f) bar->Share = 1.0f;
            bar->Seconds = left / TicksPerSecond;
            bar->Negative = side;
            BarCount++;
        }
        if (sameFigure)
        {
            for (i = 0; i < state->BeforeCount[side]; i++)
            {
                if (!Contains(state->Spell[side], state->Shown[side], state->Before[side][i])) SpellEnded(state->Before[side][i]);
            }
        }
        memcpy(state->Before[side], state->Spell[side], sizeof(state->Before[side]));
        state->BeforeCount[side] = state->Shown[side];
    }
    state->Figure = figure;
    state->HaveBefore = TRUE;
}

static void EveryFrame(BYTE* main)
{
    float now = *(float*)(main + MAIN_NOW);
    int p;

    MeasureTickRate(now);
    BarCount = 0;
    for (p = 0; p < MAX_PANELS; p++)
    {
        PanelState* state = &Panels[p];
        if (state->Panel == NULL) continue;
        __try
        {
            if (!IsPanel(state->Panel))
            {
                state->Panel = NULL;
                continue;
            }
            LayOutPanel(state->Panel);
            if (*(DWORD*)(state->Panel + PANEL_HAS_HANDLE) == 1) CollectBars(state, now);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
    SnapshotTick = GetTickCount();
}

// Vtable +238h of CUiMain: the game's update, then the icons and the bars.
static void __fastcall MainUpdateHook(BYTE* main, void* unused, float a, float b)
{
    OriginalMainUpdate(main, NULL, a, b);
    EveryFrame(main);
}

/* ------------------------------------------------------------------------ */
/* Drawing                                                                   */
/* ------------------------------------------------------------------------ */

typedef HRESULT (WINAPI *EndSceneFn)(IDirect3DDevice9* device);
typedef HRESULT (WINAPI *CreateDeviceFn)(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
                                         D3DPRESENT_PARAMETERS* parameters, IDirect3DDevice9** device);
typedef IDirect3D9* (WINAPI *Direct3DCreate9Fn)(UINT version);
typedef FARPROC (WINAPI *GetProcAddressFn)(HMODULE module, LPCSTR name);

static EndSceneFn OriginalEndScene;
static CreateDeviceFn OriginalCreateDevice;
static Direct3DCreate9Fn OriginalDirect3DCreate9;
static GetProcAddressFn OriginalGetProcAddress;

typedef struct { float X, Y, Z, Rhw; DWORD Color; } ColorVertex;
typedef struct { float X, Y, Z, Rhw; DWORD Color; float U, V; } TextureVertex;

// Digits "0123456789." in one texture
#define GLYPH_COUNT   11
#define FONT_WIDTH    512
#define FONT_HEIGHT   64
static IDirect3DTexture9* FontTexture;
static IDirect3DDevice9* FontDevice;
static float GlyphLeft[GLYPH_COUNT], GlyphWidth[GLYPH_COUNT];
static int GlyphCellHeight;

static void WriteVtableSlot(void** slot, void* value)
{
    DWORD oldProtect;
    VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect);
    *slot = value;
    VirtualProtect(slot, sizeof(void*), oldProtect, &oldProtect);
}

static void WriteCode(BYTE* address, const void* bytes, SIZE_T size)
{
    DWORD oldProtect;
    VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(address, bytes, size);
    VirtualProtect(address, size, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), address, size);
}

// Renders the digits with GDI (white with a black outline) into a texture.
static void CreateFontTexture(IDirect3DDevice9* device)
{
    static const char glyphs[GLYPH_COUNT + 1] = "0123456789.";
    BITMAPINFO info;
    void* textBits;
    void* outlineBits;
    HDC dc = CreateCompatibleDC(NULL);
    HBITMAP textBitmap, outlineBitmap;
    HFONT font;
    D3DLOCKED_RECT locked;
    int i, x, y, dx, dy, cursor = 2;
    SIZE size;

    if (FontTexture != NULL)
    {
        IDirect3DTexture9_Release(FontTexture);
        FontTexture = NULL;
    }
    FontDevice = device;
    memset(&info, 0, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = FONT_WIDTH;
    info.bmiHeader.biHeight = -FONT_HEIGHT;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    textBitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &textBits, NULL, 0);
    outlineBitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &outlineBits, NULL, 0);
    font = CreateFontA(-44, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, "Arial");
    if (textBitmap == NULL || outlineBitmap == NULL || font == NULL) goto done;

    SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    GetTextExtentPoint32A(dc, "0", 1, &size);
    GlyphCellHeight = size.cy + 6;
    for (i = 0; i < GLYPH_COUNT; i++)
    {
        GetTextExtentPoint32A(dc, glyphs + i, 1, &size);
        GlyphLeft[i] = (float)cursor;
        GlyphWidth[i] = (float)(size.cx + 6);
        SelectObject(dc, outlineBitmap);
        for (dy = -3; dy <= 3; dy++)
        {
            for (dx = -3; dx <= 3; dx++)
            {
                if (dx * dx + dy * dy <= 10) TextOutA(dc, cursor + 3 + dx, 3 + dy, glyphs + i, 1);
            }
        }
        SelectObject(dc, textBitmap);
        TextOutA(dc, cursor + 3, 3, glyphs + i, 1);
        cursor += size.cx + 8;
    }
    GdiFlush();

    if (FAILED(IDirect3DDevice9_CreateTexture(device, FONT_WIDTH, FONT_HEIGHT, 1, 0, D3DFMT_A8R8G8B8,
                                              D3DPOOL_MANAGED, &FontTexture, NULL)))
    {
        FontTexture = NULL;
        goto done;
    }
    if (SUCCEEDED(IDirect3DTexture9_LockRect(FontTexture, 0, &locked, NULL, 0)))
    {
        for (y = 0; y < FONT_HEIGHT; y++)
        {
            DWORD* out = (DWORD*)((BYTE*)locked.pBits + y * locked.Pitch);
            for (x = 0; x < FONT_WIDTH; x++)
            {
                DWORD text = ((DWORD*)textBits)[y * FONT_WIDTH + x] & 0xFF;
                DWORD outline = ((DWORD*)outlineBits)[y * FONT_WIDTH + x] & 0xFF;
                DWORD alpha = text > outline ? text : outline;
                DWORD white = alpha ? text * 255 / alpha : 0;
                out[x] = (alpha << 24) | (white << 16) | (white << 8) | white;
            }
        }
        IDirect3DTexture9_UnlockRect(FontTexture, 0);
    }
done:
    if (font) DeleteObject(font);
    if (textBitmap) DeleteObject(textBitmap);
    if (outlineBitmap) DeleteObject(outlineBitmap);
    DeleteDC(dc);
}

static void DrawRectangle(IDirect3DDevice9* device, float x, float y, float w, float h, DWORD color)
{
    ColorVertex quad[4] =
    {
        { x, y, 0, 1, color }, { x + w, y, 0, 1, color }, { x + w, y + h, 0, 1, color }, { x, y + h, 0, 1, color },
    };
    IDirect3DDevice9_SetTexture(device, 0, NULL);
    IDirect3DDevice9_SetFVF(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    IDirect3DDevice9_DrawPrimitiveUP(device, D3DPT_TRIANGLEFAN, 2, quad, sizeof(ColorVertex));
}

static void DrawDigits(IDirect3DDevice9* device, float cx, float cy, float height, const char* text)
{
    TextureVertex quads[6 * 8];
    float scale = height / GlyphCellHeight, total = 0, x;
    int n = 0, i;

    if (FontTexture == NULL) return;
    for (i = 0; text[i] && i < 8; i++)
    {
        int g = text[i] == '.' ? 10 : text[i] - '0';
        total += GlyphWidth[g] * scale;
    }
    x = cx - total / 2;
    for (i = 0; text[i] && i < 8; i++)
    {
        int g = text[i] == '.' ? 10 : text[i] - '0';
        float w = GlyphWidth[g] * scale, top = cy - height / 2;
        float u0 = GlyphLeft[g] / FONT_WIDTH, u1 = (GlyphLeft[g] + GlyphWidth[g]) / FONT_WIDTH;
        float v1 = (float)GlyphCellHeight / FONT_HEIGHT;
        TextureVertex corners[4] =
        {
            { x, top, 0, 1, 0xFFFFFFFF, u0, 0 }, { x + w, top, 0, 1, 0xFFFFFFFF, u1, 0 },
            { x + w, top + height, 0, 1, 0xFFFFFFFF, u1, v1 }, { x, top + height, 0, 1, 0xFFFFFFFF, u0, v1 },
        };
        quads[n++] = corners[0]; quads[n++] = corners[1]; quads[n++] = corners[2];
        quads[n++] = corners[0]; quads[n++] = corners[2]; quads[n++] = corners[3];
        x += w;
    }
    IDirect3DDevice9_SetTexture(device, 0, (IDirect3DBaseTexture9*)FontTexture);
    IDirect3DDevice9_SetFVF(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    IDirect3DDevice9_DrawPrimitiveUP(device, D3DPT_TRIANGLELIST, n / 3, quads, sizeof(TextureVertex));
}

static void FormatSeconds(float seconds, char* text, size_t size)
{
    if (seconds >= 1.0f)
    {
        sprintf_s(text, size, "%d", (int)seconds);
    }
    else
    {
        int tenths = (int)(seconds * 10.0f);
        if (tenths < 1) tenths = 1;
        sprintf_s(text, size, "0.%d", tenths);
    }
}

static void DrawBars(IDirect3DDevice9* device)
{
    IDirect3DStateBlock9* saved = NULL;
    D3DVIEWPORT9 viewport;
    float sx, sy;
    int i, count = BarCount;

    if (count == 0 || GetTickCount() - SnapshotTick > SNAPSHOT_MAX_AGE_MS || RootWidth < 100.0f || RootHeight < 100.0f)
    {
        return;
    }
    if (FAILED(IDirect3DDevice9_GetViewport(device, &viewport))) return;
    if (FontDevice != device) CreateFontTexture(device);
    if (FAILED(IDirect3DDevice9_CreateStateBlock(device, D3DSBT_ALL, &saved))) return;

    sx = viewport.Width / RootWidth;
    sy = viewport.Height / RootHeight;
    IDirect3DDevice9_SetVertexShader(device, NULL);
    IDirect3DDevice9_SetPixelShader(device, NULL);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_FOGENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHATESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_STENCILENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SCISSORTESTENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ALPHABLENDENABLE, TRUE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    IDirect3DDevice9_SetRenderState(device, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    IDirect3DDevice9_SetRenderState(device, D3DRS_COLORWRITEENABLE, 0xF);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    IDirect3DDevice9_SetSamplerState(device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    for (i = 0; i < count && i < MAX_BARS; i++)
    {
        Bar bar = Bars[i];
        float x = viewport.X + bar.X * sx, y = viewport.Y + bar.Y * sy;
        float w = bar.Width * sx, h = bar.Height * sy;
        if (BarHeight > 0)
        {
            float bh = h * BarHeight / 100.0f;
            if (bh < 2.0f) bh = 2.0f;
            DrawRectangle(device, x, y + h - bh, w, bh, 0xC0000000);
            DrawRectangle(device, x, y + h - bh, w * bar.Share, bh, bar.Negative ? 0xFFE04040 : 0xFF40D040);
        }
        if (ShowSecondsBelow > 0 && bar.Seconds < (float)ShowSecondsBelow)
        {
            char text[16];
            FormatSeconds(bar.Seconds, text, sizeof(text));
            DrawDigits(device, x + w / 2, y + h / 2, h * 0.55f, text);
        }
    }
    IDirect3DStateBlock9_Apply(saved);
    IDirect3DStateBlock9_Release(saved);
}

static HRESULT WINAPI EndSceneHook(IDirect3DDevice9* device)
{
    __try
    {
        DrawBars(device);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return OriginalEndScene(device);
}

static volatile LONG EndSceneHooked;

// Length of the instruction at <code> when it is one of the usual first instructions of a function and does not
// depend on where it is (so it can run from a copy); 0 otherwise.
static int MovableInstructionLength(const BYTE* code)
{
    BYTE op = code[0];
    if (op >= 0x50 && op <= 0x57) return 1;                         // push reg
    if (op == 0x6A) return 2;                                       // push imm8
    if (op == 0x68) return 5;                                       // push imm32
    if (op >= 0xB8 && op <= 0xBF) return 5;                         // mov reg, imm32
    if ((op == 0x8B || op == 0x89 || op == 0x33 || op == 0x2B || op == 0x85) && code[1] >= 0xC0) return 2; // reg, reg
    if (op == 0x83 && (code[1] & 0xF8) == 0xE8) return 3;           // sub reg, imm8
    if (op == 0x81 && (code[1] & 0xF8) == 0xE8) return 6;           // sub reg, imm32
    if (op == 0x8B && (code[1] & 0xC7) == 0x45) return 3;           // mov reg, [ebp+imm8]
    return 0;
}

// Hooks EndScene of the device whose vtable is <vtable>, once. The function itself is patched (its first
// instructions move to a trampoline), so every device is covered - on some Windows every device has a vtable of
// its own. EndScene already patched by another mod (CooldownClock) starts with a jump: the hook goes before it.
// When the first instructions cannot be moved, only this vtable's slot is replaced.
static void HookEndScene(void** vtable, const char* how)
{
    BYTE* function = (BYTE*)vtable[D3D_END_SCENE];
    int length = 0, step;

    if (function == (BYTE*)EndSceneHook || InterlockedCompareExchange(&EndSceneHooked, 1, 0) != 0) return;
    if (function[0] == 0xE9)
    {
        BYTE jump[5] = { 0xE9 };
        OriginalEndScene = (EndSceneFn)(function + 5 + *(LONG*)(function + 1));
        *(LONG*)(jump + 1) = (LONG)((BYTE*)EndSceneHook - (function + 5));
        WriteCode(function, jump, sizeof(jump));
        Log("EndScene hooked in d3d9 at %p, before the jump another mod put there (%s)", function, how);
        return;
    }
    while (length < 5 && (step = MovableInstructionLength(function + length)) > 0) length += step;
    if (length >= 5)
    {
        BYTE* trampoline = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (trampoline != NULL)
        {
            BYTE jump[5] = { 0xE9 };
            memcpy(trampoline, function, length);
            trampoline[length] = 0xE9;
            *(LONG*)(trampoline + length + 1) = (LONG)((function + length) - (trampoline + length + 5));
            OriginalEndScene = (EndSceneFn)trampoline;
            *(LONG*)(jump + 1) = (LONG)((BYTE*)EndSceneHook - (function + 5));
            WriteCode(function, jump, sizeof(jump));
            Log("EndScene hooked in d3d9 at %p, %d bytes moved (%s)", function, length, how);
            return;
        }
    }
    Log("EndScene at %p starts with %02X %02X %02X %02X %02X %02X %02X, which cannot be moved; only the %s is hooked",
        function, function[0], function[1], function[2], function[3], function[4], function[5], function[6], how);
    OriginalEndScene = (EndSceneFn)function;
    WriteVtableSlot(&vtable[D3D_END_SCENE], (void*)EndSceneHook);
}

static HRESULT WINAPI CreateDeviceHook(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND window, DWORD flags,
                                       D3DPRESENT_PARAMETERS* parameters, IDirect3DDevice9** device)
{
    HRESULT result = OriginalCreateDevice(d3d, adapter, type, window, flags, parameters, device);
    if (SUCCEEDED(result) && device != NULL && *device != NULL)
    {
        HookEndScene(*(void***)*device, "game device");
    }
    return result;
}

// Direct3D was already loaded when the mod was: a small device of the mod's own, on a hidden window, shows where
// EndScene is, and the hook then also covers the game's device.
static DWORD WINAPI HookExistingDirect3D(LPVOID unused)
{
    WNDCLASSA windowClass;
    HWND window;
    HMODULE d3d9;
    Direct3DCreate9Fn create;
    IDirect3D9* d3d;
    IDirect3DDevice9* device = NULL;
    D3DPRESENT_PARAMETERS parameters;
    int i;

    for (i = 0; i < 600 && (d3d9 = GetModuleHandleA("d3d9.dll")) == NULL; i++) Sleep(100);
    if (d3d9 == NULL || EndSceneHooked) return 0;
    create = (Direct3DCreate9Fn)GetProcAddress(d3d9, "Direct3DCreate9");
    if (create == NULL || (d3d = create(D3D_SDK_VERSION)) == NULL)
    {
        Log("Direct3DCreate9 failed, no drawing");
        return 0;
    }
    memset(&windowClass, 0, sizeof(windowClass));
    windowClass.lpfnWndProc = DefWindowProcA;
    windowClass.hInstance = ModuleInstance;
    windowClass.lpszClassName = "BetterIconsProbe";
    RegisterClassA(&windowClass);
    window = CreateWindowA("BetterIconsProbe", "", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, ModuleInstance, NULL);
    memset(&parameters, 0, sizeof(parameters));
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.BackBufferWidth = 64;
    parameters.BackBufferHeight = 64;
    parameters.BackBufferFormat = D3DFMT_UNKNOWN;
    parameters.hDeviceWindow = window;
    if (FAILED(IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                       D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, &device)) &&
        FAILED(IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF, window,
                                       D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, &device)))
    {
        device = NULL;
        Log("no Direct3D device could be made, no drawing");
    }
    if (device != NULL)
    {
        HookEndScene(*(void***)device, "own device");
        IDirect3DDevice9_Release(device);
    }
    IDirect3D9_Release(d3d);
    if (window != NULL) DestroyWindow(window);
    UnregisterClassA("BetterIconsProbe", ModuleInstance);
    return 0;
}

static IDirect3D9* WINAPI Direct3DCreate9Hook(UINT version)
{
    IDirect3D9* d3d = OriginalDirect3DCreate9(version);
    if (d3d != NULL)
    {
        void** vtable = *(void***)d3d;
        if (vtable[D3D_CREATE_DEVICE] != (void*)CreateDeviceHook)
        {
            OriginalCreateDevice = (CreateDeviceFn)vtable[D3D_CREATE_DEVICE];
            WriteVtableSlot(&vtable[D3D_CREATE_DEVICE], (void*)CreateDeviceHook);
        }
    }
    return d3d;
}

static FARPROC WINAPI GetProcAddressHook(HMODULE module, LPCSTR name)
{
    FARPROC result = OriginalGetProcAddress(module, name);
    if (result != NULL && ((DWORD_PTR)name >> 16) != 0 && strcmp(name, "Direct3DCreate9") == 0)
    {
        OriginalDirect3DCreate9 = (Direct3DCreate9Fn)result;
        return (FARPROC)Direct3DCreate9Hook;
    }
    return result;
}

/* ------------------------------------------------------------------------ */
/* Patching                                                                  */
/* ------------------------------------------------------------------------ */

static const BYTE UpdateIconsPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF }; // push ebp; mov ebp,esp; push -1

static void ApplyPatch(HINSTANCE instance)
{
    BYTE* base = (BYTE*)GetModuleHandleA(NULL);
    BYTE* update;
    BYTE* trampoline;
    BYTE jump[5] = { 0xE9 };
    void** destructorSlot;
    void** mainSlot;
    void** iatSlot;

    ModuleInstance = instance;
    ReadConfig();
    if (!IsGameImageLargeEnough(base) || *(int*)(base + VERSION_RVA) != VERSION_VALUE)
    {
        Log("unsupported SpellForce.exe build, nothing patched");
        return;
    }
    update = base + UPDATE_ICONS_RVA;
    destructorSlot = (void**)(base + PANEL_VTABLE_RVA);
    if (memcmp(update, UpdateIconsPrologue, sizeof(UpdateIconsPrologue)) != 0 ||
        *destructorSlot != (void*)(base + PANEL_DESTRUCTOR_RVA))
    {
        Log("game code not as expected (another mod?), nothing patched");
        return;
    }
    trampoline = (BYTE*)VirtualAlloc(NULL, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (trampoline == NULL) return;
    GameBase = base;
    SetUpGame(base);
    LoadLearned();

    // The icons: after the game's own work, and when a panel goes
    memcpy(trampoline, update, UPDATE_ICONS_MOVED);
    trampoline[UPDATE_ICONS_MOVED] = 0xE9;
    *(LONG*)(trampoline + UPDATE_ICONS_MOVED + 1) = (LONG)((update + UPDATE_ICONS_MOVED) - (trampoline + UPDATE_ICONS_MOVED + 5));
    OriginalUpdateIcons = (UpdateIconsFn)trampoline;
    *(LONG*)(jump + 1) = (LONG)((BYTE*)UpdateIconsHook - (update + 5));
    WriteCode(update, jump, sizeof(jump));
    OriginalDestructor = (DestructorFn)*destructorSlot;
    WriteVtableSlot(destructorSlot, (void*)DestructorHook);

    // Every frame (another mod may have replaced the UI update already: it is called in turn) and EndScene
    mainSlot = (void**)(base + MAIN_VTABLE_RVA + MAIN_VT_UPDATE);
    iatSlot = (void**)(base + GET_PROC_ADDRESS_IAT);
    OriginalMainUpdate = (MainUpdateFn)*mainSlot;
    OriginalGetProcAddress = (GetProcAddressFn)*iatSlot;
    WriteVtableSlot(mainSlot, (void*)MainUpdateHook);
    WriteVtableSlot(iatSlot, (void*)GetProcAddressHook);
    if (GetModuleHandleA("d3d9.dll") != NULL)
    {
        HANDLE thread = CreateThread(NULL, 0, HookExistingDirect3D, NULL, 0, NULL);
        if (thread != NULL) CloseHandle(thread);
        Log("patched (Direct3D already loaded)");
    }
    else
    {
        Log("patched");
    }
}
