/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_SHADER_IR_H
#define PT_R2_SHADER_IR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* This is a shader compiler, not the RS/VAP/texture-format executor. Inputs
 * are decoded float vectors in architectural register order. No guest
 * pointer, allocation, process-global cache or device state is retained. */
#define PT_R2_SHADER_WORDS 1026u
#define PT_R2_SHADER_STEPS 384u
#define PT_R2_SHADER_REGS 32u
#define PT_R2_SHADER_CONSTS 256u
#define PT_R2_SHADER_ABI 1u

typedef enum { PT_R2_SHADER_US = 1, PT_R2_SHADER_PVS = 2 } PtR2ShaderStage;
typedef enum {
    PT_R2_SHADER_FP32_SEPARATE = 1,
    /* Explicit experimental GPU contract: subnormal arithmetic operands -> +0;
     * all zero/subnormal arithmetic results -> +0. Bitwise moves/signs keep
     * their bits. Device acceptance is required; neither Metal nor physical
     * R300 reset/FP24 is claimed to guarantee this entire contract. */
    PT_R2_SHADER_FP32_FTZ0 = 2
} PtR2ShaderPrecision;
typedef enum {
    PT_R2_SHADER_OK, PT_R2_SHADER_ARGUMENT, PT_R2_SHADER_LENGTH,
    PT_R2_SHADER_ENCODING, PT_R2_SHADER_UNSUPPORTED, PT_R2_SHADER_INTERFACE,
    PT_R2_SHADER_BUFFER, PT_R2_SHADER_IR
} PtR2ShaderStatus;
typedef struct {
    PtR2ShaderStatus status;
    uint32_t word, raw[4], raw_count;
    char reason[160];
} PtR2ShaderError;

typedef enum {
    PT_R2_F32 = 1, PT_R2_F32X3 = 3, PT_R2_F32X4 = 4
} PtR2ShaderType;
typedef enum {
    PT_R2_TEMP, PT_R2_INPUT, PT_R2_CONSTANT, PT_R2_CS, PT_R2_AS,
    PT_R2_PRESUB_RGB, PT_R2_PRESUB_ALPHA, PT_R2_ZERO, PT_R2_ONE, PT_R2_HALF,
    PT_R2_OUTPUT, PT_R2_ALT_TEMP,
    PT_R2_ADDR      /* the PVS address register A0 (a destination only; read through `relative`) */
} PtR2ShaderBank;
/* relative: 0 absolute; 1-4 the index is index + A0.xyzw[relative - 1] (PVS constants only; outside 0..255 reads 0). */
typedef struct {
    uint8_t type, bank, index, swizzle[4], absolute, negate, relative;
} PtR2ShaderOperand;
typedef enum {
    PT_R2_OP_NOP, PT_R2_OP_MAD, PT_R2_OP_DP3, PT_R2_OP_DP4,
    PT_R2_OP_D2A, PT_R2_OP_MIN, PT_R2_OP_MAX, PT_R2_OP_CND,
    PT_R2_OP_CMP, PT_R2_OP_FRC, PT_R2_OP_SOP, PT_R2_OP_EXP2,
    PT_R2_OP_LOG2, PT_R2_OP_RCP, PT_R2_OP_RSQ, PT_R2_OP_MUL,
    PT_R2_OP_ADD, PT_R2_OP_DST, PT_R2_OP_SGE, PT_R2_OP_SLT,
    PT_R2_OP_M2ADD, PT_R2_OP_RCP_DX, PT_R2_OP_RSQ_DX, PT_R2_OP_RCP_FF, PT_R2_OP_RSQ_FF,
    PT_R2_OP_LIT,   /* ME_LIGHT_COEFF_DX: (1, max(B,0), B > 0 ? max(A,0)^clamp(C,-128,128) : 0, 1) of the W operands */
    /* e^A, |A|^C (0^0 = 1, an overflow clamped as DX), sin A and cos A in radians (PVS math). */
    PT_R2_OP_EXPE, PT_R2_OP_POW, PT_R2_OP_SIN, PT_R2_OP_COS,
    /* VE_FLT2FIX_DX floor(A) and VE_FLT2FIX_DX_RND floor(A + 0.5) into A0 (e.g. Halo's word 0010010d). */
    PT_R2_OP_ARL, PT_R2_OP_ARR,
    /* ME_POWER_FUNC_FF_CLAMP_01: POW with the base and the result clamped to [0,1] (Tiger's fog factor). */
    PT_R2_OP_POW01
} PtR2ShaderOp;
typedef struct {
    PtR2ShaderOperand rgb_source[3], alpha_source[3];
    PtR2ShaderOperand rgb_arg[3], alpha_arg[3];
    uint8_t rgb_op, alpha_op, rgb_presub, alpha_presub;
    int8_t rgb_scale, alpha_scale;
    uint8_t rgb_clamp, alpha_clamp;
    uint8_t rgb_temp, rgb_mask, rgb_output, rgb_output_mask;
    uint8_t alpha_temp, alpha_mask, alpha_output, alpha_output_mask, depth_mask;
} PtR2ShaderUS;
/* A dual-math step (dual_op != NOP) also runs a math op on arg[2] (lanes x, y: its two scalar operands) into
 * lane dual_lane of alternate temporary dual_dst; both halves read the registers as they were before the step. */
