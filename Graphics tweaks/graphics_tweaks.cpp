/*
 * Graphics Tweaks
 *
 * Fixes for the dynamic shadows and the terrain, sharper shadows and textures.
 * Settings are read from sfsf\graphics_tweaks.ini when the game starts.
 *
 * Shadow strength (ShadowType 2): the shadow map is drawn white where an object
 * casts a shadow, then blurred by averaging four samples of it, read through
 * texture stages 0 - 3. The pass never sets the stages' texture transform; the
 * terrain leaves stage 3 in projected mode (it looks the shadow map up that
 * way), so whenever the terrain happens to be the last to set it (after
 * selecting a building, after moving the camera, ...) the fourth sample reads
 * nothing and every shadow gets only three quarters of its strength. The pass
 * now runs with the stages' texture transform turned off.
 *
 * Missing casters (ShadowType 3): the shadow map holds the light space depth of
 * the casters, written by a pixel shader into all four channels. Alpha tested
 * objects (tree leaves, ...) keep the alpha test of the colour shadow map,
 * which then tests the depth instead: every leaf closer to the light than the
 * middle of the depth range is dropped. Which ones that are depends on where
 * the camera looks, as the depth range follows it. Leaves are cut out by the
 * shader itself, so the depth pass now draws without alpha test and blending.
 *
 * Shadow resolution: the shadow map (and the blur target of ShadowType 2) is
 * created at 1024 x 1024; the pass has the texel size in a number of constants.
 * Both follow the setting. The game's render targets end at 2048 x 2048: for
 * 4096 the game makes the map at 2048, and its texture and depth buffer are
 * swapped for ones of the full size.
 *
 * Shadow edges: the shadows' receivers take 16 samples of the map at fixed
 * offsets in texture coordinates (about 2.4 texels of a 1024 map), so a larger
 * map drew the same blurred edges, and each sample is all in or all out of the
 * shadow. The depth shadows' receivers (ShadowType 3) get a weighted grid of
 * filtered samples instead (see make_bilinear_filter), as wide as the setting;
 * the colour shadows' (ShadowType 2) offsets are scaled to the setting's number
 * of 1024 texels.
 *
 * Terrain detail: the detailed terrain textures are drawn as an overlay over a
 * square ahead of the camera. The overlay and the base mesh of the terrain have
 * fixed buffers, which overflow on wide screens: each failed build makes the
 * game shrink the square for good, and more of the screen gets the baked
 * texture. Both buffers get three times the room, still within the 16 bit
 * indices they use (REB_Mesh::Reconstruct refuses more than 65535 indices, but
 * the limit of 16 bit indices is on the vertex count). The square can then also
 * be made larger; a size that does not fit still shrinks to one that does.
 *
 * Grey ground after a map change: the terrain is kept from map to map, and on
 * a map change its texture slots get the new map's textures. Slots whose
 * material has no texture are skipped and keep the previous map's one, which is
 * then drawn over that ground (mud turns grey after going through a portal and
 * back). Those slots are now emptied, as on the first map.
 */

#include "../../src/api/sfsf.h"

#include <windows.h>
#include <d3d9.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

SpellforceSpellFramework *sfsf;
SFLog *logAPI;

/* ------------------------------------------------------------------------ */
/* Game addresses (offsets relative to the image base, like ASI::AddrOf)     */
/* ------------------------------------------------------------------------ */

// CDrwGameView::PreRender: updates the shadow map, once per frame
#define RVA_PRE_RENDER_SHADOWS  0x3a7795
#define RVA_UPDATE_SHADOW_MAP   0x48afc0
// CDrwShadowMaps::UpdateShadowMap: works out the shadow map's area at the start
// of the pass (a value of 0 skips the pass) ...
#define RVA_SHADOW_AREA_CALL    0x48b087
#define RVA_SHADOW_AREA         0x48a390
// ... and restores the render target at its end
#define RVA_SHADOW_DONE_CALL    0x48bacf
#define RVA_RESTORE_TARGET      0x1d0fa0
#define SHADOW_MAPS_MODE        0xc         // 1 = colour (ShadowType 2), 2 = depth (ShadowType 3)
#define SHADOW_MODE_DEPTH       2
#define RVA_DSP_DISPLAY_PTR     0x928f5c    // DSP_Display*
#define DSP_DISPLAY_DEVICE      0x338
#define DSP_TEXTURES            0x150708    // textures by id, 0x40 bytes each
#define DSP_TEXTURE_SIZE        0x40
#define TEXTURE_D3D             0x0         // IDirect3DBaseTexture9 (the one below, of a 2D texture)
#define TEXTURE_D3D_2D          0x4
#define TEXTURE_SURFACE         0x10        // render target: its surface
#define TEXTURE_SIZE_CLASS      0x2c        // 0 - 11: 1 - 2048 texels
#define DSP_DEPTH_SURFACES      0x1507c0    // the render targets' depth buffers, by size class
#define DSP_STAGE_TEXTURES      0x1506d0    // id of the texture set on each of the 8 stages
#define SHADOW_TEXTURE_IDS      {0x88, 0x89}    // shadow map, and the colour shadows' blur target
#define SIZE_CLASS_2048         11
#define DSP_PIXEL_SHADERS       0x15082c    // table of 0x80 pixel shaders {used, shader, ...}
#define DSP_PIXEL_SHADER_COUNT  0x80
#define DSP_PIXEL_SHADER_SIZE   0xc
#define DSP_PIXEL_SHADER_PTR    4

// Mesh renderer: applies the state set of a draw (DSP_Display::FUN_005d0a80)
#define RVA_APPLY_STATES        0x1d0a80
#define RVA_APPLY_STATES_CALL_1 0x55157b
#define RVA_APPLY_STATES_CALL_2 0x5515cb
#define STATE_SET_SIZE          0x82        // dwords
#define STATE_SET_ALPHA_TEST    (0x71 + 5)
#define STATE_SET_ALPHA_BLEND   (0x71 + 11)

