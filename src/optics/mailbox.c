/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mailbox.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
struct PtR2Mailbox {
    pthread_mutex_t mutex;
    PtR2Message *messages;
    uint32_t capacity,head,count;
    size_t limit,bytes;
    uint64_t epoch;
    int consumer_waiting,producer_waiting;
    void (*wake_consumer)(void *),(*wake_producer)(void *);void *context;
};
PtR2Mailbox *pt_r2_mailbox_create(uint32_t jobs,size_t bytes,void (*consumer)(void *),void (*producer)(void *),void *context) {
    if(!jobs||jobs>65536||!bytes||!consumer||!producer)return NULL;
    PtR2Mailbox *m=calloc(1,sizeof *m);if(!m)return NULL;m->messages=calloc(jobs,sizeof(PtR2Message));
    if(!m->messages){free(m);return NULL;}
    if(pthread_mutex_init(&m->mutex,NULL)){free(m->messages);free(m);return NULL;}
    m->capacity=jobs;m->limit=bytes;m->epoch=1;m->consumer_waiting=1;
    m->wake_consumer=consumer;m->wake_producer=producer;m->context=context;return m;
}
PtR2MailResult pt_r2_mailbox_push(PtR2Mailbox *m,PtR2Message message) {
    if(!m||!message.release||message.bytes>m->limit)return PT_R2_MAIL_ARGUMENT;
    pthread_mutex_lock(&m->mutex);
    if(message.epoch!=m->epoch){pthread_mutex_unlock(&m->mutex);return PT_R2_MAIL_STALE;}
    if(m->count==m->capacity||message.bytes>m->limit-m->bytes){m->producer_waiting=1;pthread_mutex_unlock(&m->mutex);return PT_R2_MAIL_FULL;}
    m->messages[(m->head+m->count)%m->capacity]=message;m->count++;m->bytes+=message.bytes;
    int wake=m->consumer_waiting;m->consumer_waiting=0;pthread_mutex_unlock(&m->mutex);
    if(wake)m->wake_consumer(m->context);return PT_R2_MAIL_OK;
}
PtR2MailResult pt_r2_mailbox_pop(PtR2Mailbox *m,PtR2Message *out) {
    if(!m||!out)return PT_R2_MAIL_ARGUMENT;pthread_mutex_lock(&m->mutex);
    if(!m->count){m->consumer_waiting=1;pthread_mutex_unlock(&m->mutex);return PT_R2_MAIL_EMPTY;}
    *out=m->messages[m->head];memset(&m->messages[m->head],0,sizeof(PtR2Message));m->head=(m->head+1)%m->capacity;m->count--;m->bytes-=out->bytes;
    int wake=m->producer_waiting;m->producer_waiting=0;pthread_mutex_unlock(&m->mutex);
    if(wake)m->wake_producer(m->context);return PT_R2_MAIL_OK;
}
uint64_t pt_r2_mailbox_epoch(PtR2Mailbox *m) {if(!m)return 0;pthread_mutex_lock(&m->mutex);uint64_t e=m->epoch;pthread_mutex_unlock(&m->mutex);return e;}
int pt_r2_mailbox_reset(PtR2Mailbox *m) {
    if(!m)return 0;pthread_mutex_lock(&m->mutex);if(m->epoch==UINT64_MAX){pthread_mutex_unlock(&m->mutex);return 0;}m->epoch++;
    while(m->count) {
        PtR2Message message=m->messages[m->head];memset(&m->messages[m->head],0,sizeof message);
        m->head=(m->head+1)%m->capacity;m->count--;m->bytes-=message.bytes;
        pthread_mutex_unlock(&m->mutex);message.release(message.payload);pthread_mutex_lock(&m->mutex);
    }
    m->producer_waiting=0;m->consumer_waiting=1;pthread_mutex_unlock(&m->mutex);return 1;
}
void pt_r2_mailbox_destroy(PtR2Mailbox *m) {if(m){pt_r2_mailbox_reset(m);pthread_mutex_destroy(&m->mutex);free(m->messages);free(m);}}
