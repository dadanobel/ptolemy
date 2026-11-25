/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The host side of Ptolemy Agent's private calls. Every guest buffer is checked page by page (RAM only, user
 * mappings, write permission) before a command is read or answered. */
#include "ppc.h"
#include "pt_agent.h"
#include "pt_video.h"
#include "platform.h"
#include "optics/diagnostics.h"
_Static_assert(PTA_OPTICS == OPTICS_AGENT_OPCODE, "Optics agent opcode ABI");
/* Validate ALL pages before consuming a command. RAM only, user translation,
 * write permissions and COW faults respected; no device/VRAM host pointers. physical: a word for each page of n. */
static int pages_within(CPU *c, uint32_t ea, uint32_t n, uint32_t limit, int write, uint32_t *physical) {
    if (!n || n > limit || (uint64_t)ea + n > 0x100000000ull || !c->npreg || c->preg[0].base ||
        c->preg[0].read_only || !c->preg[0].host)
        return PTA_FAULT;
    unsigned i = 0;
    while (n) {
        uint32_t k = 4096 - (ea & 4095), ra;
        int wr;
        if (k > n)
            k = n;
        if (pt_xlate(c, ea, write, 1, &ra, &wr) || (write && !wr) || (uint64_t)ra + k > c->preg[0].size)
            return PTA_FAULT;
#ifdef PT_ENABLE_970
        if (c->is970 && ppc970_agent_prepare(c, ra, k, write))
            return PTA_FAULT;
#endif
        physical[i++] = ra;
        ea += k;
        n -= k;
    }
    return PTA_OK;
}
static int pages(CPU *c, uint32_t ea, uint32_t n, int write, uint32_t physical[17]) {
    return pages_within(c, ea, n, PTA_MAX_REQUEST, write, physical);
}
/* Copies between a host buffer and guest memory through the guest's pre-translated physical pages (an agent request
 * may span 17 pages; physical[0] is the address of ea itself, the rest are pages). */
static void copy_pages(CPU *c, uint32_t ea, void *b, uint32_t n, int write, const uint32_t *physical) {
    if (write)
        c->reserve = 0; /* Successful host copyout invalidates a guest reservation. */
    unsigned i = 0;
    while (n) {
        uint32_t k = 4096 - (ea & 4095);
        if (k > n)
            k = n;
        uint8_t *p = write ? pmem_write(c, physical[i++], k) : c->preg[0].host + physical[i++];
        if (write)
            memcpy(p, b, k);
        else
            memcpy(b, p, k);
        b = (uint8_t *)b + k;
        ea += k;
        n -= k;
    }
}
/* Video (pt_video.h). A compressed frame and a picture are far larger than an agent request, so their pages are
 * counted for the call; the rule is the same: every page is validated before a byte moves. */
