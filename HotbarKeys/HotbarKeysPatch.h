#pragma once
// HotbarKeys - keyboard keys for the 10 slots of the spell hotbar on the bottom bar in SpellForce 1.61.11213.
// By default F2..F6 use slots 1..5; slots 6..10 have no key. Keys are read from HotbarKeys.txt next to the
// .asi/.sfm file; the file is created with default values when missing.
//
// How the game handles a click on a hotbar slot (addresses with the default image base):
//   CUiMain (vtable 00C06ADC, constructor 009DD1C0, destructor 009DE3C0) keeps the hotbar at [this+774Ch].
//   The hotbar is a CUiBottom (vtable 00C05A68) with ten buttons "btnAction0..9" at [bar+39Ch+4*i]
//   (button vtable 00BE7CCC). A left click on button i calls CUiBottom::OnActionButton (bar vtable +334h,
//   009CCFF0) with (button, mouse event). That marks the slot and calls CUiMain::OnSpellSlot (vtable +344h,
//   009ED880), which reads only the mouse button from the event (+20h, 0 = left), takes the slot record
//   [CUiMain+7558h] + 32 * slot and starts the spell (009E63E0), the same as clicking with the mouse. It only
//   accepts the click when the button has the "pressed by the mouse" flag (100000h at +128h), which the mouse
//   down sets; the patch sets that flag for the call.
//
// The patch hooks the CUiMain constructor and destructor to know the live CUiMain. A background thread watches
// the keys while the game window is in front and posts a message to the game window; the window procedure
// (subclassed by the patch) then calls CUiBottom::OnActionButton for that slot on the game's own thread, with
// a left-click event. The mouse is not touched. Nothing happens while the hotbar or the slot is hidden.

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

#define MAIN_CTOR_RVA      0x5DD1C0
#define MAIN_DTOR_RVA      0x5DE3C0
#define MAIN_VTABLE_RVA    0x806ADC
#define BOTTOM_VTABLE_RVA  0x805A68
#define BUTTON_VTABLE_RVA  0x7E7CCC

#define MAIN_HOTBAR        0x774C
#define BOTTOM_BUTTONS     0x39C
#define BOTTOM_ON_ACTION   0x334 // vtable offset of CUiBottom::OnActionButton(button, event)
#define CONTROL_FLAGS      0x128
#define CONTROL_PARENT     0x12C
#define FLAG_VISIBLE       2
#define FLAG_MOUSE_PRESSED 0x100000 // set while a mouse button went down on the control (vtable +E8h reads it)
#define EVENT_MOUSE_BUTTON 0x20

#define SLOT_COUNT         10
#define MAX_PARENT_DEPTH   32

static const BYTE CtorOriginal[] = { 0x55, 0x8B, 0xEC, 0x6A, 0xFF };       // push ebp; mov ebp,esp; push -1
static const BYTE DtorOriginal[] = { 0x55, 0x8B, 0xEC, 0x56, 0x8B, 0xF1 }; // push ebp; mov ebp,esp; push esi; mov esi,ecx

static const char DefaultConfig[] =
    "# HotbarKeys (.asi or .sfm) - keys that use the 10 slots of the spell hotbar on the bottom bar.\r\n"
    "# Slot1 is the leftmost slot. Leave a slot empty for no key.\r\n"
    "# Key names: F1-F24, A-Z, 0-9, NUM0-NUM9, or a decimal Windows virtual-key code.\r\n"
    "Slot1=F2\r\n"
    "Slot2=F3\r\n"
    "Slot3=F4\r\n"
    "Slot4=F5\r\n"
    "Slot5=F6\r\n"
    "Slot6=\r\n"
    "Slot7=\r\n"
    "Slot8=\r\n"
    "Slot9=\r\n"
    "Slot10=\r\n"
    "# Log=1 writes HotbarKeys.log next to this file (useful when a key does nothing).\r\n"
    "Log=0\r\n";

static BYTE* GameBase;
static HINSTANCE ModuleInstance;
static int SlotKeys[SLOT_COUNT];
static BOOL LogEnabled;
static char LogPath[MAX_PATH];

// Written by the game thread in the hooks, read on the game thread when a key is used.
static BYTE* volatile LiveMain;
static DWORD CtorResume;
static DWORD DtorResume;

static HWND GameWindow;
static WNDPROC GameWindowProc;
static UINT UseSlotMessage;

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

// CUiMain constructor - remember the object, then run the original prologue.
__declspec(naked) static void MainCtorHook(void)
{
    __asm
    {
        mov     LiveMain, ecx
        push    ebp
        mov     ebp, esp
        push    0FFFFFFFFh
        jmp     dword ptr [CtorResume]
    }
}