// Shadow map creation (CDrwShadowMaps::FUN_0088a0a0): "push <level>" of the
// render target calls, level 10 = 1024 x 1024
static const uintptr_t RVA_SHADOW_LEVEL_PUSHES[] = {0x48a254, 0x48a281, 0x48a2b7, 0x48a2e5};
#define SHADOW_LEVEL_VANILLA    10
#define SHADOW_SIZE_VANILLA     1024
// Area: snaps the map to texels, "mulss xmm0, [1/1024]"
#define RVA_SHADOW_SNAP         0x48a6cc
#define SHADOW_SNAP_OPERAND     4
#define RVA_ONE_OVER_1024       0x793be0
// Pass: "mov [ebp - x], <float>" with values in texels of the 1024 map (blur
// offsets, half texel offsets, border strips)
static const uintptr_t RVA_TEXEL_CONSTANTS[] = {
    0x48b3b8, 0x48b3ce, 0x48b3e4, 0x48b3fa, 0x48b410, 0x48b426, 0x48b43c, 0x48b452, 0x48b468, 0x48b47e, 0x48b494,
    0x48b4b1, 0x48b4ce, 0x48b4eb, 0x48b508, 0x48b51f, 0x48b78b, 0x48b79b, 0x48b7ab, 0x48b7bb, 0x48b7f7, 0x48b807,
    0x48b817, 0x48b827, 0x48b85c, 0x48b86c, 0x48b87c, 0x48b88c, 0x48b8c1, 0x48b8d1, 0x48b8e1, 0x48b8f1, 0x48b940,
    0x48b950, 0x48b967, 0x48b99a, 0x48b9bd, 0x48b9ce, 0x48ba03, 0x48ba26, 0x48ba37, 0x48ba7e, 0x48ba90, 0x48baa0,
};
#define TEXEL_CONSTANT_OPCODE   0x85c7      // c7 85 <disp32> <imm32>
#define TEXEL_CONSTANT_IMM      6

// CDrwTerrain constructor: the detail texture overlay mesh. Its buffer of 0x536d
// vertices / 0xfa47 indices is cut into 0x71 chunks of 0xbd vertices / 0x237
// indices, which the texture layers take one after another.
#define RVA_OVERLAY_RECONSTRUCT_CALL 0x3ab176
#define RVA_REB_MESH_RECONSTRUCT     0x1f3640
#define RVA_OVERLAY_VERTICES_PUSH    0x3ab165   // push 0x536d
#define RVA_OVERLAY_INDICES_PUSH     0x3ab151   // push 0xfa47
#define RVA_OVERLAY_VERTEX_END       0x3ab1b8   // cmp edx, 0x536d
#define OVERLAY_VERTICES_VANILLA     0x536d
#define OVERLAY_INDICES_VANILLA      0xfa47
// The base mesh of the terrain: 20000 vertices, 60000 indices, kept under a
// budget of 17000 vertices
#define RVA_BASE_INDICES_PUSH        0x3ab113   // push 0xea60
#define RVA_BASE_VERTICES_PUSH       0x3ab11a   // push 0x4e20
#define BASE_VERTICES_VANILLA        20000
#define BASE_INDICES_VANILLA         60000
#define RVA_BASE_BUDGET_COMPARE      0x3ac3ef   // comiss xmm0, [17000.0]
#define RVA_BASE_BUDGET              0x7d09d0
// REB_Mesh::Reconstruct refuses more than 65535 indices ("cmp edi, 0x10000"); the
// limit of 16 bit indices is on the vertex count, which stays below it
#define RVA_MESH_INDEX_LIMIT         0x1f3668
#define TERRAIN_CAPACITY             3
// CDrwTerrain: size of the detail texturing square, set when a map loads
static const uintptr_t RVA_DETAIL_RADIUS_SETS[] = {0x3aaf6d, 0x3abbad};
#define DETAIL_RADIUS_VANILLA        14

// CDrwTerrainTexturer2 refresh (FUN_008a4220, flag 0x10 on a map change): call to
// the loading of the map's terrain textures, (fastcall texturer)
#define RVA_LOAD_TERRAIN_TEXTURES_CALL 0x4a4234
#define RVA_LOAD_TERRAIN_TEXTURES      0x4a34f0
#define TERRAIN_SLOTS                  0x3f
#define TEXTURER_BASE_TEXTURES         0x10      // per slot a shared pointer {texture, count}
#define TEXTURER_DETAIL_TEXTURES       0x208
#define TEXTURER_MESH                  0x4a70    // landscape_island_textures.msh: its materials
#define MESH_MATERIALS                 0x24
#define MESH_MATERIAL_COUNT            0x2c
#define MATERIAL_SIZE                  0xf0
#define MATERIAL_BASE_NAME             0x4c
#define MATERIAL_DETAIL_NAME           0x9c
#define RVA_MAP_PTR                    0x975d38  // the map being played
#define MAP_WORLD                      0x2c      // map: the world (CGdWorld)
#define WORLD_SLOT_MATERIALS           0x832302  // world: material of each slot (byte)
#define RVA_SHARED_PTR_DESTROY         0x3983d0  // fastcall (shared pointer)
#define RVA_SHARED_PTR_INIT            0x3d7620  // fastcall (shared pointer): empty

#define SHADOW_PASS_STAGES      4
#define SAMPLER_COUNT           8
#define SETTINGS_FILE           "sfsf\\graphics_tweaks.ini"

#define COUNT(a)                (sizeof(a) / sizeof((a)[0]))

typedef void (__thiscall *update_shadow_map_ptr)(void *maps, void *center, float half_size, float *camera,
                                                 float light);
typedef float (__thiscall *shadow_area_ptr)(void *maps, float *center, float half_size, float *camera,
                                            float *light, void *transform);
typedef int (__attribute__((fastcall)) *restore_target_ptr)(void *display);
typedef int (__thiscall *apply_states_ptr)(void *display, int *states);
typedef void (__thiscall *mesh_reconstruct_ptr)(void *mesh, int vertex_format, int vertices, int index_format,
                                                unsigned indices, unsigned subsets);
typedef void (__attribute__((fastcall)) *texturer_ptr)(void *texturer);

struct Settings
{
    int shadow_resolution;
    int terrain_detail;
    float shadow_softness;
};

static Settings s_settings = {2048, 22, 1.0f};

static uintptr_t s_base;
static update_shadow_map_ptr s_update_shadow_map;
static shadow_area_ptr s_shadow_area;
static restore_target_ptr s_restore_target;
static apply_states_ptr s_apply_states;
static mesh_reconstruct_ptr s_mesh_reconstruct;
static texturer_ptr s_load_terrain_textures;

// Device state changed for the shadow pass, restored at its end
static IDirect3DStateBlock9 *s_saved_state;
static bool s_depth_pass;

static float s_texel_size = 1.0f / SHADOW_SIZE_VANILLA;

static inline uintptr_t addr_of(uintptr_t rva)
{
    return s_base + rva;
}

