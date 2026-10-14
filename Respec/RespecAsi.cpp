/*
 * Respec.asi - the Respec mod (../respec/respec.cpp, unchanged) as an ASI plugin, without the Spellforce Spell
 * Framework.
 *
 * respec.cpp uses a few SFSF functions through the SpellforceSpellFramework it gets in InitModule. This file
 * builds that structure itself with only those members filled in, pointing at the same game functions SFSF uses
 * (src/internal/core/sf_hooks.c) or at copies of SFSF's own helpers:
 *   figureAPI->getSpellJobStartNode, toolboxAPI->getSpellIndexFromDLL / getNextNode, uiAPI->setButtonName:
 *       game functions (SFSF DEFINE_FUNCTION addresses);
 *   uiAPI->attachNewButton: copy of SFSF's attachNewButton (src/internal/core/sf_ui_wrappers.c);
 *   spellAPI->hasSpellTag: SFSF answers from its spell registry; here a table of the vanilla spell lines SFSF
 *       tags as auras (src/internal/registry/sf_vanilla_registry.cpp);
 *   logAPI: writes Respec.log next to the .asi.
 * Every other member stays NULL; respec.cpp does not use them.
 *
 * Build with 32-bit MinGW g++ (the SFSF headers need GCC's packed structures): see build.bat.
 */

#define InitModule RespecInitModule
#define RegisterMod RespecRegisterMod
#define DllMain RespecDllMain
#include "../respec/respec.cpp"
#undef InitModule
#undef RegisterMod
#undef DllMain

#include <stdarg.h>

#define VERSION_RVA   0x177e63 // Int32 11213 in build 1.61.11213
#define VERSION_VALUE 11213

/* ------------------------------------------------------------------------ */
/* Log                                                                       */
/* ------------------------------------------------------------------------ */

static char s_log_path[MAX_PATH];

static void write_log(const char *level, const char *format, va_list args)
{
    FILE *file = s_log_path[0] ? fopen(s_log_path, "a") : NULL;
    if (!file)
    {
        return;
    }
    fprintf(file, "%s ", level);
    vfprintf(file, format, args);
    fputc('\n', file);
    fclose(file);
}

static void log_error(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    write_log("[error]", format, args);
    va_end(args);
}

static void log_warning(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    write_log("[warning]", format, args);
    va_end(args);
}

static void log_info(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    write_log("[info]", format, args);
    va_end(args);
}

static void log_debug(DebugLevel level, const char *format, ...)
{
}

/* ------------------------------------------------------------------------ */
/* Spell tags                                                                */
/* ------------------------------------------------------------------------ */

// Vanilla spell lines SFSF tags as AURA_SPELL, with their WHITE/BLACK tag
static const struct
{
    uint16_t line;
    uint16_t tags;
} s_aura_lines[] = {
    {88, SpellTag::AURA_SPELL | SpellTag::BLACK_AURA_SPELL},  // Weakness
    {89, SpellTag::AURA_SPELL | SpellTag::BLACK_AURA_SPELL},  // Suffocation
    {91, SpellTag::AURA_SPELL | SpellTag::BLACK_AURA_SPELL},  // Life Tap
    {94, SpellTag::AURA_SPELL | SpellTag::BLACK_AURA_SPELL},  // Slow Fighting
    {95, SpellTag::AURA_SPELL | SpellTag::BLACK_AURA_SPELL},  // Inflexibility
    {97, SpellTag::AURA_SPELL | SpellTag::BLACK_AURA_SPELL},  // Slow Walking
    {98, SpellTag::AURA_SPELL | SpellTag::BLACK_AURA_SPELL},  // Inability
    {102, SpellTag::AURA_SPELL | SpellTag::WHITE_AURA_SPELL}, // Strength
    {103, SpellTag::AURA_SPELL | SpellTag::WHITE_AURA_SPELL}, // Healing
    {104, SpellTag::AURA_SPELL | SpellTag::WHITE_AURA_SPELL}, // Endurance
    {107, SpellTag::AURA_SPELL | SpellTag::WHITE_AURA_SPELL}, // Regeneration
    {110, SpellTag::AURA_SPELL | SpellTag::WHITE_AURA_SPELL}, // Fast Fighting
    {111, SpellTag::AURA_SPELL | SpellTag::WHITE_AURA_SPELL}, // Flexibility
    {113, SpellTag::AURA_SPELL | SpellTag::WHITE_AURA_SPELL}, // Fast Walking
    {114, SpellTag::AURA_SPELL | SpellTag::WHITE_AURA_SPELL}, // Light
    {115, SpellTag::AURA_SPELL | SpellTag::WHITE_AURA_SPELL}, // Dexterity
    {127, SpellTag::AURA_SPELL},                              // Hypnotization
    {129, SpellTag::AURA_SPELL},                              // Brilliance
    {131, SpellTag::AURA_SPELL},                              // Mana Tap
    {192, SpellTag::AURA_SPELL | SpellTag::WHITE_AURA_SPELL}, // Eternity
};

