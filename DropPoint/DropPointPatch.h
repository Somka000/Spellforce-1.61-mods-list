#pragma once
// DropPoint - while a building is being placed (its blueprint follows the mouse), the place its workers walk to
// with their goods (where for example the woodcutter kneels to drop the logs) is lit on the ground: orange in the
// middle of each field, light yellow at its sides. SpellForce 1.61.11213.
//
// Game side (addresses with the default image base):
//   - Placing: the build mode's update 00A370B0 (this = mode: building type byte +30h, angle in degrees +38h,
//     the place found for it +3Ch / +3Eh as map x / y words, the UI +4) finds a free place (006D93C0) and, when
//     there is one, draws the blueprint and ends with SetCursor("ui_cursor_buildhouse_enabled") (call 00A36100 at
//     00A37786, the cursor name at 00C0CA58); with no place it sets "..._disabled".
//   - Workers bringing goods walk to the building's place, its middle (006F20D6): the middle field of its shape
//     (006D74A0), kept free; the building's model is drawn off it (006D9B50).
//   - Map: [00D75D38] (height of field (x, y): word [[+2Ch] + ((y & m) << 10 | (x & m)) * 2 + 61118Ah], m = word
//     +38h); world = map field * 1.4 (00BD099Ch), height * 0.01 (00BD0998h).
//   - Camera: UI +2D8h = game view, +4Ch = camera, +330h its view data; 005F54A0(view data) is the matrix from the
//     world to the screen, its rows kept as (constant, x, y, z): (sx, sy, depth, w) = M[0] + x M[1] + y M[2] +
//     z M[3], screen = (sx, sy) / w in pixels; the mouse goes back through the inverse 005F5170 in 008958D0.
// Where workers really drop their goods is learned from the game: once per frame (CUiMain update, vtable 00C06ADC
// +238h) the patch looks at every figure ([00D274AC]: position words +2/+4, building +14h, job +20Bh); when one
// starts a drop job (checking / putting down its load: jobs 4, 10, 11, 12, 20, 30, 46, 64, 67, 92, 101, 114, 123)
// its place relative to its building (CGdBuilding [toolbox+0Ch]: place +4/+6, type +0Ah, angle +1Bh), turned back
// to angle 0, is kept for the building's type (DropPoint.dat next to the .asi/.sfm, for the next games too).
// The patch redirects the SetCursor call at 00A37786: with the "enabled" cursor it lights one field of the
// blueprint: for a type whose drop place was seen, that place turned by the blueprint's angle; otherwise the
// building's place itself (in a real game every type seen dropped there). The end of the frame (Direct3D EndScene,
// found like CooldownClock) draws it. Only what is shown changes, so it is safe in multiplayer.
// Settings in DropPoint.txt next to the .asi/.sfm.

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

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

#define SET_CURSOR_CALL      0x637786 // call 00A36100 at the end of the build mode's update
#define SET_CURSOR_RVA       0x636100 // build mode: SetCursor(name, name)
#define CURSOR_ENABLED       0x80CA58 // "ui_cursor_buildhouse_enabled"
#define TOOLBOX_RVA          0x3B1050 // CGdBuildingToolbox
#define SCREEN_MATRIX_RVA    0x1F54A0 // camera view data: world -> screen matrix
#define WORLD_GLOBAL         0x975D38 // the map
#define TILE_SCALE_RVA       0x7D099C // float 1.4
#define HEIGHT_SCALE_RVA     0x7D0998 // float 0.01
#define GET_PROC_ADDRESS_IAT 0x788060 // KERNEL32!GetProcAddress in the game's import table
#define CURSOR_ENABLED_PUSH  0x63776F // push "..._enabled" on the way to that call

