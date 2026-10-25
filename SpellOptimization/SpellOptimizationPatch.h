#pragma once
// SpellOptimization - stops the game from opening Log.txt for every log line. SpellForce 1.61.11213 only.
//
// The game's log file writer 005EE180 (cdecl, one argument: the line) runs for every message while file logging
// is on ([00CF1AFC], on by default): fopen(<Documents\SpellForce\Log.txt> [00D3ADF0], "a+t"), on the first call
// a header ([00D3ADEC] set to 1), fprintf("%s\n", line), fclose. The game logs every spell effect it adds and
// every spell it removes ("[INFO][GD]: GdSpell::AddEffect() ...", "GdSpell::Done() ..."), so with many auras that
// is about a hundred file opens per second, and LagProfiler measured about 45% of the main thread in them.
//
// The patch puts a jump at the start of 005EE180. The first line still goes through the game's code (header and
// all). After that Log.txt stays open and every line is appended with one WriteFile (text mode like the game:
// \n written as \r\n), so the log stays complete and is on disk right away even if the game crashes.
// With Disable=1 in SpellOptimization.ini ([SpellOptimization], next to the .asi/.sfm) nothing is written to Log.txt at all.
// Only logging changes, so it is safe in multiplayer and every player can decide for themselves.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#define VERSION_RVA     0x177E63 // Int32 11213 in build 1.61.11213
#define VERSION_VALUE   11213

#define WRITER_RVA      0x1EE180 // log file writer: void __cdecl (const char* line)
#define WRITER_RESUME   9        // push ebp / mov ebp,esp / sub esp,244h
#define LOG_ON_RVA      0x8F1AFC // int: file logging on
#define HEADER_DONE_RVA 0x93ADEC // int: header written this session
#define LOG_PATH_RVA    0x93ADF0 // char[260]: path of Log.txt

static const BYTE WriterPrologue[WRITER_RESUME] = { 0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x44, 0x02, 0x00, 0x00 };

typedef void (__cdecl *WriterFn)(const char* line);

static BYTE* GameBase;
static WriterFn OriginalWriter;          // trampoline: the moved prologue, then a jump back
static BYTE Trampoline[WRITER_RESUME + 5];
static CRITICAL_SECTION LogLock;
static HANDLE LogFile = INVALID_HANDLE_VALUE;
static BOOL LogDisabled;

static void AppendText(const char* line)
{
    char buffer[1024];
    int used = 0;
    DWORD written;
    const char* p;

    // Text mode like the game's "a+t": every \n becomes \r\n, and one \n ends the line.
    for (p = line; ; p++)
    {
        char c = *p ? *p : '\n';
        if (used > (int)sizeof(buffer) - 2)
        {
            WriteFile(LogFile, buffer, used, &written, NULL);
            used = 0;
        }
        if (c == '\n') buffer[used++] = '\r';
        buffer[used++] = c;
        if (!*p) break;
    }
    WriteFile(LogFile, buffer, used, &written, NULL);
}

static void __cdecl WriterHook(const char* line)
{
    if (LogDisabled || !*(int*)(GameBase + LOG_ON_RVA))
        return;

    EnterCriticalSection(&LogLock);
    if (!*(int*)(GameBase + HEADER_DONE_RVA) || line == NULL)
    {
        // The first line (with the header) the game's way.
        LeaveCriticalSection(&LogLock);
        OriginalWriter(line);
        return;
    }
    if (LogFile == INVALID_HANDLE_VALUE)
    {
        LogFile = CreateFileA((const char*)(GameBase + LOG_PATH_RVA), FILE_APPEND_DATA,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    }
    if (LogFile == INVALID_HANDLE_VALUE)
    {
        LeaveCriticalSection(&LogLock);
        OriginalWriter(line);
        return;
    }
    AppendText(line);
    LeaveCriticalSection(&LogLock);
}

static void ApplyPatch(HINSTANCE instance)
{
    char ini[MAX_PATH];
    char* slash;
    BYTE* writer;
    BYTE jump[WRITER_RESUME];
    DWORD oldProtect;

    GameBase = (BYTE*)GetModuleHandleA(NULL);
    writer = GameBase + WRITER_RVA;
    if (*(int*)(GameBase + VERSION_RVA) != VERSION_VALUE)
    {
        OutputDebugStringA("SpellOptimization: unsupported SpellForce.exe build, nothing patched.\n");
        return;
    }
    if (memcmp(writer, WriterPrologue, WRITER_RESUME) != 0)
    {
        OutputDebugStringA("SpellOptimization: unexpected code at the log writer (another mod?), nothing patched.\n");
        return;
    }

    GetModuleFileNameA(instance, ini, MAX_PATH);
    slash = strrchr(ini, '\\');
    lstrcpyA(slash ? slash + 1 : ini, "SpellOptimization.ini");
    LogDisabled = GetPrivateProfileIntA("SpellOptimization", "Disable", 0, ini) != 0;

    InitializeCriticalSection(&LogLock);

    // Trampoline: the 9 prologue bytes, then jmp back to writer + 9.
    memcpy(Trampoline, WriterPrologue, WRITER_RESUME);
    Trampoline[WRITER_RESUME] = 0xE9;
    *(LONG*)(Trampoline + WRITER_RESUME + 1) = (LONG)((writer + WRITER_RESUME) - (Trampoline + WRITER_RESUME + 5));
    VirtualProtect(Trampoline, sizeof(Trampoline), PAGE_EXECUTE_READWRITE, &oldProtect);
    OriginalWriter = (WriterFn)(void*)Trampoline;

    // jmp WriterHook, then nops over the rest of the prologue.
    memset(jump, 0x90, sizeof(jump));
    jump[0] = 0xE9;
    *(LONG*)(jump + 1) = (LONG)((BYTE*)WriterHook - (writer + 5));
    VirtualProtect(writer, WRITER_RESUME, PAGE_EXECUTE_READWRITE, &oldProtect);
    memcpy(writer, jump, WRITER_RESUME);
    VirtualProtect(writer, WRITER_RESUME, oldProtect, &oldProtect);
    FlushInstructionCache(GetCurrentProcess(), writer, WRITER_RESUME);

    OutputDebugStringA(LogDisabled ? "SpellOptimization: Log.txt disabled.\n" : "SpellOptimization: Log.txt kept open.\n");
}
