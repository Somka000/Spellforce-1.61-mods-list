#pragma once
// CooldownClock - cooldowns on the spell hotbar of SpellForce 1.61.11213 shown as a clock: a slot that is cooling
// down stays dark only in the part of the time that is left, the bright part grows clockwise from 12 o'clock, and
// below 10 seconds the time left is shown in the middle of the slot (whole seconds, below 1 second as 0.5).
//
// Game side (addresses with the default image base):
//   CUiMain (vtable 00C06ADC) updates itself every frame in vtable +238h (009DE860, thiscall, 2 floats). Its
//   current game tick is the float [CUiMain+7500h]. The hotbar is the CUiBottom [CUiMain+774Ch]; its 10 slot
//   buttons are at [bar+39Ch+4*i] and slot i's record at [CUiMain+7558h] + 32 * i (kind byte +10h: 10 = avatar,
//   11-15 = heroes; spell line word +12h). CUiMain::GetReadyTick(kind, line) (009E6F20) gives the tick the spell
//   can be cast again; while it is in the future the game disables the slot, which sets the button's look to
//   5 (dark) through vtable +2E4h (0092A050, look kept at +2F0h).
//   The patch replaces vtable +238h of CUiMain: after the game's update it works out, for every slot, the time
//   left and the share of the cooldown left (the cooldown's length is the time left when the slot was first
//   seen cooling down), keeps the look of cooling slots at 4 (normal) and remembers where to draw. It also
//   replaces vtable +2E4h of the slot buttons' class (00BE7CCC) so the game cannot darken a cooling slot again
//   (for example when the mouse moves over it). Slots that are disabled for other reasons are not touched.
//   Ticks become seconds with the measured tick rate (10 ticks per second until measured).
// Drawing: the game gets Direct3DCreate9 with GetProcAddress from its import table (005D7EF8). The patch wraps
// that import, then IDirect3D9::CreateDevice, then the device's EndScene. When d3d9.dll was already loaded
// before the mod (another mod or a launcher overlay loaded it), a background thread makes a small device of its
// own on a hidden window to find EndScene in d3d9.dll. EndScene itself is patched (hot-patch prologue), so the
// game's device is covered whenever it was made. The hook draws the dark part (a fan of
// triangles, darkness from the config) and the seconds (digits drawn once with GDI into a texture) over the
// slots at the end of every frame. The look is only changed while the drawing works.
//
// Settings in CooldownClock.txt next to the .asi/.sfm (created with defaults when missing).

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <math.h>
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

#define MAIN_VTABLE_RVA      0x806ADC
#define MAIN_VT_UPDATE       0x238
#define MAIN_UPDATE_RVA      0x5DE860
#define BUTTON_VTABLE_RVA    0x7E7CCC
#define BUTTON_VT_SET_LOOK   0x2E4
#define SET_LOOK_RVA         0x52A050
#define BOTTOM_VTABLE_RVA    0x805A68
#define GET_READY_TICK_RVA   0x5E6F20
#define GET_PROC_ADDRESS_IAT 0x788060 // KERNEL32!GetProcAddress in the game's import table

#define MAIN_NOW             0x7500
#define MAIN_HOTBAR          0x774C
#define MAIN_SLOTS_BEGIN     0x7558
#define MAIN_SLOTS_END       0x755C
#define SLOT_RECORD_SIZE     32
#define SLOT_KIND            0x10
#define SLOT_LINE            0x12
#define BOTTOM_BUTTONS       0x39C
#define CONTROL_FLAGS        0x128
#define CONTROL_PARENT       0x12C
#define CONTROL_ABS_RECT     0x144
#define BUTTON_LOOK          0x2F0
#define FLAG_ENABLED         1
#define FLAG_VISIBLE         2
#define LOOK_NORMAL          4
#define LOOK_DISABLED        5

#define SLOT_COUNT           10
#define MAX_PARENT_DEPTH     32
#define D3D_CREATE_DEVICE    16 // IDirect3D9 vtable index
#define D3D_END_SCENE        42 // IDirect3DDevice9 vtable index
#define SNAPSHOT_MAX_AGE_MS  250
#define DRAWING_MAX_AGE_MS   1000

static const char DefaultConfig[] =
    "# CooldownClock (.asi or .sfm) - cooldowns on the spell hotbar as a clock.\r\n"
    "# Darkness of the part of the slot that is still cooling down, in percent (0-100).\r\n"
    "Darkness=65\r\n"
    "# The time left is written in the slot below this many seconds (0 = never).\r\n"
    "ShowSecondsBelow=10\r\n"
    "# Log=1 writes CooldownClock.log next to this file.\r\n"
    "Log=0\r\n";