#define MODE_UI              0x04
#define MODE_TYPE            0x30
#define MODE_ANGLE           0x38
#define MODE_PLACE           0x3C
#define UI_GAME_VIEW         0x2D8
#define VIEW_CAMERA          0x4C
#define CAMERA_VIEW_DATA     0x330
#define MAX_DROP_DISTANCE    30   // fields from the building's place
#define WORLD_HEIGHTS        0x2C
#define WORLD_MASK           0x38
#define HEIGHTS_OFFSET       0x61118A
#define MAX_FIELDS           1
#define MAIN_VTABLE_RVA      0x806ADC // CUiMain
#define MAIN_VT_UPDATE       0x238
#define FIGURES_RVA          0x9274AC // CGdFigure*
#define TOOLBOX_BUILDINGS    0x0C
#define FIGURE_SIZE          0x2B3
#define FIGURE_X             0x02
#define FIGURE_Y             0x04
#define FIGURE_BUILDING      0x14
#define FIGURE_FLAGS         0x16
#define FIGURE_OWNER         0x1C
#define FIGURE_JOB           0x20B
#define FIGURE_GONE          0x0A
#define BUILDING_SIZE        0x34
#define BUILDING_PLACE       0x04
#define BUILDING_TYPE        0x0A
#define BUILDING_ANGLE       0x1B
#define MAX_FIGURES          3000
#define MAX_BUILDINGS        1000
#define TYPES                256
#define SNAPSHOT_MAX_AGE_MS  200
#define D3D_CREATE_DEVICE    16 // IDirect3D9 vtable index
#define D3D_END_SCENE        42 // IDirect3DDevice9 vtable index

static const char DefaultConfig[] =
    "# DropPoint (.asi or .sfm) - the place workers bring their goods to, lit on a building's blueprint.\r\n"
    "# How strong the light is, in percent (10-100).\r\n"
    "Opacity=75\r\n"
    "# Log=1 writes DropPoint.log next to this file.\r\n"
    "Log=0\r\n";

/* ------------------------------------------------------------------------ */
/* Settings and log                                                          */
/* ------------------------------------------------------------------------ */

static HINSTANCE ModuleInstance;
static char DropPath[MAX_PATH];
static int Opacity = 75;
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
    strcpy_s(LogPath + (slash + 1 - path), MAX_PATH - (slash + 1 - path), "DropPoint.log");
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "DropPoint.dat");
    strcpy_s(DropPath, MAX_PATH, path);
    strcpy_s(slash + 1, MAX_PATH - (slash + 1 - path), "DropPoint.txt");

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
        if (_stricmp(line, "Opacity") == 0) Opacity = atoi(value);
        else if (_stricmp(line, "Log") == 0) LogEnabled = atoi(value) != 0;
    }
    fclose(file);
    if (Opacity < 10) Opacity = 10;
    if (Opacity > 100) Opacity = 100;
}

/* ------------------------------------------------------------------------ */
/* Game side                                                                 */
/* ------------------------------------------------------------------------ */

typedef void (__fastcall *SetCursorFn)(BYTE* mode, void* unused, DWORD name, DWORD name2);
typedef BYTE* (__cdecl *ToolboxFn)(void);
typedef float* (__fastcall *ScreenMatrixFn)(BYTE* viewData, void* unused);

static BYTE* GameBase;
static SetCursorFn OriginalSetCursor;
static ToolboxFn GetToolbox;
static ScreenMatrixFn ScreenMatrix;
static DWORD CursorEnabled;
static BYTE** WorldGlobal;
static float TileScale = 1.4f, HeightScale = 0.01f;

typedef struct { float X, Y; } ScreenPoint;
typedef struct { ScreenPoint Middle, Corner[4]; } LitField;

static LitField Fields[MAX_FIELDS];
static volatile int FieldCount;
static volatile DWORD SnapshotTick;
static int LastType = -1, LastAngle = -1;

// Height of map field (x, y) in world units
static float FieldHeight(BYTE* world, int x, int y)
{
    DWORD mask = *(WORD*)(world + WORLD_MASK);
    BYTE* heights = *(BYTE**)(world + WORLD_HEIGHTS);
    DWORD index = (((DWORD)y & mask) << 10) | ((DWORD)x & mask);
    return *(WORD*)(heights + index * 2 + HEIGHTS_OFFSET) * HeightScale;
}

// World point to screen pixels with the camera's matrix. The game keeps its rows as (the constant row, x, y, z):
// (sx, sy, depth, w) = m[0..3] + x * m[4..7] + y * m[8..11] + z * m[12..15], screen = (sx, sy) / w (008958D0 does the
// same with the inverse for the mouse). FALSE behind the camera.
static BOOL Project(const float* m, float x, float y, float z, ScreenPoint* out)
{
    float sx = m[0] + x * m[4] + y * m[8] + z * m[12];
    float sy = m[1] + x * m[5] + y * m[9] + z * m[13];
    float w = m[3] + x * m[7] + y * m[11] + z * m[15];
    if (w <= 1e-6f) return FALSE;
    out->X = sx / w;
    out->Y = sy / w;
    return TRUE;
}