typedef struct {
    CPU *c;
    uint32_t ea, row_bytes;
} VideoTarget;
enum { VIDEO_ROW_SLACK = 4096 }; /* a row's padding: bounds the picture's span in guest memory */
static int video_write(void *o, const uint8_t *rows, uint32_t stride, uint32_t width, uint32_t height) {
    VideoTarget *t = o;
    uint32_t row = 2 * width;
    if (t->row_bytes < row || t->row_bytes > row + VIDEO_ROW_SLACK)
        return PTA_INVALID;
    uint32_t span = (height - 1) * t->row_bytes + row; /* at most 2304 rows of 12288 bytes */
    uint32_t *physical = malloc(((size_t)span / 4096 + 2) * sizeof *physical);
    if (!physical)
        return PTA_BUSY;
    int rc = pages_within(t->c, t->ea, span, span, 1, physical);
    if (!rc)
        t->c->reserve = 0; /* as copy_pages: a host copyout invalidates a guest reservation */
    for (uint32_t y = 0; !rc && y < height; y++) { /* a row starts anywhere in its page: physical[] gives the page */
        const uint8_t *from = rows + (size_t)y * stride;
        for (uint32_t ea = t->ea + y * t->row_bytes, left = row, k; left; from += k, ea += k, left -= k) {
            k = 4096 - (ea & 4095) < left ? 4096 - (ea & 4095) : left;
            memcpy(pmem_write(t->c, (physical[(ea >> 12) - (t->ea >> 12)] & ~4095u) | (ea & 4095), k), from, k);
        }
    }
    free(physical);
    return rc;
}
static int video(CPU *c, uint32_t op) {
    uint32_t session = c->gpr[4], frame = c->gpr[5], ea = c->gpr[6], n = c->gpr[7];
    if (op == PTA_VIDEO_PROBE) {
        c->gpr[4] = PTV_VERSION;
        c->gpr[5] = (uint32_t)pt_video_enabled();
        return PTA_OK;
    }
    if (op == PTA_VIDEO_OPEN || op == PTA_VIDEO_DECODE) {
        uint32_t limit = op == PTA_VIDEO_OPEN ? PTA_MAX_REQUEST : PTV_MAX_FRAME;
        if (!n || n > limit)
            return PTA_INVALID;
        uint32_t *physical = malloc(((size_t)n / 4096 + 2) * sizeof *physical);
        uint8_t *b = malloc(n);
        int rc = physical && b ? pages_within(c, ea, n, limit, 0, physical) : PTA_BUSY;
        if (!rc) {
            copy_pages(c, ea, b, n, 0, physical);
            if (op == PTA_VIDEO_DECODE)
                rc = pt_video_decode(session, frame, b, n);
            else if (!(rc = pt_video_open(b, n, frame >> 16, frame & 0xffff, &session)))
                c->gpr[4] = session;
        }
        free(b);
        free(physical);
        return rc;
    }
    if (op == PTA_VIDEO_FETCH) {
        VideoTarget t = {c, ea, n};
        return pt_video_fetch(session, frame, video_write, &t);
    }
    return op == PTA_VIDEO_DROP ? pt_video_drop(session, frame) : pt_video_close(session);
}
/* The Tools agent's hypercall: validates every guest buffer in full, then dispatches hello, pull, transfers,
 * export, input, pointer and Optics status; the video calls need no agent. */