/* ------------------------------------------------------------------------ */
/* Settings and log                                                          */
/* ------------------------------------------------------------------------ */

static HINSTANCE ModuleInstance;
static int Darkness = 65;
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
    strcpy_s(LogPath + (slash + 1 - path), MAX_PATH - (slash + 1 - path), "CooldownClock.log");
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "CooldownClock.txt");

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
        if (_stricmp(line, "Darkness") == 0) Darkness = atoi(value);
        else if (_stricmp(line, "ShowSecondsBelow") == 0) ShowSecondsBelow = atoi(value);
        else if (_stricmp(line, "Log") == 0) LogEnabled = atoi(value) != 0;
    }
    fclose(file);
    if (Darkness < 0) Darkness = 0;
    if (Darkness > 100) Darkness = 100;
    if (ShowSecondsBelow < 0) ShowSecondsBelow = 0;
}

/* ------------------------------------------------------------------------ */
/* Game side                                                                 */
/* ------------------------------------------------------------------------ */

typedef void (__fastcall *MainUpdateFn)(BYTE* main, void* unused, float a, float b);
typedef void (__fastcall *SetLookFn)(BYTE* button, void* unused, int look);
typedef int (__fastcall *GetReadyTickFn)(BYTE* main, void* unused, DWORD kind, DWORD line);

static BYTE* GameBase;
static MainUpdateFn OriginalMainUpdate;
static SetLookFn OriginalSetLook;
static GetReadyTickFn GetReadyTick;

// What the end of the frame draws, written by the game's update
typedef struct
{
    BOOL Cooling;
    float X, Y, Width, Height; // UI coordinates
    float Left;                // share of the cooldown left, 0-1
    float Seconds;             // time left
} SlotState;

static SlotState Slots[SLOT_COUNT];
static float RootWidth, RootHeight;
static volatile DWORD SnapshotTick;
static volatile DWORD LastDrawTick;   // last frame the clock was drawn

// Buttons whose look the patch keeps normal, with the cooldown each slot was first seen with
static BYTE* CoolingButtons[SLOT_COUNT];
static int CooldownReady[SLOT_COUNT];
static float CooldownTotal[SLOT_COUNT];

// Measured game ticks per second
static float TicksPerSecond = 10.0f;
static float RateTickStart;
static DWORD RateMsStart;

static BOOL IsCoolingButton(BYTE* button)
{
    int i;
    for (i = 0; i < SLOT_COUNT; i++)
    {
        if (CoolingButtons[i] == button) return TRUE;
    }
    return FALSE;
}

// Vtable +2E4h of the slot buttons' class: a cooling slot keeps its normal look.
static void __fastcall SetLookHook(BYTE* button, void* unused, int look)
{
    if (look == LOOK_DISABLED && IsCoolingButton(button)) look = LOOK_NORMAL;
    OriginalSetLook(button, NULL, look);
}

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

// Ends the patch's hold on the look of slot <i>: a slot the game still has disabled gets its dark look back.
static void ReleaseSlot(int i)
{
    BYTE* button = CoolingButtons[i];
    CoolingButtons[i] = NULL;
    CooldownReady[i] = 0;
    if (button != NULL && (*(DWORD*)(button + CONTROL_FLAGS) & FLAG_ENABLED) == 0 &&
        *(int*)(button + BUTTON_LOOK) != LOOK_DISABLED)
    {
        OriginalSetLook(button, NULL, LOOK_DISABLED);
    }
}