// Map field (fx, fy) lit: where its middle and corners are on the screen with matrix <m>; FALSE when not seen.
static BOOL LightField(BYTE* world, const float* m, int fx, int fy, LitField* out)
{
    float h00 = FieldHeight(world, fx, fy);
    float h10 = FieldHeight(world, fx + 1, fy), h01 = FieldHeight(world, fx, fy + 1);
    float h11 = FieldHeight(world, fx + 1, fy + 1), hm10 = FieldHeight(world, fx - 1, fy);
    float h0m1 = FieldHeight(world, fx, fy - 1), hm1m1 = FieldHeight(world, fx - 1, fy - 1);
    float h1m1 = FieldHeight(world, fx + 1, fy - 1), hm11 = FieldHeight(world, fx - 1, fy + 1);
    // corners half a field away, at the mean height of the 4 fields around them
    float cx[4] = { fx - 0.5f, fx + 0.5f, fx + 0.5f, fx - 0.5f };
    float cy[4] = { fy - 0.5f, fy - 0.5f, fy + 0.5f, fy + 0.5f };
    float ch[4] =
    {
        (h00 + hm10 + h0m1 + hm1m1) / 4, (h00 + h10 + h0m1 + h1m1) / 4,
        (h00 + h10 + h01 + h11) / 4, (h00 + hm10 + h01 + hm11) / 4,
    };
    int k;
    BOOL ok = Project(m, fx * TileScale, fy * TileScale, h00, &out->Middle);
    for (k = 0; k < 4 && ok; k++) ok = Project(m, cx[k] * TileScale, cy[k] * TileScale, ch[k], &out->Corner[k]);
    return ok;
}

static void Turn(float di, float dj, float degrees, float* ti, float* tj)
{
    float r = degrees * 3.14159265f / 180.0f, c = cosf(r), s = sinf(r);
    *ti = di * c - dj * s;
    *tj = di * s + dj * c;
}

/* ------------------------------------------------------------------------ */
/* Learned drop places                                                       */
/* ------------------------------------------------------------------------ */

static const WORD DropJobs[] = { 4, 10, 11, 12, 20, 30, 46, 64, 67, 92, 101, 114, 123 };
static float DropI[TYPES], DropJ[TYPES];  // drop place of each building type at angle 0, from its place
static BOOL DropKnown[TYPES];
static WORD LastJob[MAX_FIGURES];

static void LoadDrops(void)
{
    FILE* file;
    int type;
    float i, j;
    if (DropPath[0] == 0 || fopen_s(&file, DropPath, "r") != 0) return;
    while (fscanf_s(file, "%d %f %f", &type, &i, &j) == 3)
    {
        if (type > 0 && type < TYPES)
        {
            DropI[type] = i;
            DropJ[type] = j;
            DropKnown[type] = TRUE;
        }
    }
    fclose(file);
}

static void SaveDrops(void)
{
    FILE* file;
    int type;
    if (DropPath[0] == 0 || fopen_s(&file, DropPath, "w") != 0) return;
    for (type = 1; type < TYPES; type++)
    {
        if (DropKnown[type]) fprintf(file, "%d %.2f %.2f\n", type, DropI[type], DropJ[type]);
    }
    fclose(file);
}

static BOOL IsDropJob(WORD job)
{
    int k;
    for (k = 0; k < sizeof(DropJobs) / sizeof(DropJobs[0]); k++)
    {
        if (DropJobs[k] == job) return TRUE;
    }
    return FALSE;
}

// A worker starts dropping its load: its place relative to its building, turned back to angle 0.
static void LearnDrop(BYTE* figure, BYTE* buildings)
{
    DWORD building = *(WORD*)(figure + FIGURE_BUILDING);
    BYTE* record;
    int type, angle, dx, dy;
    float li, lj;
    if (building == 0 || building >= MAX_BUILDINGS) return;
    record = buildings + building * BUILDING_SIZE;
    type = record[BUILDING_TYPE];
    angle = *(WORD*)(record + BUILDING_ANGLE);
    dx = (int)*(WORD*)(figure + FIGURE_X) - (int)*(WORD*)(record + BUILDING_PLACE);
    dy = (int)*(WORD*)(figure + FIGURE_Y) - (int)*(WORD*)(record + BUILDING_PLACE + 2);
    if (type == 0 || type >= TYPES || dx < -MAX_DROP_DISTANCE || dx > MAX_DROP_DISTANCE || dy < -MAX_DROP_DISTANCE || dy > MAX_DROP_DISTANCE) return;
    Turn((float)dx, (float)dy, (float)-angle, &li, &lj);
    if (DropKnown[type] && fabsf(DropI[type] - li) < 0.01f && fabsf(DropJ[type] - lj) < 0.01f) return;
    DropI[type] = li;
    DropJ[type] = lj;
    DropKnown[type] = TRUE;
    Log("building type %d (angle %d): workers drop at %+d/%+d from its place = %.2f/%.2f at angle 0", type, angle,
        dx, dy, li, lj);
    SaveDrops();
}