static bool __thiscall has_spell_tag(uint16_t spell_line, SpellTag tag)
{
    for (size_t i = 0; i < sizeof(s_aura_lines) / sizeof(s_aura_lines[0]); i++)
    {
        if (s_aura_lines[i].line == spell_line)
        {
            return (s_aura_lines[i].tags & tag) != 0;
        }
    }
    return false;
}

/* ------------------------------------------------------------------------ */
/* UI                                                                        */
/* ------------------------------------------------------------------------ */

static UiFunctions s_ui;
static string_ctor_ptr s_create_string;
static string_dtor_ptr s_destroy_string;

// SFSF's attachNewButton (src/internal/core/sf_ui_wrappers.c)
static CMnuSmpButton *__thiscall respec_attach_new_button(CMnuContainer *parent, char *button_mesh_default,
                                                   char *button_mesh_pressed, char *button_initial_load_mesh,
                                                   char *button_mesh_disabled, char *label_char, uint8_t font_index,
                                                   uint16_t x_pos, uint16_t y_pos, uint16_t width, uint16_t height,
                                                   int button_index, uint32_t callback_func_ptr)
{
    SF_String m_mesh_string_default;
    SF_String m_mesh_string_pressed;
    SF_String m_button_initial_load_mesh;
    SF_String m_mesh_string_disabled;
    SF_String m_label_string;

    SF_FontStruct *fonts = s_ui.getFonts();
    SF_String *label_string = s_create_string(&m_label_string, label_char);
    SF_String *mesh_string_default = s_create_string(&m_mesh_string_default, button_mesh_default);
    SF_String *mesh_string_pressed = s_create_string(&m_mesh_string_pressed, button_mesh_pressed);
    SF_String *init_load_mesh = s_create_string(&m_button_initial_load_mesh, button_initial_load_mesh);
    SF_String *mesh_string_disabled = s_create_string(&m_mesh_string_disabled, button_mesh_disabled);

    CMnuSmpButton *new_button = (CMnuSmpButton *)s_ui.newOperator(0x428);
    if (font_index > 32)
    {
        log_error("[respec] invalid font index %u, using font 6", font_index);
        font_index = 6;
    }
    new_button = s_ui.initializeSmpButton(new_button);
    SF_Font *selected_font = s_ui.getFont(fonts, font_index);
    s_ui.CMnuBaseSetName((CMnuBase *)new_button, label_string);
    s_ui.createButton(new_button, x_pos, y_pos, width, height, mesh_string_default, init_load_mesh,
                      mesh_string_pressed, mesh_string_disabled);
    s_ui.setFont(new_button, selected_font);
    s_ui.setButtonIndex(new_button, button_index);
    s_ui.setMenuButtonFlag(new_button, '\x01');
    s_ui.setButtonName(new_button, label_string);

    CUtlCallback2 callback;
    callback.vtable_ptr = *(uint32_t *)addr_of(0x7f9c64);
    callback.param_ptr = (uint32_t)parent;
    callback.callback_func = callback_func_ptr;
    uint32_t param1, param2, param3;
    s_ui.attachCallback(&callback, &param1, &param2, &param3);
    new_button->CMnuBase_data.param_1_callback = param1;
    new_button->CMnuBase_data.param_2_callback = param2;
    new_button->CMnuBase_data.param_3_callback = param3;

    s_ui.vfunction16AttachCallback(new_button, '\x01');
    s_ui.containerAddControl(parent, (CMnuBase *)new_button, '\x01', '\x01', 0);

    s_destroy_string(mesh_string_default);
    s_destroy_string(mesh_string_pressed);
    s_destroy_string(init_load_mesh);
    s_destroy_string(mesh_string_disabled);
    s_destroy_string(label_string);
    return new_button;
}