static IDirect3DDevice9 *get_device()
{
    uint8_t *display = *(uint8_t **)addr_of(RVA_DSP_DISPLAY_PTR);
    return display ? *(IDirect3DDevice9 **)(display + DSP_DISPLAY_DEVICE) : NULL;
}

/* ------------------------------------------------------------------------ */
/* Shadow filter                                                             */
/* ------------------------------------------------------------------------ */

static bool s_upgrade_failed;

// Size of the shadow map in use
static int shadow_map_size()
{
    return s_upgrade_failed && s_settings.shadow_resolution > 2048 ? 2048 : s_settings.shadow_resolution;
}

// Pixel shaders already looked at (the replacements among them)
static void *s_known_shaders[512];
static int s_known_count;

static bool known_shader(void *shader)
{
    for (int i = 0; i < s_known_count; i++)
    {
        if (s_known_shaders[i] == shader)
        {
            return true;
        }
    }
    if (s_known_count == (int)COUNT(s_known_shaders))
    {
        s_known_count = 0;      // the game made and dropped many: look at them again
    }
    s_known_shaders[s_known_count++] = shader;
    return false;
}

// The shadow receivers take 16 samples of the shadow map around the point, at
// fixed offsets in texture coordinates ("def c10" - "def c25", the filter is
// recognised by "def c4, +-0.0625" and its first offset). The offsets are
// scaled by the factor; returns whether the code was such a filter.
static bool scale_shadow_filter(uint32_t *code, UINT size, float factor)
{
    UINT count = size / 4;
    if (count < 2 || code[0] != 0xffff0200)
    {
        return false;
    }
    float *offsets[16 * 4];
    int found = 0;
    bool weight = false, first = false;
    for (UINT i = 1; i < count && code[i] != 0x0000ffff;)
    {
        uint32_t token = code[i];
        if ((token & 0xffff) == 0xfffe)
        {
            i += 1 + ((token >> 16) & 0x7fff);
            continue;
        }
        UINT length = (token >> 24) & 0xf;
        if ((token & 0xffff) == 0x51 && length == 5 && i + 5 < count)
        {
            uint32_t reg = code[i + 1];
            int type = ((reg >> 28) & 7) | ((reg >> 8) & 0x18);
            int number = reg & 0x7ff;
            float *values = (float *)&code[i + 2];
            if (type == 2 && number == 4 && fabsf(fabsf(values[0]) - 0.0625f) < 1e-6f)
            {
                weight = true;
            }
            if (type == 2 && number == 10 && fabsf(values[0] + 0.000692f) < 1e-6f)
            {
                first = true;
            }
            if (type == 2 && number >= 10 && number <= 25 && found < (int)COUNT(offsets) - 4)
            {
                for (int k = 0; k < 4; k++)
                {
                    offsets[found++] = &values[k];
                }
            }
        }
        i += 1 + length;
    }
    if (!weight || !first)
    {
        return false;
    }
    for (int k = 0; k < found; k++)
    {
        *offsets[k] *= factor;
    }
    return true;
}

// A small assembler for shader model 2 tokens
enum
{
    REG_TEMP = 0,
    REG_CONST = 2,
    REG_TEXTURE = 3,
    REG_SAMPLER = 10,
};
#define SWIZZLE_XYZW    0xe4
#define SWIZZLE_X       0x00
#define SWIZZLE_Y       0x55
#define SWIZZLE_Z       0xaa
#define SWIZZLE_W       0xff

static uint32_t reg_bits(int type, int number)
{
    return 0x80000000u | ((uint32_t)(type & 7) << 28) | ((uint32_t)(type & 0x18) << 8) | (uint32_t)number;
}

static uint32_t dst(int type, int number, int mask = 0xf)
{
    return reg_bits(type, number) | ((uint32_t)mask << 16);
}

static uint32_t src(int type, int number, int swizzle = SWIZZLE_XYZW, bool negate = false)
{
    return reg_bits(type, number) | ((uint32_t)swizzle << 16) | (negate ? 1u << 24 : 0u);
}

struct Tokens
{
    uint32_t *code;
    UINT count, capacity;
    bool overflow;

    void put(uint32_t token)
    {
        if (count < capacity)
        {
            code[count++] = token;
        }
        else
        {
            overflow = true;
        }
    }
    void op(int opcode, uint32_t a, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0)
    {
        int length = d ? 4 : c ? 3 : b ? 2 : 1;
        put((uint32_t)opcode | ((uint32_t)length << 24));
        put(a);
        if (b) put(b);
        if (c) put(c);
        if (d) put(d);
    }
    void def(int number, float x, float y, float z, float w)
    {
        put(0x51 | (5u << 24));
        put(dst(REG_CONST, number));
        float values[4] = {x, y, z, w};
        for (int i = 0; i < 4; i++)
        {
            uint32_t bits;
            memcpy(&bits, &values[i], 4);
            put(bits);
        }
    }
};

enum
{
    OP_MOV = 1, OP_ADD = 2, OP_MAD = 4, OP_MUL = 5, OP_RCP = 6, OP_DP4 = 9, OP_FRC = 0x13, OP_TEXLD = 0x42,
    OP_DEF = 0x51, OP_CMP = 0x58,
};
#define PCF_TAPS_MAX     5

