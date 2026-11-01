/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Normalized description of one R300 3D draw, built by the frontend from its register shadow and the draw packet.
 * Pure C, no Metal: the Metal executor and the test reference consume the same description. Anything the builder
 * cannot express exactly is a refusal with a reason, except the states counted in OPTICS_APPROX_* (approx below). */
#ifndef OPTICS_DRAW_H
#define OPTICS_DRAW_H
#include "foundation/shader_ir.h"
#include <stdint.h>
#include <stddef.h>
enum { OPTICS_DRAW_INPUTS=16, OPTICS_DRAW_VARYINGS=8, OPTICS_DRAW_TEXTURES=16, OPTICS_DRAW_MAX_VERTICES=65536 };   /* 16-bit index range (Doom 3 meshes exceed 4096) */
typedef struct {
    uint32_t offset,width,height,pitch;     /* GPU address, texels, row pitch in texels */
    uint32_t format;                        /* R300 TX format code (cache identity) */
    uint8_t layout,bpp;                     /* OPTICS_PIXEL_* linear layout and bytes per texel */
    uint8_t select[4];                      /* output R,G,B,A <- 0..3 component of the layout's RGBA decode, 4 zero, 5 one */
    uint8_t linear;                         /* magnification/minification filter */
    uint32_t sampler;                       /* renderer sampler code (PtR2Raster.sampler): address modes, filters, levels, anisotropy */
    uint8_t swap;                           /* TX_OFFSET[1:0] endian swap of the texture's memory (as VAP: 0 none, 1 16-bit, 2 32-bit, 3 halves) */
    uint8_t cube;                           /* six square faces, Face0..5 contiguous (R5xx Acceleration 4.6), level 0 */
    uint8_t levels;                         /* mip levels uploaded and sampled (1: level 0 only) */
    uint32_t depth;                         /* 3D: slices (TX_FORMAT0 [25:22] log2), stored one after another; 0: 2D */
    uint8_t floating;                       /* float formats 0x18..0x1d: 1 + (format - 0x18) (I16, I16A16, RGBA16, I32, I32A32, RGBA32); 0 none */
} OpticsTexture;
typedef struct { uint16_t offset; uint8_t components,format; } OpticsRawAttribute;
typedef struct {
    /* Colour target: R300 COLORFORMAT (draw.c), linear backing (tile bits are a GPU-private layout here). cb_select:
     * US_OUT_FMT_0 bits 15:8 (C0..C3 from A, R, G, B), read for the one- and two-byte formats. */
    uint32_t cb_offset,cb_pitch,width,height; uint8_t cb_bpp,cb_format,cb_select;   /* 1, 2, 4 or 8 bytes per pixel; hand-built draws: 0, 0 */
    int32_t scissor[4];                     /* x0,y0,x1,y1 exclusive, already clipped to the target */
    uint32_t vte; float viewport[6];        /* SE_VTE_CNTL and SE_VPORT_{X,Y,Z}{SCALE,OFFSET} */
    const PtR2ShaderIR *pvs,*us; PtR2ShaderInterface vi,fi;   /* decoded programs, owned by the builder's cache */
    uint64_t pvs_id,us_id;                  /* program identity (same id = same decoded program) */
    uint32_t pvs_constants,pvs_inputs;      /* PVS constant vectors and input vectors read (highest + 1) */
    /* Initialized PVS prefix: max(1,pvs_constants); unused tail is not part of a snapshot. US is fully initialized. */
    float pvs_constant[256][4],us_constant[32][4];
    uint32_t inputs;                        /* PVS input vectors per vertex */
    uint32_t vertices; float (*vertex)[OPTICS_DRAW_INPUTS][4];
    /* Optional immutable interleaved snapshot: 0 LE float, 1 BE float, 2 UNORM8.
     * It owns its bytes; no guest pointer survives the draw builder. */
    uint8_t *raw_vertex; uint16_t raw_stride; OpticsRawAttribute raw_attribute[16];
    uint16_t raw_select[16]; uint8_t raw_general; /* VAP routing, normalized raw_vertex.h formats. */
    uint32_t indices; uint32_t *index; uint8_t lines;   /* triangle list, or pairs of a line list when lines */
    uint32_t query;                         /* occlusion query counting this draw (set by the frontend), 0 none */
    uint32_t position_output,color_output,varyings;
    uint8_t pvs_output[OPTICS_DRAW_VARYINGS],us_input[OPTICS_DRAW_VARYINGS];
    /* RS component selection per varying: 0..3 output component, 4 zero, 5 one. */
    uint8_t component[OPTICS_DRAW_VARYINGS][4];   /* for sprite varyings: 0 s, 1 t, 4 zero, 5 one */
    uint32_t texture_mask; OpticsTexture texture[OPTICS_DRAW_TEXTURES];
    /* 0 off; else bit 31 | RGB src 3:0, dst 7:4, op 10:8 | alpha src 15:12, dst 19:16, op 22:20, in Metal's
     * MTLBlendFactor (Zero..OneMinusBlendAlpha) and MTLBlendOperation enum values; blend_color: RB3D_BLEND_COLOR, ARGB8888. */
    uint32_t blend,blend_color;
    /* Point sprites: each point is expanded into a window-space quad of point_size pixels around the transformed
     * vertex; corner[v] is 0..3 (bit 0 right, bit 1 lower). Varyings with stuffed[k] take the generated (s,t,0,1),
     * s/t interpolated from sprite_st (S0,T0 at corner 0, S1,T1 at corner 3). */
    uint32_t sprite; float point_size[2],sprite_st[4]; uint8_t *corner; uint8_t stuffed[OPTICS_DRAW_VARYINGS];
    /* Generated ("stuffed", GB_ENABLE) coordinates of lines and triangles, read through stuffed[k] like the sprite's
     * (component 0 s, 1 t). stuff 1, lines: s runs from line_s[0] to line_s[1] along each segment (GA_LINE_S0/S1), t is the
     * stipple accumulator, the major-axis length in 1/12 pixel since its reset (stipple_reset: 1 each line, 2 each packet),
     * times stipple_scale (GA_LINE_STIPPLE_CONFIG; Tiger's pattern texture has one row per bit); the executor's CPU vertex
     * stage fills them. stuff 2, triangles (polygon stipple): s,t are the window position shifted right by
     * stipple_shift (GA_TRIANGLE_STIPPLE x, y), both negated (Tiger's pattern texture holds the rows from the last and
     * each row's bits from the right), computed in the fragment function. */
    uint8_t stuff,stipple_reset,stipple_shift[2]; float stipple_scale,line_s[2];
    /* RB3D_CBLEND DISCARD_SRC_PIXELS [5:3], with or without blending: the source pixel is not written when its alpha (1),
     * colour (2) or both (3) are 0, or 1 (4, 5, 6), as 8-bit values (r300_reg.h; no Tiger draw seen using it yet). */
    uint8_t discard;
    /* TX_FILTER1 [1:0] CHROMA_KEY per unit: 1 FORCE kills the fragment, 2 BLEND samples transparent black, where the texel
     * equals TX_CHROMA_KEY (bytes X..W in the texel's component order, through the unit's selects; Tiger's stipple pattern
     * textures key their zero texels). Approximation: compared after filtering. */
    uint8_t chroma[OPTICS_DRAW_TEXTURES]; float chroma_key[OPTICS_DRAW_TEXTURES][4];
    uint32_t color_mask;                     /* RGBA bits 0..3 */
    /* Fast-clear values in force for this draw (RB3D_COLOR_CLEAR_VALUE ARGB, ZB_DEPTHCLEARVALUE as 0..1). */
    uint32_t color_clear; float depth_clear;
    uint32_t depth_clear_word,depth_clear_format; /* Preserve D24 stencil bits until fast-clear execution. */
    uint8_t alpha_test,alpha_func; float alpha_ref;   /* FG_ALPHA_FUNC: enable, NEVER..ALWAYS (R300 order), reference 0..1 */
    /* SU_CULL_MODE (cull bit 0 front, bit 1 back; front_cw its bit 2) and polygon offset in GL terms
     * (units of the 24-bit depth step, factor of the depth slope); 0 for lines and point sprites. */
    uint8_t cull,front_cw; float depth_bias,depth_slope;
    /* RB3D_AARESOLVE_CTL bit 0: the draw does not render; the pixels it covers are resolved from the colour buffer into
     * RB3D_AARESOLVE_OFFSET (pitch in pixels), bit 1 averaging with gamma 2.2, bit 2 averaging alpha too (else sample 0).
     * Without multisampling the resolve is a copy. resolve_ctl: the three bits. */
    uint32_t resolve,resolve_offset,resolve_pitch,resolve_ctl;
    /* Multisampling: GB_AA_CONFIG bit 0 on, 2:1 2, 3, 4 or 6 samples (aa_samples, 1 when off), at GB_MSPOS0/1 in
     * GB_TILE_CONFIG's subpixel units (Tiger's FSAA: a separate buffer drawn with samples, resolved into the drawable). */
    uint8_t aa_samples; uint32_t aa_config,aa_tile,aa_mspos[2];
    /* Depth/stencil, GPU-only (normalized D32Float/S8, not the R300 D24S8 bytes). Compare/ops use Metal's enum order. */
    uint32_t diag_vap_cntl_status,diag_array0,diag_out_fmt[2];   /* development probe only: VAP_CNTL_STATUS, GPU address of the first array, VAP_OUT_VTX_FMT_0/1 */
    uint32_t zb_offset; uint8_t depth_enable,depth_write,depth_compare,stencil_enable,stencil_ref,stencil_mask,stencil_writemask;
    uint8_t stencil_op[4],stencil_back[4];   /* compare, fail, depth-fail, pass */
    uint32_t approx;                         /* OPTICS_APPROX_*: state executed approximately (counted, not refused) */
    /* Fixed-function units around the programs (r300_reg.h names, read from what Tiger's driver programs).
     * cb_endian: RB3D_COLORPITCH0 [20:19], the colour buffer's memory endian swap (as TX_OFFSET's), kept by targets in
     * guest RAM (glReadPixels) where no CPU aperture swaps. */
    uint8_t cb_endian;
    /* FG fog: x = clamp(input * GA_FOG_SCALE + GA_FOG_OFFSET), colour.rgb = mix(colour.rgb, FG_FOG_COLOR, x). Tiger's
     * driver programs FG_FOG_BLEND linear and computes the exp/exp2 factor in the vertex program. The input is GB_SELECT's:
     * 0..3 the alpha of VAP colour 0..3 (PVS output fog_output), 4 the clip w, 5 the clip z. */
    uint8_t fog,fog_select,fog_output; float fog_color[3],fog_scale,fog_offset;
    uint8_t clip_planes; float clip_plane[6][4];   /* VAP_CLIP_CNTL [5:0] (0 with CLIP_DISABLE); clip-space planes, PVS vectors 1024+i */
    uint8_t flat;                                  /* varyings of colours that GA_COLOR_CONTROL shades FLAT (indices put the provoking vertex first) */
    uint8_t rop;                                   /* RB3D_ROPCNTL: 0 off, else 1 + ROP [11:8] (the GL logic op truth table) */
    uint8_t fill;                                  /* GA_POLY_MODE: 0 fill, 1 both faces as lines */
    uint8_t poly_front,poly_back;                  /* GA_POLY_MODE per face: 0 points, 1 lines, 2 filled (also when off); faces in
                                                    * different modes are drawn by the frontend in two passes, one per face */
    float line_width;                              /* GA_LINE_CNTL: line width in pixels */
    uint8_t point_size_output; float point_min,point_max;   /* VAP_OUT_VTX_FMT_0 bit 16: PVS output of each point's size (.x), 0 none; GA_POINT_MINMAX */
    float lod_bias[OPTICS_DRAW_TEXTURES];          /* TX_FILTER1 [12:3], in levels */
    /* Border address modes per unit, emulated in the fragment program over a zero border: bits 1:0 S, 3:2 T (1
     * CLAMP_TO_BORDER, 2 CLAMP, which also clamps the coordinate to 0..1), bit 4 linear filtering. border: TX_BORDER_COLOR
     * as the texel's RGBA after the unit's component selects. */
    uint8_t border_mode[OPTICS_DRAW_TEXTURES]; float border[OPTICS_DRAW_TEXTURES][4];
} OpticsDraw;
/* Texture state the executor approximates: MIP: levels it does not model (TXPITCH with levels, more levels than the
 * size has), BORDER: mirror-once to a border, ANISO: an unseen anisotropy field (above
 * 16:1), micro/macro-tiled texels read as linear; YUV601: YUV422 converted with BT.601 coefficients; WIDE: colour
 * targets other than ARGB8888 rendered at 8 bits per channel, 16-bit texture channels (W16Z16Y16X16, X16) held at 8. */