/* ------------------------------------------------------------------------ */
/* Framework stand-in                                                        */
/* ------------------------------------------------------------------------ */

static SFLog s_log;
static SpellFunctions s_spell;
static FigureFunctions s_figure;
static ToolboxFunctions s_toolbox;
static SpellforceSpellFramework s_framework;

#define GAME_FUNCTION(api, name, rva) api.name = (name##_ptr)addr_of(rva)

static void build_framework()
{
    s_log.logError = log_error;
    s_log.logWarning = log_warning;
    s_log.logInfo = log_info;
    s_log.logDebug = log_debug;

    // Addresses from SFSF src/internal/core/sf_hooks.c
    GAME_FUNCTION(s_figure, getSpellJobStartNode, 0x2b2de0);
    GAME_FUNCTION(s_toolbox, getSpellIndexFromDLL, 0x2fdd90);
    GAME_FUNCTION(s_toolbox, getNextNode, 0x2fe240);
    GAME_FUNCTION(s_ui, setButtonName, 0x52f8a0);
    GAME_FUNCTION(s_ui, getFonts, 0x5357b0);
    GAME_FUNCTION(s_ui, getFont, 0x535180);
    GAME_FUNCTION(s_ui, newOperator, 0x675a9d);
    GAME_FUNCTION(s_ui, initializeSmpButton, 0x51a9d0);
    GAME_FUNCTION(s_ui, CMnuBaseSetName, 0x512e30);
    GAME_FUNCTION(s_ui, createButton, 0x52e1e0);
    GAME_FUNCTION(s_ui, setFont, 0x530c20);
    GAME_FUNCTION(s_ui, setButtonIndex, 0x5136a0);
    GAME_FUNCTION(s_ui, setMenuButtonFlag, 0x5308a0);
    GAME_FUNCTION(s_ui, attachCallback, 0x6188b0);
    GAME_FUNCTION(s_ui, vfunction16AttachCallback, 0x532b90);
    GAME_FUNCTION(s_ui, containerAddControl, 0x506f30);
    s_create_string = (string_ctor_ptr)addr_of(RVA_SF_STRING_CTOR);
    s_destroy_string = (string_dtor_ptr)addr_of(RVA_SF_STRING_DTOR);
    s_ui.attachNewButton = (attachNewButton_ptr)&respec_attach_new_button;
    s_spell.hasSpellTag = (hasSpellTag_ptr)&has_spell_tag;

    s_framework.spellAPI = &s_spell;
    s_framework.uiAPI = &s_ui;
    s_framework.figureAPI = &s_figure;
    s_framework.toolboxAPI = &s_toolbox;
    s_framework.logAPI = &s_log;
}

// True when the game image is large enough for the addresses below, so loading the .asi into another program
// does nothing instead of crashing it.
static bool is_game_image(uintptr_t base)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    return dos->e_magic == IMAGE_DOS_SIGNATURE && nt->OptionalHeader.SizeOfImage > 0x400000;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason != DLL_PROCESS_ATTACH)
    {
        return TRUE;
    }
    DisableThreadLibraryCalls(instance);
    if (GetModuleFileNameA(instance, s_log_path, MAX_PATH))
    {
        char *slash = strrchr(s_log_path, '\\');
        if (slash)
        {
            strcpy(slash + 1, "Respec.log");
        }
        else
        {
            s_log_path[0] = '\0';
        }
    }

    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    if (!is_game_image(base) || *(int *)(base + VERSION_RVA) != VERSION_VALUE)
    {
        log_error("[respec] unsupported SpellForce.exe build, nothing patched");
        return TRUE;
    }
    s_base = base;
    build_framework();
    RespecInitModule(&s_framework);
    return TRUE;
}