int pt_agent_hypercall(CPU *c, uint32_t instruction, PtAgent *a) {
    if (instruction != PTA_INSN || c->gpr[0] != PTA_MAGIC)
        return 0;
    unsigned need = MSR_PR | MSR_IR | MSR_DR;
    int rc = PTA_DENIED;
    uint32_t token = c->gpr[4], id = c->gpr[5], op = c->gpr[3];
    int is_video = op >= PTA_VIDEO_PROBE && op <= PTA_VIDEO_CLOSE;
    if (!a && !is_video)
        return 0;
    if (!c->sys || (c->msr & need) != need) {
        c->gpr[3] = rc;
        return 1;
    }
    if (is_video) {
        c->gpr[3] = (uint32_t)video(c, op);
        return 1;
    }
    uint8_t b[PTA_MAX_REQUEST];
    uint32_t physical[17], n = 0, ms = 0;
    if (op == PTA_HELLO) {
        rc = pt_agent_register_caps(a, c->gpr[5], c->gpr[6], &token);
        if (!rc) {
            c->gpr[4] = token;
            c->gpr[5] = PTA_VERSION;
            c->gpr[6] = pt_env_flag("PT_POINTER_V2") ? PTA_CAP_INPUT : 0;
            if (pt_env_flag("PT_R1_UX"))
                c->gpr[6] |= PTA_CAP_POINTER | PTA_CAP_EXPORT;
        }
    } else if (op == PTA_POLL) {
        uint32_t ea = c->gpr[5], cap = c->gpr[6];
        rc = pages(c, ea, cap, 1, physical);
        if (!rc) {
            rc = pt_agent_pull(a, token, b, cap, &id, &n, &ms);
            if (!rc) {
                copy_pages(c, ea, b, n, 1, physical);
                c->gpr[4] = id;
                c->gpr[5] = n;
                c->gpr[6] = ms;
            }
        }
    } else if (op == PTA_OUTPUT) {
        n = c->gpr[7];
        rc = pages(c, c->gpr[6], n, 0, physical);
        if (!rc) {
            copy_pages(c, c->gpr[6], b, n, 0, physical);
            rc = pt_agent_output(a, token, id, b, n);
        }
    } else if (op == PTA_FINISH)
        rc = pt_agent_finish(a, token, id, c->gpr[6]);
    else if (op == PTA_CHECK) {
        uint32_t cancel = 0;
        rc = pt_agent_check(a, token, id, &cancel);
        if (!rc)
            c->gpr[4] = cancel;
    } else if (op == PTA_INPUT)
        rc = c->gpr[6] == PTA_INPUT_ABI ? pt_agent_input(a, token, id) : PTA_INVALID;
    else if (op == PTA_POINTER) {
        rc = c->gpr[6] == 24 ? pages(c, c->gpr[5], 24, 0, physical) : PTA_INVALID;
        if (!rc) {
            copy_pages(c, c->gpr[5], b, 24, 0, physical);
            rc = pt_agent_pointer(a, token, b);
        }
    } else if (op == PTA_BYE)
        rc = pt_agent_unregister(a, token);
    /* Transfers. Every guest buffer is validated in full before the host state moves. */
    else if (op == PTA_XBEGIN) {
        uint32_t ea = c->gpr[5];
        rc = pages(c, ea, PTA_XHEAD, 1, physical);
        if (!rc) {
            rc = pt_agent_xbegin(a, token, b, &id);
            if (!rc) {
                copy_pages(c, ea, b, PTA_XHEAD, 1, physical);
                c->gpr[4] = id;
            }
        }
    } else if (op == PTA_XCHUNK) {
        uint32_t ea = c->gpr[7];
        rc = pages(c, ea, PTA_CHUNK, 1, physical);
        if (!rc) {
            rc = pt_agent_xchunk(a, token, id, c->gpr[6], b, &n);
            if (!rc) {
                copy_pages(c, ea, b, n, 1, physical);
                c->gpr[4] = n;
            }
        }
    } else if (op == PTA_XCOMMIT) {
        rc = pages(c, c->gpr[6], 32, 0, physical);
        if (!rc) {
            copy_pages(c, c->gpr[6], b, 32, 0, physical);
            rc = pt_agent_xcommit(a, token, id, b);
        }
    } else if (op == PTA_XDONE) {
        rc = pages(c, c->gpr[7], PTA_PATH_MAX, 0, physical);
        if (!rc) {
            copy_pages(c, c->gpr[7], b, PTA_PATH_MAX, 0, physical);
            b[PTA_PATH_MAX - 1] = 0;
            rc = pt_agent_xdone(a, token, id, c->gpr[6], (char *)b);
        }
    } else if (op == PTA_EXPORT_BEGIN)
        rc = pt_agent_export_begin(a, token, id, (uint64_t)c->gpr[6] << 32 | c->gpr[7]);
    else if (op == PTA_EXPORT_CHUNK) {
        n = c->gpr[7];
        rc = n > 8 && n <= PTA_CHUNK ? pages(c, c->gpr[6], n, 0, physical) : PTA_INVALID;
        if (!rc) {
            copy_pages(c, c->gpr[6], b, n, 0, physical);
            uint64_t off = 0;
            for (unsigned i = 0; i < 8; i++)
                off = off << 8 | b[i];
            rc = pt_agent_export_chunk(a, token, id, off, b + 8, n - 8);
        }
    } else if (op == PTA_EXPORT_COMMIT) {
        rc = pages(c, c->gpr[6], 32, 0, physical);
        if (!rc) {
            copy_pages(c, c->gpr[6], b, 32, 0, physical);
            rc = pt_agent_export_commit(a, token, id, b);
        }
    } else if (op == PTA_OPTICS) {
        uint32_t ea = c->gpr[5];
        if (c->gpr[6] != OPTICS_DIAG_BYTES || c->gpr[7] != OPTICS_DIAG_ABI)
            rc = PTA_INVALID;
        else {
            rc = pages(c, ea, OPTICS_DIAG_BYTES, 1, physical);
            if (!rc) {
                rc = pt_agent_optics_status(a, token, b);
                if (!rc)
                    copy_pages(c, ea, b, OPTICS_DIAG_BYTES, 1, physical);
            }
        }
    } else
        rc = PTA_INVALID;
    c->gpr[3] = rc;
    return 1;
}