// The depth shadows' receivers (they compare each sample with the point's
// depth, "cmp"): each of the 16 samples is all in or all out of the shadow. They
// are replaced by a grid of filtered samples, each comparing the four texels
// around it and weighing them by where the point falls between them, the
// samples weighed in turn by a binomial kernel (3 x 3 at softness 1, 5 x 5 above,
// further apart at 3 and more). A caster's edge in the map moves a whole texel at
// a time as the sun turns; the more texels the kernel spreads over, the smaller
// each step of the shadow's edge. Needs more instructions than ps_2_0 has: the
// shader becomes ps_2_x. Returns whether the code was such a receiver.
static bool make_bilinear_filter(const uint32_t *code, UINT count, Tokens &out, float size, float softness)
{
    if (count < 2 || code[0] != 0xffff0200)
    {
        return false;
    }
    // the filter: from "rcp r11.w, tN.w" to the "add r5, ..." that sums it up
    UINT start = 0, end = 0, i = 1;
    int texcoord = -1, sampler = -1;
    bool compares = false, constants_after = false;
    while (i < count && code[i] != 0x0000ffff)
    {
        uint32_t token = code[i];
        if ((token & 0xffff) == 0xfffe)
        {
            i += 1 + ((token >> 16) & 0x7fff);
            continue;
        }
        UINT length = (token >> 24) & 0xf;
        int opcode = token & 0xffff;
        if (i + length >= count)
        {
            return false;
        }
        if (!start && opcode == OP_RCP && (code[i + 1] & 0x7ff) == 11 && ((code[i + 2] >> 28) & 7) == REG_TEXTURE)
        {
            start = i;
            texcoord = code[i + 2] & 0x7ff;
        }
        else if (start && !end)
        {
            if (opcode == OP_TEXLD && sampler < 0)
            {
                sampler = code[i + 3] & 0x7ff;
            }
            compares = compares || opcode == OP_CMP;
            if (opcode == OP_ADD && code[i + 1] == dst(REG_TEMP, 5))
            {
                end = i + 1 + length;
            }
        }
        else if (end)
        {
            // the code after the filter may read only the game's c0 - c2
            for (UINT k = 2; k <= length; k++)
            {
                uint32_t operand = code[i + k];
                int type = ((operand >> 28) & 7) | ((operand >> 8) & 0x18);
                constants_after = constants_after || (type == REG_CONST && (operand & 0x7ff) >= 3);
            }
        }
        i += 1 + length;
    }
    if (!start || !end || !compares || texcoord < 0 || sampler < 0 || constants_after)
    {
        return false;
    }

    int across = softness < 1.5f ? 3 : PCF_TAPS_MAX;
    int spacing = across == 3 ? 1 : (int)floorf(softness * 0.5f + 0.5f);
    static const float binomial[2][PCF_TAPS_MAX] = {{1, 2, 1}, {1, 4, 6, 4, 1}};
    const float *weights = binomial[across == 3 ? 0 : 1];
    float total = across == 3 ? 16.0f : 256.0f;
    float texel = 1.0f / size;

    out.put(0xffff0201);
    for (i = 1; i < start;)
    {
        uint32_t token = code[i];
        UINT length = (token & 0xffff) == 0xfffe ? ((token >> 16) & 0x7fff) : (token >> 24) & 0xf;
        bool dropped = (token & 0xffff) == OP_DEF && (code[i + 1] & 0x7ff) >= 3;
        for (UINT k = 0; k <= length && !dropped; k++)
        {
            out.put(code[i + k]);
        }
        i += 1 + length;
    }
    // c3: size, -0.5, texel, half a texel; c4: (texel, 0), 1, 0; c5: (0, texel);
    // c6...: each sample's offset and -weight (the game's sign)
    const int first_tap = 6;
    out.def(3, size, -0.5f, texel, 0.5f * texel);
    out.def(4, texel, 0.0f, 1.0f, 0.0f);
    out.def(5, 0.0f, texel, 0.0f, 0.0f);
    for (int tap = 0; tap < across * across; tap++)
    {
        int x = tap % across - across / 2, y = tap / across - across / 2;
        out.def(first_tap + tap, (float)(x * spacing) * texel, (float)(y * spacing) * texel,
                -weights[tap % across] * weights[tap / across] / total, 0.0f);
    }
    const int t = texcoord;
    // the point in the map, the texel below and left of it, and the weights
    // of the four texels around it
    out.op(OP_RCP, dst(REG_TEMP, 11, 8), src(REG_TEXTURE, t, SWIZZLE_W));
    out.op(OP_MUL, dst(REG_TEMP, 11, 3), src(REG_TEMP, 11, SWIZZLE_W), src(REG_TEXTURE, t));
    out.op(OP_MAD, dst(REG_TEMP, 6, 3), src(REG_TEMP, 11), src(REG_CONST, 3, SWIZZLE_X), src(REG_CONST, 3, SWIZZLE_Y));
    out.op(OP_FRC, dst(REG_TEMP, 7, 3), src(REG_TEMP, 6));
    out.op(OP_ADD, dst(REG_TEMP, 6, 3), src(REG_TEMP, 6), src(REG_TEMP, 7, SWIZZLE_XYZW, true));
    out.op(OP_MAD, dst(REG_TEMP, 6, 3), src(REG_TEMP, 6), src(REG_CONST, 3, SWIZZLE_Z), src(REG_CONST, 3, SWIZZLE_W));
    out.op(OP_ADD, dst(REG_TEMP, 8, 3), src(REG_CONST, 4, SWIZZLE_Z), src(REG_TEMP, 7, SWIZZLE_XYZW, true));
    out.op(OP_MUL, dst(REG_TEMP, 9, 1), src(REG_TEMP, 8, SWIZZLE_X), src(REG_TEMP, 8, SWIZZLE_Y));
    out.op(OP_MUL, dst(REG_TEMP, 9, 2), src(REG_TEMP, 7, SWIZZLE_X), src(REG_TEMP, 8, SWIZZLE_Y));
    out.op(OP_MUL, dst(REG_TEMP, 9, 4), src(REG_TEMP, 8, SWIZZLE_X), src(REG_TEMP, 7, SWIZZLE_Y));
    out.op(OP_MUL, dst(REG_TEMP, 9, 8), src(REG_TEMP, 7, SWIZZLE_X), src(REG_TEMP, 7, SWIZZLE_Y));
    out.op(OP_MOV, dst(REG_TEMP, 10), src(REG_CONST, 4, SWIZZLE_Y));
    for (int tap = 0; tap < across * across; tap++)
    {
        out.op(OP_ADD, dst(REG_TEMP, 0, 3), src(REG_TEMP, 6), src(REG_CONST, first_tap + tap));
        out.op(OP_ADD, dst(REG_TEMP, 1, 3), src(REG_TEMP, 0), src(REG_CONST, 4));
        out.op(OP_ADD, dst(REG_TEMP, 2, 3), src(REG_TEMP, 0), src(REG_CONST, 5));
        out.op(OP_ADD, dst(REG_TEMP, 3, 3), src(REG_TEMP, 0), src(REG_CONST, 3, SWIZZLE_Z));
        for (int k = 0; k < 4; k++)
        {
            out.op(OP_TEXLD, dst(REG_TEMP, k), src(REG_TEMP, k), src(REG_SAMPLER, sampler));
        }
        for (int k = 0; k < 4; k++)     // in the shadow where the point is deeper
        {
            out.op(OP_ADD, dst(REG_TEMP, 4, 1 << k), src(REG_TEXTURE, t, SWIZZLE_Z),
                   src(REG_TEMP, k, SWIZZLE_X, true));
        }
        out.op(OP_CMP, dst(REG_TEMP, 4), src(REG_TEMP, 4), src(REG_CONST, 4, SWIZZLE_Z),
               src(REG_CONST, 4, SWIZZLE_Y));
        out.op(OP_DP4, dst(REG_TEMP, 4, 1), src(REG_TEMP, 4), src(REG_TEMP, 9));
        out.op(OP_MAD, dst(REG_TEMP, 10), src(REG_TEMP, 4, SWIZZLE_X), src(REG_CONST, first_tap + tap, SWIZZLE_Z),
               src(REG_TEMP, 10));
    }
    out.op(OP_MOV, dst(REG_TEMP, 5), src(REG_TEMP, 10));
    for (i = end; i < count; i++)
    {
        out.put(code[i]);
        if (code[i] == 0x0000ffff)
        {
            break;
        }
    }
    return !out.overflow;
}