typedef struct {
    PtR2ShaderOperand arg[3];
    uint8_t op, dst_bank, dst, mask, replicate_x, clamp;
    uint8_t dual_op, dual_dst, dual_lane, dual_clamp;
} PtR2ShaderPVS;
typedef enum { PT_R2_STEP_US = 1, PT_R2_STEP_TEX, PT_R2_STEP_PVS } PtR2ShaderStepKind;
typedef struct {
    uint32_t word;
    uint8_t kind;
    union {
        PtR2ShaderUS us;
        PtR2ShaderPVS pvs;
        struct { uint8_t op, src, dst, unit; } tex;
    } u;
} PtR2ShaderStep;
typedef struct {
    uint32_t tag, stage, precision, word_count, step_count;
    uint32_t textures, outputs, writes_depth;
    uint32_t words[PT_R2_SHADER_WORDS];
    PtR2ShaderStep step[PT_R2_SHADER_STEPS];
} PtR2ShaderIR;

/* A lossless interface token participates in the cache key. The executor
 * serializes RS/VAP/format semantics here, not addresses, constants, sampler
 * values, viewport or scissor. An inventory without those registers must use
 * an explicitly labelled semantic fixture, never claim a Tiger interface. */
#define PT_R2_SHADER_INTERFACE_WORDS 512u
typedef struct {
    uint32_t abi, input_count, output_count, constant_count;
    uint32_t texture_mask;
    uint8_t texture_dimension[16]; /* 0: 2D, 1: 3D, 2: cube; normalized float */
    uint32_t semantic_count, semantic[PT_R2_SHADER_INTERFACE_WORDS];
} PtR2ShaderInterface;

/* words are HOST uint32 values, captured in these slices:
 * US: seven header words, active TEX range, four active ALU banks.
 * PVS: CODE_CNTL_0, CODE_CNTL_1, four words per active instruction.
 * Flow control state is not in these words: the caller must supply it.
 * Nonzero flow control is rejected, not assumed absent by the compiler. */
PtR2ShaderStatus pt_r2_shader_decode(PtR2ShaderStage stage,
    PtR2ShaderPrecision precision, const uint32_t *words, size_t count,
    uint32_t pvs_flow_control, PtR2ShaderIR *ir, PtR2ShaderError *error);
PtR2ShaderStatus pt_r2_shader_validate(const PtR2ShaderIR *ir,
    const PtR2ShaderInterface *interface, PtR2ShaderError *error);
/* The interface half of pt_r2_shader_validate only, for an IR that is canonical by construction (the
 * output of pt_r2_shader_decode, kept unmodified by its owner): per-draw interface checks without re-decoding. */
PtR2ShaderStatus pt_r2_shader_check_interface(const PtR2ShaderIR *ir,
    const PtR2ShaderInterface *interface, PtR2ShaderError *error);
/* Canonical little-endian, padding-free, lossless key; compare all bytes.
 * A hash alone is not an equality test. No uniform values are included. */
PtR2ShaderStatus pt_r2_shader_key(const PtR2ShaderIR *ir,
    const PtR2ShaderInterface *interface, void *bytes, size_t capacity,
    size_t *needed, PtR2ShaderError *error);

typedef struct {
    float input[32][4], constant[256][4];
    float output[32][4], depth;
    uint32_t killed, pad[2]; /* MSL float4-array stride: 5136 bytes. */
} PtR2ShaderIO;
/* The callback owns sampling/format semantics, including projection/bias.
 * It receives the OLD source value even if src == dst, with the selected
 * profile's input normalization applied. Projection must use the same
 * profile's arithmetic. false is an error.
 * Reference tests bind constant 1x1 textures; that does not test LOD/filtering. */
typedef bool (*PtR2ShaderSample)(void *context, unsigned unit, unsigned op,
    const float coordinate[4], float result[4]);
PtR2ShaderStatus pt_r2_shader_run(const PtR2ShaderIR *ir,
    const PtR2ShaderInterface *interface, PtR2ShaderIO *io,
    PtR2ShaderSample sample, void *context, PtR2ShaderError *error);
/* pt_r2_shader_run without its per-call validation. Only for an (ir, interface) pair the caller has already
 * checked (canonical decode + pt_r2_shader_check_interface); per-vertex use in the draw path. */
PtR2ShaderStatus pt_r2_shader_execute(const PtR2ShaderIR *ir,
    const PtR2ShaderInterface *interface, PtR2ShaderIO *io,
    PtR2ShaderSample sample, void *context, PtR2ShaderError *error);

/* Emits a complete MSL 2.3 compute library, entry point pt_shader_test.
 * Buffer 0: PtR2ShaderIO[], buffer 1: uint count; texture/sampler N: unit N.
 * Compute sampling explicitly uses level 0 (TXB uses its bias as level).
 * This is a semantic test entry point, not a fragment-derivative substitute.
 * The emitted pt_shader_body is also usable by a stage wrapper, which
 * must supply its own stage-correct sampling functions and RS/VAP routing. */
PtR2ShaderStatus pt_r2_shader_msl(const PtR2ShaderIR *ir,
    const PtR2ShaderInterface *interface, char *text, size_t capacity,
    size_t *needed, PtR2ShaderError *error);
/* The same pt_shader_body for a fragment stage, without a kernel. io is a thread PtIO
 * {input[32], output[32], depth, killed}, constants come from `constant float4 *pt_k`, and the caller defines
 * PT_BODY_ARGS (extra leading parameters) and PT_TEX(unit, tx, sp, c, op): op 1 TEX, 3 TXP, 4 TXB (bias in c.w),
 * c the source temporary after the profile's input normalization. */
PtR2ShaderStatus pt_r2_shader_msl_body(const PtR2ShaderIR *ir,
    const PtR2ShaderInterface *interface, char *text, size_t capacity,
    size_t *needed, PtR2ShaderError *error);
#endif
