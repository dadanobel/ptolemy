/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_R2_MAILBOX_H
#define PT_R2_MAILBOX_H
#include <stddef.h>
#include <stdint.h>
typedef struct PtR2Mailbox PtR2Mailbox;
typedef struct {
    uint64_t epoch,sequence;
    uint32_t kind;
    size_t bytes;
    void *payload;
    void (*release)(void *);
} PtR2Message;
typedef enum {PT_R2_MAIL_OK,PT_R2_MAIL_EMPTY,PT_R2_MAIL_FULL,PT_R2_MAIL_STALE,PT_R2_MAIL_ARGUMENT} PtR2MailResult;
/* Two owners. Wakers post/coalesce an event only; never run an owner inline.
 * The tiny native mutex protects metadata, never payload/Metal/RAM work. */
PtR2Mailbox *pt_r2_mailbox_create(uint32_t jobs,size_t bytes,
    void (*wake_consumer)(void *),void (*wake_producer)(void *),void *context);
/* Success transfers ownership; failure leaves ownership with the producer. */
PtR2MailResult pt_r2_mailbox_push(PtR2Mailbox *,PtR2Message);
/* Success transfers ownership to the consumer. Release after encoding/copying.
 * If the backend is BUSY, retain this head message; do not pop/reorder more. */
PtR2MailResult pt_r2_mailbox_pop(PtR2Mailbox *,PtR2Message *);
uint64_t pt_r2_mailbox_epoch(PtR2Mailbox *);
/* Both actors quiesced: invalidate epoch, release queued payloads, resume only
 * after device/renderer/I/O reset barriers. Reset is not a producer operation. */
int pt_r2_mailbox_reset(PtR2Mailbox *);
void pt_r2_mailbox_destroy(PtR2Mailbox *);
#endif