// Swaps the game's shadow receivers (in the display's table of pixel shaders)
// for copies with the filter scaled. Runs every frame: shaders the game makes
// later (or again, after a device reset) are swapped too.
static void update_shadow_filter(IDirect3DDevice9 *device)
{
    float factor = s_settings.shadow_softness * SHADOW_SIZE_VANILLA / s_settings.shadow_resolution;
    uint8_t *display = *(uint8_t **)addr_of(RVA_DSP_DISPLAY_PTR);
    uint8_t *table = display ? *(uint8_t **)(display + DSP_PIXEL_SHADERS) : NULL;
    if (!table)
    {
        return;
    }
    for (int i = 0; i < DSP_PIXEL_SHADER_COUNT; i++)
    {
        uint8_t *entry = table + i * DSP_PIXEL_SHADER_SIZE;
        IDirect3DPixelShader9 **slot = (IDirect3DPixelShader9 **)(entry + DSP_PIXEL_SHADER_PTR);
        if (!*(int *)entry || !*slot || known_shader(*slot))
        {
            continue;
        }
        UINT size = 0;
        if (FAILED((*slot)->GetFunction(NULL, &size)) || size == 0 || size > 0x4000)
        {
            continue;
        }
        uint32_t code[0x1000];
        static uint32_t made[0x2000];
        Tokens out = {made, 0, (UINT)COUNT(made), false};
        if (FAILED((*slot)->GetFunction(code, &size)))
        {
            continue;
        }
        const uint32_t *replacement_code = code;
        const char *kind = "scaled";
        if (make_bilinear_filter(code, size / 4, out, (float)shadow_map_size(), s_settings.shadow_softness))
        {
            replacement_code = made;
            kind = "filtered";
        }
        else if (factor == 1.0f || !scale_shadow_filter(code, size, factor))
        {
            continue;
        }
        IDirect3DPixelShader9 *replacement = NULL;
        if (FAILED(device->CreatePixelShader((const DWORD *)replacement_code, &replacement)) || !replacement)
        {
            logAPI->logError("[graphics] could not make the shadow filter of pixel shader %d", i + 1);
            continue;
        }
        known_shader(replacement);
        (*slot)->Release();
        *slot = replacement;
        logAPI->logInfo("[graphics] shadow filter of pixel shader %d %s", i + 1, kind);
    }
}

/* ------------------------------------------------------------------------ */
/* Shadow maps larger than 2048                                              */
/* ------------------------------------------------------------------------ */

// The game makes render targets of up to 2048 x 2048 only. For a larger shadow
// map, the game makes it at 2048 and its texture, surface and depth buffer
// (shared by the render targets of that size, the shadow maps only) are
// swapped for ones of the full size. The game sets no viewport of its own, so
// it draws the whole map. Runs every frame: the game makes them again after a
// device reset.
static void upgrade_shadow_targets(IDirect3DDevice9 *device)
{
    UINT size = (UINT)s_settings.shadow_resolution;
    uint8_t *display = *(uint8_t **)addr_of(RVA_DSP_DISPLAY_PTR);
    uint8_t *textures = display ? *(uint8_t **)(display + DSP_TEXTURES) : NULL;
    if (size <= 2048 || !textures || s_upgrade_failed)
    {
        return;
    }
    static const int ids[] = SHADOW_TEXTURE_IDS;
    for (size_t i = 0; i < COUNT(ids); i++)
    {
        uint8_t *entry = textures + ids[i] * DSP_TEXTURE_SIZE;
        IDirect3DTexture9 *texture = *(IDirect3DTexture9 **)(entry + TEXTURE_D3D_2D);
        IDirect3DSurface9 *surface = *(IDirect3DSurface9 **)(entry + TEXTURE_SURFACE);
        D3DSURFACE_DESC desc;
        if (!texture || !surface || *(int *)(entry + TEXTURE_SIZE_CLASS) != SIZE_CLASS_2048 ||
            FAILED(texture->GetLevelDesc(0, &desc)) || desc.Width >= size)
        {
            continue;
        }
        IDirect3DTexture9 *larger = NULL;
        IDirect3DSurface9 *larger_surface = NULL;
        if (FAILED(device->CreateTexture(size, size, 1, D3DUSAGE_RENDERTARGET, desc.Format, D3DPOOL_DEFAULT, &larger,
                                         NULL)) ||
            FAILED(larger->GetSurfaceLevel(0, &larger_surface)))
        {
            if (larger)
            {
                larger->Release();
            }
            logAPI->logError("[graphics] could not make a %u x %u shadow map; shadows look wrong, lower "
                             "shadow_resolution", size, size);
            s_upgrade_failed = true;
            return;
        }
        surface->Release();
        texture->Release();
        *(IDirect3DTexture9 **)(entry + TEXTURE_D3D) = larger;
        *(IDirect3DTexture9 **)(entry + TEXTURE_D3D_2D) = larger;
        *(IDirect3DSurface9 **)(entry + TEXTURE_SURFACE) = larger_surface;
        // the game skips setting a texture it thinks is set: unset it
        int *stages = (int *)(display + DSP_STAGE_TEXTURES);
        for (DWORD stage = 0; stage < SAMPLER_COUNT; stage++)
        {
            if (stages[stage] == ids[i])
            {
                device->SetTexture(stage, NULL);
                stages[stage] = -1;
            }
        }
        logAPI->logInfo("[graphics] shadow map %x: %u x %u", ids[i], size, size);
    }
    IDirect3DSurface9 **depth = (IDirect3DSurface9 **)(display + DSP_DEPTH_SURFACES + SIZE_CLASS_2048 * 4);
    D3DSURFACE_DESC desc;
    if ((uintptr_t)*depth > 1 && SUCCEEDED((*depth)->GetDesc(&desc)) && desc.Width < size)
    {
        IDirect3DSurface9 *larger = NULL;
        if (FAILED(device->CreateDepthStencilSurface(size, size, desc.Format, D3DMULTISAMPLE_NONE, 0, TRUE, &larger,
                                                     NULL)))
        {
            logAPI->logError("[graphics] could not make a %u x %u depth buffer for the shadow map", size, size);
            s_upgrade_failed = true;
            return;
        }
        (*depth)->Release();
        *depth = larger;
    }
}