enum { OPTICS_APPROX_MIP=1, OPTICS_APPROX_BORDER=2, OPTICS_APPROX_ANISO=4, OPTICS_APPROX_TILED=8, OPTICS_APPROX_YUV601=16, OPTICS_APPROX_WIDE=32,
       OPTICS_APPROX_FIXED=64, OPTICS_APPROX_SIGNED=128 };   /* FIXED: a fixed-function state draw.h does not model (polygon points, fog
                                                              * inputs); SIGNED: texture components flagged signed, read as unsigned */
/* Vertex arrays from 3D_LOAD_VBPNTR: per array dword components, dword stride and GPU address. */
typedef struct { uint32_t count; struct { uint32_t components,stride; uint64_t offset; } array[16]; uint32_t local; } OpticsAos;   /* local: bit k, array k is in VRAM */
/* GPU-memory read for vertex/index fetch; returns nonzero on success. */
typedef int (*OpticsFetch)(void *,uint64_t gpu,void *,size_t);
/* reg: the frontend register file (REG_BYTES/4 words). payload: the LE-normalized packet3 words after the header. */
struct OpticsR300State;
int optics_draw_build(OpticsDraw *,const uint32_t *reg,const struct OpticsR300State *,uint8_t opcode,const uint32_t *payload,uint32_t words,
    const OpticsAos *,OpticsFetch,void *fetch_context,const char **reason);
void optics_draw_free(OpticsDraw *);
/* Valid vertex ordinal; decodes either representation for CPU fallback/debugging. */
void optics_draw_input(const OpticsDraw *,uint32_t vertex,float out[OPTICS_DRAW_INPUTS][4]);
int optics_draw_passthrough(unsigned outputs,PtR2ShaderIR *);
float optics_draw_depth_clear(uint32_t zb_depthclearvalue,uint32_t zb_format);
unsigned optics_draw_clear_mask(unsigned planes,uint32_t zb_format);
/* window: x,y in target pixels (top-left origin), z, and the clip w for perspective-correct interpolation. */
typedef struct { float window[4]; float varying[OPTICS_DRAW_VARYINGS][4]; } OpticsVertexOut;
int optics_draw_vertices(const OpticsDraw *,OpticsVertexOut *out,const char **reason);
/* Undo the texture's memory endian swap on a CPU copy of its bytes, before the layout decode (every 32-bit unit). */
void optics_texture_unswap(const OpticsTexture *,uint8_t *,size_t);
#endif
