/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_AGENT_H
#define PT_AGENT_H
#include <stdint.h>
#define PTA_INSN 0x00005041u
#define PTA_MAGIC 0x50544147u
#define PTA_MAX_ARGS 64u
#define PTA_MAX_REQUEST 65536u
#define PTA_MAX_OUTPUT (1024u*1024u)
/* Protocol 3 confirms capabilities. Protocol 2 file envelopes stay compatible.
 * Protocol 2 adds host -> guest file transfers. HELLO carries the guest's version in r5 and returns the
 * host's; a version-1 guest (r5 = 0) keeps working for commands and is told it cannot receive files. */
#define PTA_VERSION 3u
#define PTA_TRANSFER_VERSION 2u
#define PTA_CHUNK 65536u                      /* XCHUNK: the guest's buffer is always this large */
#define PTA_NAME_MAX 255u
#define PTA_XHEAD (8u+32u+4u+4u+PTA_NAME_MAX)   /* XBEGIN header: size BE64, sha256, kind BE32, name length BE32, name */
enum {PTA_XFILE,PTA_XCLIPBOARD};   /* kind: publish into ~/Desktop, or set the Tiger pasteboard (UTF-8 text) */
#define PTA_CLIP_MAX (1024u*1024u)
#define PTA_XLIMIT 0xffff0000u                /* offsets are 32-bit */
#define PTA_PATH_MAX 1024u                    /* XDONE: published path, NUL-terminated in a buffer this large */
enum {PTA_OK,PTA_AGAIN,PTA_BUSY,PTA_INVALID,PTA_FAULT,PTA_DENIED,PTA_CANCELLED};
enum {PTA_HELLO,PTA_POLL,PTA_OUTPUT,PTA_FINISH,PTA_CHECK,PTA_BYE,PTA_XBEGIN,PTA_XCHUNK,PTA_XCOMMIT,PTA_XDONE,PTA_OPTICS,PTA_INPUT,PTA_POINTER,PTA_EXPORT_BEGIN,PTA_EXPORT_CHUNK,PTA_EXPORT_COMMIT};
#define PTA_CAP_INPUT 1u
#define PTA_CAP_EXPORT 2u
#define PTA_CAP_POINTER 4u
#define PTA_INPUT_ABI 1u
typedef struct PtAgent PtAgent;
typedef struct {uint64_t at;int32_t x,y;uint32_t width,height,state;} PtGuestPointer;
int pt_agent_pointer(PtAgent *,uint32_t token,const uint8_t wire[24]);
int pt_agent_pointer_read(PtAgent *,PtGuestPointer *);
unsigned pt_agent_capabilities(PtAgent *,uint32_t *session);
/* Explicit development mode only. Parent directory must be owned and 0700.
 * Socket is 0600; same-host-user access, not a boundary against that user. */
PtAgent *pt_agent_open(const char *socket_path);
void pt_agent_close(PtAgent *);
int pt_agent_register_caps(PtAgent *,uint32_t version,uint32_t caps,uint32_t *token);
int pt_agent_register(PtAgent *,uint32_t guest_version,uint32_t *token);
/* pull consumes a job only after the adapter validated the entire destination. */
int pt_agent_pull(PtAgent *,uint32_t token,void *,uint32_t capacity,uint32_t *id,uint32_t *n,uint32_t *timeout_ms);
int pt_agent_output(PtAgent *,uint32_t token,uint32_t id,const void *,uint32_t n);
int pt_agent_finish(PtAgent *,uint32_t token,uint32_t id,uint32_t exit_status);
int pt_agent_check(PtAgent *,uint32_t token,uint32_t id,uint32_t *cancel);
int pt_agent_export_begin(PtAgent *,uint32_t token,uint32_t job,uint64_t size);
int pt_agent_export_chunk(PtAgent *,uint32_t token,uint32_t job,uint64_t off,const void *,unsigned);
int pt_agent_export_commit(PtAgent *,uint32_t token,uint32_t job,const uint8_t sha[32]);
int pt_agent_input(PtAgent *,uint32_t token,unsigned state);
int pt_agent_unregister(PtAgent *,uint32_t token);
/* One transfer at a time (socket op 2). The guest pulls the stream in order: a chunk at the served end advances it
 * and the host's SHA-256; a chunk wholly inside the served part is re-read without side effects; a gap or a chunk
 * across the end is refused. COMMIT checks size and both hashes and may be repeated; only after it the guest
 * publishes and reports the path (or a failure) with DONE. A cancelled transfer answers PTA_CANCELLED. */
int pt_agent_xbegin(PtAgent *,uint32_t token,uint8_t head[PTA_XHEAD],uint32_t *id);
int pt_agent_xchunk(PtAgent *,uint32_t token,uint32_t id,uint32_t offset,uint8_t out[PTA_CHUNK],uint32_t *n);
int pt_agent_xcommit(PtAgent *,uint32_t token,uint32_t id,const uint8_t sha256[32]);
int pt_agent_xdone(PtAgent *,uint32_t token,uint32_t id,uint32_t status,const char *path);
/* Read-only Optics diagnostics ABI, independent of agent transfer protocol 2. */
int pt_agent_optics_publish(PtAgent *,const void *,uint32_t);
int pt_agent_optics_status(PtAgent *,uint32_t token,void *);
struct CPU;
int pt_agent_hypercall(struct CPU *,uint32_t instruction,PtAgent *);
#endif