static void WatchWorkers(void)
{
    BYTE* figures = *(BYTE**)(GameBase + FIGURES_RVA);
    BYTE* toolbox;
    BYTE* buildings;
    DWORD count, id;
    if (figures == NULL || (toolbox = GetToolbox()) == NULL || (buildings = *(BYTE**)(toolbox + TOOLBOX_BUILDINGS)) == NULL) return;
    count = *(WORD*)figures;
    if (count >= MAX_FIGURES) count = MAX_FIGURES - 1;
    for (id = 1; id <= count; id++)
    {
        BYTE* figure = figures + id * FIGURE_SIZE;
        WORD job = *(WORD*)(figure + FIGURE_JOB);
        if (*(WORD*)(figure + FIGURE_OWNER) == 0xFFFF || (*(DWORD*)(figure + FIGURE_FLAGS) & FIGURE_GONE) != 0)
        {
            LastJob[id] = 0;
            continue;
        }
        if (job != LastJob[id] && IsDropJob(job)) LearnDrop(figure, buildings);
        LastJob[id] = job;
    }
}

typedef void (__fastcall *MainUpdateFn)(BYTE* main, void* unused, float a, float b);
static MainUpdateFn OriginalMainUpdate;

static void __fastcall MainUpdateHook(BYTE* main, void* unused, float a, float b)
{
    OriginalMainUpdate(main, NULL, a, b);
    __try
    {
        WatchWorkers();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

static void UpdateMarks(BYTE* mode)
{
    int type = mode[MODE_TYPE];
    int angle = *(int*)(mode + MODE_ANGLE);
    int x = *(WORD*)(mode + MODE_PLACE), y = *(WORD*)(mode + MODE_PLACE + 2);
    BYTE* world = *WorldGlobal;
    BYTE* ui = *(BYTE**)(mode + MODE_UI);
    BYTE* view = *(BYTE**)(ui + UI_GAME_VIEW);
    BYTE* camera = *(BYTE**)(view + VIEW_CAMERA);
    BOOL learned = type < TYPES && DropKnown[type];
    float* m;
    int fx = x, fy = y, n;

    if (world == NULL || camera == NULL) return;
    m = ScreenMatrix(camera + CAMERA_VIEW_DATA, NULL);
    if (learned)
    {
        // The drop place learned from the workers, turned by the blueprint's angle
        float ti, tj;
        Turn(DropI[type], DropJ[type], (float)angle, &ti, &tj);
        fx = x + (int)floorf(ti + 0.5f);
        fy = y + (int)floorf(tj + 0.5f);
    }
    // Not seen yet: the building's place itself (workers walk to it, 006F20D6, and every type seen dropped there)
    n = LightField(world, m, fx, fy, &Fields[0]) ? 1 : 0;
    FieldCount = n;
    SnapshotTick = GetTickCount();
    if (type != LastType || angle != LastAngle)
    {
        LastType = type;
        LastAngle = angle;
        Log("building type %d, angle %d at %d/%d: drop place %d/%d (%s), %s at %.0f/%.0f (screen)", type, angle, x, y,
            fx, fy, learned ? "learned" : "its place", n ? "lit" : "not seen", n ? Fields[0].Middle.X : 0.0f,
            n ? Fields[0].Middle.Y : 0.0f);
    }
}

// Replaces the build mode's SetCursor call at the end of its update.
static void __fastcall SetCursorHook(BYTE* mode, void* unused, DWORD name, DWORD name2)
{
    OriginalSetCursor(mode, NULL, name, name2);
    if (name != CursorEnabled)
    {
        FieldCount = 0;
        return;
    }
    __try
    {
        UpdateMarks(mode);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        FieldCount = 0;
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


// Light yellow at the sides, orange in the middle
static DWORD Colour(DWORD rgb, int percent)
{
    return ((DWORD)(Opacity * 255 * percent / 10000) << 24) | rgb;
}

static void DrawFields(IDirect3DDevice9* device)
{
    IDirect3DStateBlock9* saved = NULL;
    D3DVIEWPORT9 viewport;
    int i, count = FieldCount;

    if (count == 0 || GetTickCount() - SnapshotTick > SNAPSHOT_MAX_AGE_MS) return;
    if (FAILED(IDirect3DDevice9_GetViewport(device, &viewport))) return;
    if (FAILED(IDirect3DDevice9_CreateStateBlock(device, D3DSBT_ALL, &saved))) return;

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
    IDirect3DDevice9_SetRenderState(device, D3DRS_SHADEMODE, D3DSHADE_GOURAUD);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    IDirect3DDevice9_SetTextureStageState(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTextureStageState(device, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    IDirect3DDevice9_SetTexture(device, 0, NULL);
    IDirect3DDevice9_SetFVF(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);

    for (i = 0; i < count && i < MAX_FIELDS; i++)
    {
        LitField field = Fields[i];
        ColorVertex fan[6];
        int k;
        fan[0].X = viewport.X + field.Middle.X; fan[0].Y = viewport.Y + field.Middle.Y;
        fan[0].Color = Colour(0xFF8000, 100); // orange
        for (k = 0; k < 5; k++)
        {
            ScreenPoint corner = field.Corner[k % 4];
            fan[k + 1].X = viewport.X + corner.X; fan[k + 1].Y = viewport.Y + corner.Y;
            fan[k + 1].Color = Colour(0xFFF060, 100); // light yellow
        }
        for (k = 0; k < 6; k++)
        {
            fan[k].Z = 0; fan[k].Rhw = 1;
        }
        IDirect3DDevice9_DrawPrimitiveUP(device, D3DPT_TRIANGLEFAN, 4, fan, sizeof(ColorVertex));
    }
    IDirect3DStateBlock9_Apply(saved);
    IDirect3DStateBlock9_Release(saved);
}

static HRESULT WINAPI EndSceneHook(IDirect3DDevice9* device)
{
    __try
    {
        DrawFields(device);
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
// its own. EndScene already patched by another mod (CooldownClock, BetterIcons) starts with a jump: the hook goes
// before it.
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
    windowClass.lpszClassName = "DropPointProbe";
    RegisterClassA(&windowClass);
    window = CreateWindowA("DropPointProbe", "", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, NULL, NULL, ModuleInstance, NULL);
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
    UnregisterClassA("DropPointProbe", ModuleInstance);
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
    BYTE* call;
    void** iatSlot;
    LONG rel;

    ModuleInstance = instance;
    ReadConfig();
    if (!IsGameImageLargeEnough(base) || *(int*)(base + VERSION_RVA) != VERSION_VALUE)
    {
        Log("unsupported SpellForce.exe build, nothing patched");
        return;
    }
    call = base + SET_CURSOR_CALL;
    if (call[0] != 0xE8 || call + 5 + *(LONG*)(call + 1) != base + SET_CURSOR_RVA ||
        *(DWORD*)(base + CURSOR_ENABLED_PUSH) != (DWORD)(base + CURSOR_ENABLED))
    {
        Log("game code not as expected (another mod?), nothing patched");
        return;
    }
    GameBase = base;
    OriginalSetCursor = (SetCursorFn)(base + SET_CURSOR_RVA);
    GetToolbox = (ToolboxFn)(base + TOOLBOX_RVA);
    ScreenMatrix = (ScreenMatrixFn)(base + SCREEN_MATRIX_RVA);
    CursorEnabled = (DWORD)(base + CURSOR_ENABLED);
    WorldGlobal = (BYTE**)(base + WORLD_GLOBAL);
    TileScale = *(float*)(base + TILE_SCALE_RVA);
    HeightScale = *(float*)(base + HEIGHT_SCALE_RVA);

    rel = (LONG)((BYTE*)SetCursorHook - (call + 5));
    WriteCode(call + 1, &rel, sizeof(rel));
    LoadDrops();

    // Once per frame: watching the workers (another mod may have replaced the UI update already: it is called in turn)
    {
        void** mainSlot = (void**)(base + MAIN_VTABLE_RVA + MAIN_VT_UPDATE);
        OriginalMainUpdate = (MainUpdateFn)*mainSlot;
        WriteVtableSlot(mainSlot, (void*)MainUpdateHook);
    }

    iatSlot = (void**)(base + GET_PROC_ADDRESS_IAT);
    OriginalGetProcAddress = (GetProcAddressFn)*iatSlot;
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