// CUiMain scalar deleting destructor - forget the object, then run the original prologue.
__declspec(naked) static void MainDtorHook(void)
{
    __asm
    {
        cmp     ecx, LiveMain
        jne     keep
        mov     LiveMain, 0
    keep:
        push    ebp
        mov     ebp, esp
        push    esi
        mov     esi, ecx
        jmp     dword ptr [DtorResume]
    }
}

static int ParseKey(const char* name)
{
    while (*name == ' ' || *name == '\t') name++;
    if (*name == '\0') return 0;
    if ((name[0] == 'F' || name[0] == 'f') && name[1] >= '1' && name[1] <= '9')
    {
        int n = atoi(name + 1);
        return n >= 1 && n <= 24 ? VK_F1 + n - 1 : 0;
    }
    if (_strnicmp(name, "NUM", 3) == 0 && name[3] >= '0' && name[3] <= '9') return VK_NUMPAD0 + name[3] - '0';
    if (name[1] == '\0' || name[1] == ' ' || name[1] == '\t')
    {
        char c = name[0];
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    }
    if (name[0] >= '0' && name[0] <= '9')
    {
        int vk = atoi(name);
        return vk > 0 && vk < 256 ? vk : 0;
    }
    return 0;
}

static void TrimLine(char* line)
{
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == '\n' || line[n - 1] == ' ' || line[n - 1] == '\t'))
    {
        line[--n] = '\0';
    }
}

static void ReadConfig(void)
{
    char path[MAX_PATH];
    char line[256];
    char* slash;
    FILE* file;
    int i;

    for (i = 0; i < SLOT_COUNT; i++) SlotKeys[i] = i < 5 ? VK_F2 + i : 0;

    if (GetModuleFileNameA(ModuleInstance, path, MAX_PATH) == 0) return;
    slash = strrchr(path, '\\');
    if (slash == NULL) return;
    strcpy_s(LogPath, MAX_PATH, path);
    strcpy_s(LogPath + (slash + 1 - path), MAX_PATH - (slash + 1 - path), "HotbarKeys.log");
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "HotbarKeys.txt");

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
        TrimLine(value);
        if (_strnicmp(line, "Slot", 4) == 0)
        {
            int slot = atoi(line + 4);
            if (slot >= 1 && slot <= SLOT_COUNT) SlotKeys[slot - 1] = ParseKey(value);
        }
        else if (_stricmp(line, "Log") == 0) LogEnabled = atoi(value) != 0;
    }
    fclose(file);
}

// Finds the hotbar and the button of <slot>. Fails when there is no game UI, the hotbar or the button (or one
// of its parents) is hidden, or the memory does not look like the expected objects.
static BOOL FindSlotButton(int slot, BYTE** bar, BYTE** button)
{
    BOOL ok = FALSE;
    __try
    {
        BYTE* main = LiveMain;
        BYTE* node;
        int depth;

        if (main == NULL || *(DWORD*)main != (DWORD)(GameBase + MAIN_VTABLE_RVA))
        {
            Log("slot %d: no game UI", slot + 1);
            return FALSE;
        }
        *bar = *(BYTE**)(main + MAIN_HOTBAR);
        if (*bar == NULL || *(DWORD*)*bar != (DWORD)(GameBase + BOTTOM_VTABLE_RVA))
        {
            Log("slot %d: no hotbar", slot + 1);
            return FALSE;
        }
        *button = *(BYTE**)(*bar + BOTTOM_BUTTONS + 4 * slot);
        if (*button == NULL || *(DWORD*)*button != (DWORD)(GameBase + BUTTON_VTABLE_RVA))
        {
            Log("slot %d: no button", slot + 1);
            return FALSE;
        }
        node = *button;
        for (depth = 0; depth < MAX_PARENT_DEPTH; depth++)
        {
            BYTE* parent = *(BYTE**)(node + CONTROL_PARENT);
            if (parent == NULL)
            {
                ok = TRUE;
                break;
            }
            if ((*(DWORD*)(node + CONTROL_FLAGS) & FLAG_VISIBLE) == 0)
            {
                Log("slot %d: hidden (level %d, flags %08X)", slot + 1, depth, *(DWORD*)(node + CONTROL_FLAGS));
                break;
            }
            node = parent;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("slot %d: game UI memory not readable", slot + 1);
        ok = FALSE;
    }
    return ok;
}

typedef void (__fastcall *OnActionButtonFn)(BYTE* bar, void* unused, BYTE* button, BYTE* event);

// Runs on the game thread (from the window procedure): does what a left click on the slot does.
static void UseSlot(int slot)
{
    // The handlers read only the mouse button (+20h) of the event; the rest stays zero.
    static BYTE leftClick[0x100];
    BYTE* bar;
    BYTE* button;
    OnActionButtonFn onAction;

    DWORD* flags;
    DWORD pressedBefore;

    if (!FindSlotButton(slot, &bar, &button)) return;
    *(DWORD*)(leftClick + EVENT_MOUSE_BUTTON) = 0;
    onAction = *(OnActionButtonFn*)(*(BYTE**)bar + BOTTOM_ON_ACTION);
    flags = (DWORD*)(button + CONTROL_FLAGS);
    Log("slot %d: bar %p button %p flags %08X", slot + 1, bar, button, *flags);
    // CUiMain::OnSpellSlot ignores the click unless the button is marked as pressed by the mouse (set on mouse
    // down, checked on mouse up), so mark it for the call and put the bit back afterwards.
    pressedBefore = *flags & FLAG_MOUSE_PRESSED;
    *flags |= FLAG_MOUSE_PRESSED;
    onAction(bar, NULL, button, leftClick);
    *flags = (*flags & ~FLAG_MOUSE_PRESSED) | pressedBefore;
}

static LRESULT CALLBACK HotbarWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == UseSlotMessage && UseSlotMessage != 0)
    {
        if (wParam < SLOT_COUNT) UseSlot((int)wParam);
        return 0;
    }
    return CallWindowProcA(GameWindowProc, window, message, wParam, lParam);
}