static void UpdateSlots(BYTE* main)
{
    BOOL drawing = GetTickCount() - LastDrawTick < DRAWING_MAX_AGE_MS;
    BYTE* bar = *(BYTE**)(main + MAIN_HOTBAR);
    BYTE* begin = *(BYTE**)(main + MAIN_SLOTS_BEGIN);
    int count = (int)((*(BYTE**)(main + MAIN_SLOTS_END) - begin) / SLOT_RECORD_SIZE);
    float now = *(float*)(main + MAIN_NOW);
    int i;

    MeasureTickRate(now);
    if (bar == NULL || *(DWORD*)bar != (DWORD)(GameBase + BOTTOM_VTABLE_RVA)) return;

    for (i = 0; i < SLOT_COUNT; i++)
    {
        BYTE* button = *(BYTE**)(bar + BOTTOM_BUTTONS + 4 * i);
        SlotState* slot = &Slots[i];
        int ready = 0;
        float left;

        slot->Cooling = FALSE;
        if (button == NULL || *(DWORD*)button != (DWORD)(GameBase + BUTTON_VTABLE_RVA))
        {
            ReleaseSlot(i);
            continue;
        }
        if (i < count && begin != NULL)
        {
            BYTE* record = begin + i * SLOT_RECORD_SIZE;
            ready = GetReadyTick(main, NULL, *(record + SLOT_KIND), *(WORD*)(record + SLOT_LINE));
        }
        left = (float)ready - now;
        if (ready == 0 || left <= 0.0f || !drawing || !IsShown(button, &RootWidth, &RootHeight))
        {
            if (CoolingButtons[i] != NULL) ReleaseSlot(i);
            continue;
        }

        // A new cooldown (or another one in the same slot): its length is the time left now
        if (CoolingButtons[i] != button || CooldownReady[i] != ready)
        {
            if (CoolingButtons[i] != NULL && CoolingButtons[i] != button) ReleaseSlot(i);
            CoolingButtons[i] = button;
            CooldownReady[i] = ready;
            CooldownTotal[i] = left;
            Log("slot %d: cooldown %.1f ticks (%.1f s)", i + 1, left, left / TicksPerSecond);
        }
        if (left > CooldownTotal[i]) CooldownTotal[i] = left;
        if (*(int*)(button + BUTTON_LOOK) == LOOK_DISABLED) OriginalSetLook(button, NULL, LOOK_NORMAL);

        slot->Cooling = TRUE;
        slot->X = *(float*)(button + CONTROL_ABS_RECT);
        slot->Y = *(float*)(button + CONTROL_ABS_RECT + 4);
        slot->Width = *(float*)(button + CONTROL_ABS_RECT + 8);
        slot->Height = *(float*)(button + CONTROL_ABS_RECT + 12);
        slot->Left = CooldownTotal[i] > 0.0f ? left / CooldownTotal[i] : 0.0f;
        slot->Seconds = left / TicksPerSecond;
    }
    SnapshotTick = GetTickCount();
}

// Vtable +238h of CUiMain: the game's update, then the slots.
static void __fastcall MainUpdateHook(BYTE* main, void* unused, float a, float b)
{
    OriginalMainUpdate(main, NULL, a, b);
    __try
    {
        UpdateSlots(main);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        SnapshotTick = 0;
    }
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

// Point on the edge of the slot (centre cx, cy, half width hw, half height hh) in direction <angle> (0 = up,
// clockwise), the angle taken as if the slot were square, so the clock fills the whole slot.
static void EdgePoint(float cx, float cy, float hw, float hh, float angle, float* x, float* y)
{
    float dx = sinf(angle), dy = -cosf(angle);
    float m = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy);
    *x = cx + dx / m * hw;
    *y = cy + dy / m * hh;
}