/* ------------------------------------------------------------------------ */
/* Every frame                                                               */
/* ------------------------------------------------------------------------ */

// Runs in place of the shadow map update at the start of every frame: the
// shadows' pixel shaders and render targets are swapped here (they are made
// again after a device reset).
static void __thiscall update_shadow_map_hook(void *maps, void *center, float half_size, float *camera, float light)
{
    IDirect3DDevice9 *device = get_device();
    if (device)
    {
        update_shadow_filter(device);
        upgrade_shadow_targets(device);
    }
    s_update_shadow_map(maps, center, half_size, camera, light);
}

/* ------------------------------------------------------------------------ */
/* Shadow pass                                                               */
/* ------------------------------------------------------------------------ */

static void set_pass_state(IDirect3DDevice9 *device)
{
    for (DWORD stage = 0; stage < SHADOW_PASS_STAGES; stage++)
    {
        device->SetTextureStageState(stage, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    }
}

// The device is a pure device, which cannot report its state, so the state is
// saved in a state block. Recording the changes makes a block of just these
// states; it is captured here and applied again when the pass ends.
static void begin_pass()
{
    IDirect3DDevice9 *device = get_device();
    if (!device || s_saved_state || FAILED(device->BeginStateBlock()))
    {
        return;
    }
    set_pass_state(device);     // recorded, not applied
    if (FAILED(device->EndStateBlock(&s_saved_state)) || !s_saved_state)
    {
        s_saved_state = NULL;
        return;
    }
    s_saved_state->Capture();
    set_pass_state(device);
}

static void end_pass()
{
    s_depth_pass = false;
    if (s_saved_state)
    {
        s_saved_state->Apply();
        s_saved_state->Release();
        s_saved_state = NULL;
    }
}

static float __thiscall shadow_area_hook(void *maps, float *center, float half_size, float *camera, float *light,
                                         void *transform)
{
    float area = s_shadow_area(maps, center, half_size, camera, light, transform);
    if (area != 0.0f)
    {
        s_depth_pass = *(int *)((uint8_t *)maps + SHADOW_MAPS_MODE) == SHADOW_MODE_DEPTH;
        begin_pass();
    }
    return area;
}

static int __attribute__((fastcall)) shadow_done_hook(void *display)
{
    end_pass();
    return s_restore_target(display);
}

// The depth pass draws without alpha test and blending: the alpha written by
// its shaders is the depth (see the top of the file).
static int __thiscall apply_states_hook(void *display, int *states)
{
    if (!s_depth_pass || (!states[STATE_SET_ALPHA_TEST] && !states[STATE_SET_ALPHA_BLEND]))
    {
        return s_apply_states(display, states);
    }
    int copy[STATE_SET_SIZE];
    memcpy(copy, states, sizeof(copy));
    copy[STATE_SET_ALPHA_TEST] = 0;
    copy[STATE_SET_ALPHA_BLEND] = 0;
    return s_apply_states(display, copy);
}

/* ------------------------------------------------------------------------ */
/* Terrain detail                                                            */
/* ------------------------------------------------------------------------ */

// The overlay mesh is cut into TERRAIN_CAPACITY times as many chunks of the same
// size (the end of the loop filling in their offsets is patched to match)
static void __thiscall overlay_reconstruct_hook(void *mesh, int vertex_format, int vertices, int index_format,
                                                unsigned indices, unsigned subsets)
{
    s_mesh_reconstruct(mesh, vertex_format, vertices, index_format, indices, subsets * TERRAIN_CAPACITY);
}

/* ------------------------------------------------------------------------ */
/* Terrain textures after a map change                                       */
/* ------------------------------------------------------------------------ */

// The terrain is kept from map to map; on a map change the texturer loads the
// textures of the new map's texture slots. A slot whose material has no texture
// (no detail texture, mostly) is skipped and keeps the previous map's texture,
// which the terrain then draws over that ground: grey patches. Such slots are
// emptied, as they are on the first map.
static void __attribute__((fastcall)) load_terrain_textures_hook(void *texturer)
{
    s_load_terrain_textures(texturer);

    uint8_t *map = *(uint8_t **)addr_of(RVA_MAP_PTR);
    uint8_t *world = map ? *(uint8_t **)(map + MAP_WORLD) : NULL;
    uint8_t **mesh = *(uint8_t ***)((uint8_t *)texturer + TEXTURER_MESH);
    uint8_t *display_mesh = mesh ? *mesh : NULL;
    if (!world || !display_mesh)
    {
        return;
    }
    uint8_t *materials = *(uint8_t **)(display_mesh + MESH_MATERIALS);
    int count = *(int *)(display_mesh + MESH_MATERIAL_COUNT);
    texturer_ptr destroy = (texturer_ptr)addr_of(RVA_SHARED_PTR_DESTROY);
    texturer_ptr init = (texturer_ptr)addr_of(RVA_SHARED_PTR_INIT);
    int emptied = 0;
    for (int slot = 0; slot < TERRAIN_SLOTS; slot++)
    {
        int material = world[WORLD_SLOT_MATERIALS + slot];
        if (!materials || material >= count)
        {
            continue;
        }
        const char *names[2] = {(const char *)materials + material * MATERIAL_SIZE + MATERIAL_BASE_NAME,
                                (const char *)materials + material * MATERIAL_SIZE + MATERIAL_DETAIL_NAME};
        int offsets[2] = {TEXTURER_BASE_TEXTURES, TEXTURER_DETAIL_TEXTURES};
        for (int i = 0; i < 2; i++)
        {
            void **texture = (void **)((uint8_t *)texturer + offsets[i] + slot * 8);
            if (names[i][0] == '\0' && texture[0])
            {
                destroy(texture);
                init(texture);
                emptied++;
            }
        }
    }
    if (emptied)
    {
        logAPI->logInfo("[graphics] map change: %d terrain texture slots of the previous map emptied", emptied);
    }
}

/* ------------------------------------------------------------------------ */
/* Settings                                                                  */
/* ------------------------------------------------------------------------ */

static void write_settings()
{
    FILE *f = fopen(SETTINGS_FILE, "w");
    if (!f)
    {
        return;
    }
    fprintf(f, "# Graphics Tweaks; read when the game starts\n");
    fprintf(f, "# Shadow map resolution: 1024 (the game's own), 2048, 4096 or 8192\n");
    fprintf(f, "shadow_resolution = %d\n", s_settings.shadow_resolution);
    fprintf(f, "# Softness of the shadows' edges, in texels of the shadow map: 1 (the sharpest) to 8; 2 and\n");
    fprintf(f, "# up use a wider filter that moves the edges in smaller steps as the sun turns\n");
    fprintf(f, "shadow_softness = %.2f\n", s_settings.shadow_softness);
    fprintf(f, "# Size of the area with detailed terrain textures: 14 (the game's own) to 24; larger sizes\n");
    fprintf(f, "# shrink to what fits\n");
    fprintf(f, "terrain_detail = %d\n", s_settings.terrain_detail);
    fclose(f);
}

static void load_settings()
{
    FILE *f = fopen(SETTINGS_FILE, "r");
    if (!f)
    {
        write_settings();
        return;
    }
    char line[128];
    int value;
    float number;
    int found = 0;
    while (fgets(line, sizeof(line), f))
    {
        if (sscanf(line, " shadow_resolution = %d", &value) == 1)
        {
            s_settings.shadow_resolution = value;
            found |= 1;
        }
        else if (sscanf(line, " shadow_softness = %f", &number) == 1)
        {
            s_settings.shadow_softness = number;
            found |= 16;
        }
        else if (sscanf(line, " terrain_texture = %d", &value) == 1)
        {
            found |= 32;
        }
        else if (sscanf(line, " anisotropy = %d", &value) == 1)
        {
            found |= 32;
        }
        else if (sscanf(line, " terrain_detail = %d", &value) == 1)
        {
            s_settings.terrain_detail = value;
            found |= 8;
        }
    }
    fclose(f);
    if (s_settings.shadow_resolution != 1024 && s_settings.shadow_resolution != 2048 &&
        s_settings.shadow_resolution != 4096 && s_settings.shadow_resolution != 8192)
    {
        logAPI->logWarning("[graphics] shadow_resolution must be 1024, 2048, 4096 or 8192; using 2048");
        s_settings.shadow_resolution = 2048;
    }
    if (s_settings.shadow_softness < 1.0f || s_settings.shadow_softness > 8.0f)
    {
        logAPI->logWarning("[graphics] shadow_softness must be 1 to 8; using 1");
        s_settings.shadow_softness = 1.0f;
    }
    if (s_settings.terrain_detail < 4 || s_settings.terrain_detail > 64)
    {
        logAPI->logWarning("[graphics] terrain_detail must be 4 to 64; using 22");
        s_settings.terrain_detail = 22;
    }
    if (found != 25)
    {
        write_settings();   // brings a file from an older version up to date
    }
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

// Redirects "call rel32" at rva and returns the previous target through original.
// Another mod may already have redirected the call; its target is chained then.
static bool hook_call(uintptr_t rva, void *replacement, void **original, uintptr_t vanilla_rva)
{
    uintptr_t at = addr_of(rva);
    if (*(uint8_t *)at != 0xe8)
    {
        logAPI->logError("[graphics] no call at %08x; hook skipped", (unsigned)at);
        return false;
    }
    uintptr_t target = at + 5 + *(int32_t *)(at + 1);
    if (target != addr_of(vanilla_rva))
    {
        logAPI->logInfo("[graphics] call at %08x already redirected by another mod; chaining it", (unsigned)at);
    }
    *original = (void *)target;
    int32_t rel = (int32_t)((uintptr_t)replacement - (at + 5));
    return write_code(at + 1, &rel, sizeof(rel));
}

// Rescales the shadow map and the pass to size x size texels
static bool set_shadow_resolution(int size)
{
    if (size == SHADOW_SIZE_VANILLA)
    {
        return true;
    }
    // Check everything first: a half patched pass would be worse than none
    bool ok = *(uint32_t *)(addr_of(RVA_SHADOW_SNAP) + SHADOW_SNAP_OPERAND) == addr_of(RVA_ONE_OVER_1024);
    for (size_t i = 0; i < COUNT(RVA_SHADOW_LEVEL_PUSHES); i++)
    {
        const uint8_t *push = (const uint8_t *)addr_of(RVA_SHADOW_LEVEL_PUSHES[i]);
        ok = ok && push[0] == 0x6a && push[1] == SHADOW_LEVEL_VANILLA;
    }
    for (size_t i = 0; i < COUNT(RVA_TEXEL_CONSTANTS); i++)
    {
        ok = ok && *(uint16_t *)addr_of(RVA_TEXEL_CONSTANTS[i]) == TEXEL_CONSTANT_OPCODE;
    }
    if (!ok)
    {
        logAPI->logError("[graphics] unexpected shadow map code; resolution left at %d", SHADOW_SIZE_VANILLA);
        return false;
    }

    // The game's sizes of render targets end at 2048 (level 11); a larger shadow
    // map is swapped in for its 2048 one (see upgrade_shadow_targets)
    uint8_t level = SHADOW_LEVEL_VANILLA;
    while ((SHADOW_SIZE_VANILLA << (level - SHADOW_LEVEL_VANILLA)) < size && level < SIZE_CLASS_2048)
    {
        level++;
    }
    for (size_t i = 0; i < COUNT(RVA_SHADOW_LEVEL_PUSHES); i++)
    {
        write_code(addr_of(RVA_SHADOW_LEVEL_PUSHES[i]) + 1, &level, 1);
    }
    s_texel_size = 1.0f / size;
    uint32_t snap = (uint32_t)(uintptr_t)&s_texel_size;
    write_code(addr_of(RVA_SHADOW_SNAP) + SHADOW_SNAP_OPERAND, &snap, sizeof(snap));
    // The values are a number of texels (near 0) or 1 plus or minus a number of texels
    for (size_t i = 0; i < COUNT(RVA_TEXEL_CONSTANTS); i++)
    {
        uintptr_t at = addr_of(RVA_TEXEL_CONSTANTS[i]) + TEXEL_CONSTANT_IMM;
        float value = *(float *)at;
        float base = fabsf(value) < 0.5f ? 0.0f : 1.0f;
        float scaled = base + (value - base) * SHADOW_SIZE_VANILLA / size;
        write_code(at, &scaled, sizeof(scaled));
    }
    return true;
}

static float s_base_budget;
static float s_detail_radius = DETAIL_RADIUS_VANILLA;

static bool patch_push(uintptr_t rva, uint32_t expected, uint32_t value)
{
    uintptr_t at = addr_of(rva);
    if (*(uint8_t *)at != 0x68 || *(uint32_t *)(at + 1) != expected)
    {
        return false;
    }
    return write_code(at + 1, &value, sizeof(value));
}

// More room in both terrain meshes, so that a larger area gets the detail textures
static bool set_terrain_detail()
{
    bool ok = *(uint8_t *)addr_of(RVA_OVERLAY_VERTICES_PUSH) == 0x68 &&
              *(uint32_t *)(addr_of(RVA_OVERLAY_VERTICES_PUSH) + 1) == OVERLAY_VERTICES_VANILLA &&
              *(uint32_t *)(addr_of(RVA_OVERLAY_INDICES_PUSH) + 1) == OVERLAY_INDICES_VANILLA &&
              *(uint32_t *)addr_of(RVA_OVERLAY_VERTEX_END) == OVERLAY_VERTICES_VANILLA &&
              *(uint32_t *)(addr_of(RVA_BASE_VERTICES_PUSH) + 1) == BASE_VERTICES_VANILLA &&
              *(uint32_t *)(addr_of(RVA_BASE_INDICES_PUSH) + 1) == BASE_INDICES_VANILLA &&
              *(uint32_t *)(addr_of(RVA_BASE_BUDGET_COMPARE) + 3) == addr_of(RVA_BASE_BUDGET) &&
              *(uint32_t *)addr_of(RVA_MESH_INDEX_LIMIT) == 0x10000;
    for (size_t i = 0; i < COUNT(RVA_DETAIL_RADIUS_SETS); i++)
    {
        ok = ok && *(float *)addr_of(RVA_DETAIL_RADIUS_SETS[i]) == (float)DETAIL_RADIUS_VANILLA;
    }
    if (!ok)
    {
        logAPI->logError("[graphics] unexpected terrain code; terrain detail left as it is");
        return false;
    }
    if (!hook_call(RVA_OVERLAY_RECONSTRUCT_CALL, (void *)&overlay_reconstruct_hook, (void **)&s_mesh_reconstruct,
                   RVA_REB_MESH_RECONSTRUCT))
    {
        return false;
    }
    uint32_t limit = 0x7fffffff;
    write_code(addr_of(RVA_MESH_INDEX_LIMIT), &limit, sizeof(limit));
    patch_push(RVA_OVERLAY_VERTICES_PUSH, OVERLAY_VERTICES_VANILLA, OVERLAY_VERTICES_VANILLA * TERRAIN_CAPACITY);
    patch_push(RVA_OVERLAY_INDICES_PUSH, OVERLAY_INDICES_VANILLA, OVERLAY_INDICES_VANILLA * TERRAIN_CAPACITY);
    uint32_t end = OVERLAY_VERTICES_VANILLA * TERRAIN_CAPACITY;
    write_code(addr_of(RVA_OVERLAY_VERTEX_END), &end, sizeof(end));
    patch_push(RVA_BASE_VERTICES_PUSH, BASE_VERTICES_VANILLA, BASE_VERTICES_VANILLA * TERRAIN_CAPACITY);
    patch_push(RVA_BASE_INDICES_PUSH, BASE_INDICES_VANILLA, BASE_INDICES_VANILLA * TERRAIN_CAPACITY);
    s_base_budget = *(float *)addr_of(RVA_BASE_BUDGET) * TERRAIN_CAPACITY;
    uint32_t budget = (uint32_t)(uintptr_t)&s_base_budget;
    write_code(addr_of(RVA_BASE_BUDGET_COMPARE) + 3, &budget, sizeof(budget));
    s_detail_radius = (float)s_settings.terrain_detail;
    for (size_t i = 0; i < COUNT(RVA_DETAIL_RADIUS_SETS); i++)
    {
        write_code(addr_of(RVA_DETAIL_RADIUS_SETS[i]), &s_detail_radius, sizeof(s_detail_radius));
    }
    return true;
}

extern "C" __declspec(dllexport) void InitModule(SpellforceSpellFramework *framework)
{
    sfsf = framework;
    logAPI = sfsf->logAPI;
    s_base = (uintptr_t)GetModuleHandleA(NULL);
    load_settings();

    // Both or neither: a pass that starts with the fix must also end with it
    bool strength = hook_call(RVA_SHADOW_DONE_CALL, (void *)&shadow_done_hook, (void **)&s_restore_target,
                              RVA_RESTORE_TARGET) &&
                    hook_call(RVA_SHADOW_AREA_CALL, (void *)&shadow_area_hook, (void **)&s_shadow_area,
                              RVA_SHADOW_AREA);
    apply_states_ptr second;
    bool casters = strength &&
                   hook_call(RVA_APPLY_STATES_CALL_1, (void *)&apply_states_hook, (void **)&s_apply_states,
                             RVA_APPLY_STATES) &&
                   hook_call(RVA_APPLY_STATES_CALL_2, (void *)&apply_states_hook, (void **)&second,
                             RVA_APPLY_STATES);
    bool shadow_size = set_shadow_resolution(s_settings.shadow_resolution);
    bool detail = set_terrain_detail();
    bool frame = hook_call(RVA_PRE_RENDER_SHADOWS, (void *)&update_shadow_map_hook,
                                (void **)&s_update_shadow_map, RVA_UPDATE_SHADOW_MAP);
    bool map_change = hook_call(RVA_LOAD_TERRAIN_TEXTURES_CALL, (void *)&load_terrain_textures_hook,
                                (void **)&s_load_terrain_textures, RVA_LOAD_TERRAIN_TEXTURES);
    logAPI->logInfo("[graphics] shadow strength %s, depth casters %s, shadow resolution %d %s, frame hook %s, terrain detail %d %s, map change textures %s, "
                    "shadow softness %.2f",
                    strength ? "ok" : "FAILED", casters ? "ok" : "FAILED", s_settings.shadow_resolution,
                    shadow_size ? "ok" : "FAILED",
                    frame ? "ok" : "FAILED", s_settings.terrain_detail,
                    detail ? "ok" : "FAILED", map_change ? "ok" : "FAILED", s_settings.shadow_softness);
}

extern "C" __declspec(dllexport) SFMod *RegisterMod(SpellforceSpellFramework *framework)
{
    return framework->createModInfo("Graphics Tweaks", "1.0.0", "Angry Catster",
                                    "Shadow and terrain fixes, sharper shadows and textures.");
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    return TRUE;
}