// Subclasses the game window the first time it is seen in front (and again if the game creates a new one).
static void HookGameWindow(HWND window)
{
    WNDPROC current;
    if (window == GameWindow) return;
    current = (WNDPROC)GetWindowLongPtrA(window, GWLP_WNDPROC);
    if (current == HotbarWindowProc) return;
    GameWindowProc = current;
    GameWindow = window;
    SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)HotbarWindowProc);
    Log("hooked game window %p", window);
}

static DWORD WINAPI KeyThread(LPVOID unused)
{
    BOOL wasDown[SLOT_COUNT] = { 0 };
    for (;;)
    {
        HWND window = GetForegroundWindow();
        DWORD processId = 0;
        BOOL gameInFront;
        int i;

        Sleep(10);
        if (window != NULL) GetWindowThreadProcessId(window, &processId);
        gameInFront = processId == GetCurrentProcessId();
        if (gameInFront) HookGameWindow(window);
        for (i = 0; i < SLOT_COUNT; i++)
        {
            BOOL down = SlotKeys[i] != 0 && gameInFront && (GetAsyncKeyState(SlotKeys[i]) & 0x8000) != 0;
            // Alt and Ctrl combinations (Alt+F4 and the like) are left to the game.
            if (down && !wasDown[i] && (GetAsyncKeyState(VK_MENU) & 0x8000) == 0 &&
                (GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0 && GameWindow != NULL)
            {
                PostMessageA(GameWindow, UseSlotMessage, (WPARAM)i, 0);
            }
            wasDown[i] = down;
        }
    }
}

static BOOL WriteJump(BYTE* site, size_t length, void* target)
{
    DWORD oldProtect;
    size_t i;
    if (!VirtualProtect(site, length, PAGE_EXECUTE_READWRITE, &oldProtect)) return FALSE;
    site[0] = 0xE9;
    *(DWORD*)(site + 1) = (DWORD)((BYTE*)target - (site + 5));
    for (i = 5; i < length; i++) site[i] = 0x90;
    VirtualProtect(site, length, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), site, length);
    return TRUE;
}

static void ApplyPatch(HINSTANCE instance)
{
    BYTE* base = (BYTE*)GetModuleHandleA(NULL);
    BYTE* ctor = base + MAIN_CTOR_RVA;
    BYTE* dtor = base + MAIN_DTOR_RVA;
    HANDLE thread;
    int i;

    if (!IsGameImageLargeEnough(base) || *(int*)(base + VERSION_RVA) != VERSION_VALUE ||
        memcmp(ctor, CtorOriginal, sizeof(CtorOriginal)) != 0 ||
        memcmp(dtor, DtorOriginal, sizeof(DtorOriginal)) != 0)
    {
        OutputDebugStringA("HotbarKeys: unsupported SpellForce.exe build or code already changed, nothing patched.\n");
        return;
    }

    GameBase = base;
    ModuleInstance = instance;
    ReadConfig();
    UseSlotMessage = RegisterWindowMessageA("HotbarKeys.UseSlot");
    CtorResume = (DWORD)(ctor + sizeof(CtorOriginal));
    DtorResume = (DWORD)(dtor + sizeof(DtorOriginal));
    WriteJump(ctor, sizeof(CtorOriginal), MainCtorHook);
    WriteJump(dtor, sizeof(DtorOriginal), MainDtorHook);

    thread = CreateThread(NULL, 0, KeyThread, NULL, 0, NULL);
    if (thread != NULL) CloseHandle(thread);

    for (i = 0; i < SLOT_COUNT; i++) Log("Slot%d key %d", i + 1, SlotKeys[i]);
    OutputDebugStringA("HotbarKeys: hotbar slot keys active.\n");
}