// Dark part: from the angle reached so far clockwise back to 12 o'clock. The edge between two corners of the
// square is straight, so the fan needs only the start, the corners passed and the end.
static void DrawShade(IDirect3DDevice9* device, float x, float y, float w, float h, float left)
{
    static const float corners[4] = { 0.78539816f, 2.35619449f, 3.92699082f, 5.49778714f };
    ColorVertex fan[8];
    DWORD color = (DWORD)(Darkness * 255 / 100) << 24;
    float cx = x + w / 2, cy = y + h / 2, hw = w / 2, hh = h / 2;
    float start = (1.0f - left) * 6.2831853f;
    int n = 0, i;

    fan[n].X = cx; fan[n].Y = cy; n++;
    EdgePoint(cx, cy, hw, hh, start, &fan[n].X, &fan[n].Y); n++;
    for (i = 0; i < 4; i++)
    {
        if (corners[i] > start)
        {
            EdgePoint(cx, cy, hw, hh, corners[i], &fan[n].X, &fan[n].Y);
            n++;
        }
    }
    EdgePoint(cx, cy, hw, hh, 6.2831853f, &fan[n].X, &fan[n].Y); n++;
    for (i = 0; i < n; i++)
    {
        fan[i].Z = 0; fan[i].Rhw = 1; fan[i].Color = color;
    }
    IDirect3DDevice9_SetTexture(device, 0, NULL);
    IDirect3DDevice9_SetFVF(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    IDirect3DDevice9_DrawPrimitiveUP(device, D3DPT_TRIANGLEFAN, n - 2, fan, sizeof(ColorVertex));
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

static void DrawClocks(IDirect3DDevice9* device)
{
    IDirect3DStateBlock9* saved = NULL;
    D3DVIEWPORT9 viewport;
    float sx, sy;
    int i;

    if (GetTickCount() - SnapshotTick > SNAPSHOT_MAX_AGE_MS || RootWidth < 100.0f || RootHeight < 100.0f) return;
    for (i = 0; i < SLOT_COUNT && !Slots[i].Cooling; i++)
    {
    }
    if (i == SLOT_COUNT) return;
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

    for (i = 0; i < SLOT_COUNT; i++)
    {
        SlotState slot = Slots[i];
        float x, y, w, h;
        if (!slot.Cooling) continue;
        x = viewport.X + slot.X * sx;
        y = viewport.Y + slot.Y * sy;
        w = slot.Width * sx;
        h = slot.Height * sy;
        DrawShade(device, x, y, w, h, slot.Left > 1.0f ? 1.0f : slot.Left);
        if (ShowSecondsBelow > 0 && slot.Seconds < (float)ShowSecondsBelow)
        {
            char text[16];
            FormatSeconds(slot.Seconds, text, sizeof(text));
            DrawDigits(device, x + w / 2, y + h / 2, h * 0.5f, text);
        }
    }
    IDirect3DStateBlock9_Apply(saved);
    IDirect3DStateBlock9_Release(saved);
}

static HRESULT WINAPI EndSceneHook(IDirect3DDevice9* device)
{
    __try
    {
        DrawClocks(device);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
    LastDrawTick = GetTickCount();
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
// instructions move to a trampoline), so every device is covered - on this Windows every device has a vtable of
// its own. When the first instructions cannot be moved, only this vtable's slot is replaced.
static void HookEndScene(void** vtable, const char* how)
{
    BYTE* function = (BYTE*)vtable[D3D_END_SCENE];
    int length = 0, step;

    if (function == (BYTE*)EndSceneHook || InterlockedCompareExchange(&EndSceneHooked, 1, 0) != 0) return;
    if (function[0] == 0xE9)
    {
        // Already patched by another mod (EffectStacks): the hook goes before its jump
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
    windowClass.lpszClassName = "CooldownClockProbe";
    RegisterClassA(&windowClass);
    window = CreateWindowA("CooldownClockProbe", "", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, ModuleInstance, NULL);
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
    UnregisterClassA("CooldownClockProbe", ModuleInstance);
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

static void ApplyPatch(HINSTANCE instance)
{
    BYTE* base = (BYTE*)GetModuleHandleA(NULL);
    void** mainSlot;
    void** lookSlot;
    void** iatSlot;

    ModuleInstance = instance;
    ReadConfig();
    if (!IsGameImageLargeEnough(base) || *(int*)(base + VERSION_RVA) != VERSION_VALUE)
    {
        Log("unsupported SpellForce.exe build, nothing patched");
        return;
    }
    mainSlot = (void**)(base + MAIN_VTABLE_RVA + MAIN_VT_UPDATE);
    lookSlot = (void**)(base + BUTTON_VTABLE_RVA + BUTTON_VT_SET_LOOK);
    iatSlot = (void**)(base + GET_PROC_ADDRESS_IAT);
    // The UI update may already be replaced by another mod (EffectStacks): it is called in turn
    if (*mainSlot == NULL || *lookSlot != (void*)(base + SET_LOOK_RVA))
    {
        Log("game code not as expected, nothing patched");
        return;
    }
    GameBase = base;
    GetReadyTick = (GetReadyTickFn)(base + GET_READY_TICK_RVA);
    OriginalMainUpdate = (MainUpdateFn)*mainSlot;
    OriginalSetLook = (SetLookFn)*lookSlot;
    OriginalGetProcAddress = (GetProcAddressFn)*iatSlot;
    WriteVtableSlot(iatSlot, (void*)GetProcAddressHook);
    WriteVtableSlot(lookSlot, (void*)SetLookHook);
    WriteVtableSlot(mainSlot, (void*)MainUpdateHook);
    if (GetModuleHandleA("d3d9.dll") != NULL)
    {
        // Direct3D was loaded before the mod (another mod or the overlay of a launcher): the game may already
        // have asked for Direct3DCreate9, so EndScene is found with a device of the mod's own.
        HANDLE thread = CreateThread(NULL, 0, HookExistingDirect3D, NULL, 0, NULL);
        if (thread != NULL) CloseHandle(thread);
        Log("patched (Direct3D already loaded)");
    }
    else
    {
        Log("patched");
    }
}
