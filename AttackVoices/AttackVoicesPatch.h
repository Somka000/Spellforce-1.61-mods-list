#pragma once
// AttackVoices - combat units answer an attack order on an enemy unit with their own voice lines,
// SpellForce 1.61.11213. The avatar keeps its own answer.
//
// Unit voice files are sound\speech\messages\a_<message>_<race>_<variant>_<category>_<nn>.mp3 (sf10.pak).
// Variant 0 = combat units, variants 1/2 = the avatar's male/female voice.
// The attack orders (CUiMouseBehaviourFigureAttack / FigureAttackForced) let one figure of the selection answer
// with message 106. Only the avatar has 0106 files, so when no avatar is selected the combat units stay silent.
// Combat units do have 0103 lines, but the game plays message 103 only for gathering orders (cut tree, crush stone,
// collect iron), which combat units cannot carry out.
//
// The patch (VAs with the default image base):
//   * 00A3A0A9 / 00A3A309: the "call 005C07C0" that creates the attack answer is redirected to AttackAnswerHook,
//     which marks "an attack order on a unit is answering" and calls the original.
//   * 005C10D0 (builds the file name from message, category, variant, race; thiscall, ret 10h): while that mark is
//     set, message 106 with variant 0 (a combat unit) becomes 103. The category is still chosen by the game
//     (4 = combat unit, 7 = titan). The avatar (variants 1/2) and every other message are unchanged.
//
// All voices all in pak10

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#define VERSION_RVA   0x177E63 // Int32 11213 in build 1.61.11213
#define VERSION_VALUE 11213

#define SPEECH_ID_CTOR_RVA  0x1C10D0 // builds "%04d_%02d_%01d_%01d" from (message, category, variant, race)
#define MESSAGE_CTOR_RVA    0x1C07C0 // creates a voice message for a figure (thiscall, 2 arguments, ret 8)
#define MESSAGE_ATTACK_ORDER  106    // the game's answer to an attack order (avatar files only)
#define MESSAGE_COMBAT_ATTACK 103    // combat units' attack lines
#define VARIANT_COMBAT_UNIT   0

static const DWORD AttackAnswerCalls[] =
{
    0x63A0A9, // CUiMouseBehaviourFigureAttack
    0x63A309, // CUiMouseBehaviourFigureAttackForced
};

static const BYTE SpeechIdPrologue[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF }; // push ebp ; mov ebp, esp ; push -1

static DWORD SpeechIdContinue;     // speech id constructor + 5
static DWORD MessageCtor;          // 005C07C0
static volatile LONG AnsweringAttack; // set while an attack order on a unit creates its answer

// Replaces "call 005C07C0" at the attack orders: ecx = message, [esp+4], [esp+8] = arguments.
static __declspec(naked) void AttackAnswerHook(void)
{
    __asm
    {
        mov dword ptr [AnsweringAttack], 1
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        call dword ptr [MessageCtor]
        mov dword ptr [AnsweringAttack], 0
        ret 8
    }
}

// Entered instead of the speech id constructor:
// [esp+4] = message, [esp+8] = category, [esp+0Ch] = variant, [esp+10h] = race (ecx = this, untouched).
static __declspec(naked) void SpeechIdHook(void)
{
    __asm
    {
        cmp dword ptr [AnsweringAttack], 0
        je original
        cmp dword ptr [esp + 4], MESSAGE_ATTACK_ORDER
        jne original
        cmp dword ptr [esp + 0Ch], VARIANT_COMBAT_UNIT
        jne original
        mov dword ptr [esp + 4], MESSAGE_COMBAT_ATTACK
    original:
        push ebp
        mov ebp, esp
        push -1
        jmp dword ptr [SpeechIdContinue]
    }
}

// True when the main module is large enough to hold the addresses this patch reads, so loading the mod into
// another program skips the patch instead of crashing it.
static BOOL IsGameImageLargeEnough(BYTE* base)
{
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)base;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
    return dos->e_magic == IMAGE_DOS_SIGNATURE && nt->OptionalHeader.SizeOfImage > 0x700000;
}

static BYTE* CallTarget(BYTE* call)
{
    return call + 5 + *(LONG*)(call + 1);
}

static void WriteRelative(BYTE* site, BYTE opcode, void* target)
{
    BYTE code[5];
    LONG offset = (LONG)((BYTE*)target - (site + sizeof(code)));
    DWORD oldProtect;
    code[0] = opcode;
    memcpy(code + 1, &offset, sizeof(offset));
    VirtualProtect(site, sizeof(code), PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(site, code, sizeof(code));
    VirtualProtect(site, sizeof(code), oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, sizeof(code));
}

static void ApplyPatchTo(BYTE* base)
{
    BYTE* speechId = base + SPEECH_ID_CTOR_RVA;
    int i;

    if (!IsGameImageLargeEnough(base) || *(int*)(base + VERSION_RVA) != VERSION_VALUE ||
        memcmp(speechId, SpeechIdPrologue, sizeof(SpeechIdPrologue)) != 0)
    {
        OutputDebugStringA("AttackVoices: unsupported SpellForce.exe build or code already changed, nothing patched.\n");
        return;
    }
    for (i = 0; i < sizeof(AttackAnswerCalls) / sizeof(AttackAnswerCalls[0]); i++)
    {
        BYTE* call = base + AttackAnswerCalls[i];
        if (call[0] != 0xE8 || CallTarget(call) != base + MESSAGE_CTOR_RVA)
        {
            OutputDebugStringA("AttackVoices: unexpected code, nothing patched.\n");
            return;
        }
    }

    MessageCtor = (DWORD)(base + MESSAGE_CTOR_RVA);
    SpeechIdContinue = (DWORD)(speechId + sizeof(SpeechIdPrologue));
    WriteRelative(speechId, 0xE9, SpeechIdHook);
    for (i = 0; i < sizeof(AttackAnswerCalls) / sizeof(AttackAnswerCalls[0]); i++)
    {
        WriteRelative(base + AttackAnswerCalls[i], 0xE8, AttackAnswerHook);
    }

    OutputDebugStringA("AttackVoices: combat units answer attack orders.\n");
}

