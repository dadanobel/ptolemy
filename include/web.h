/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_WEB_H
#define PT_WEB_H
#include <stddef.h>
#include <stdint.h>
/* Web gateway for the guest's old browsers (web_macos.m): listens on 127.0.0.1 and returns the port, 0 when unavailable.
 * net_user.c sends the guest's port-80 connections there. */
uint16_t pt_web_start(void);
/* Guest request head, split in place (web.c, portable). */
enum {PT_WEB_HEADERS=64,PT_WEB_HEAD=65536};
typedef struct { char *method,*target,*version; unsigned headers; char *name[PT_WEB_HEADERS],*value[PT_WEB_HEADERS]; } PtWebRequest;
/* head: n bytes ending with the blank line. 0, or EINVAL for a malformed head, E2BIG for too many headers. */
int pt_web_parse(char *head,size_t n,PtWebRequest *);
const char *pt_web_header(const PtWebRequest *,const char *name);   /* case-insensitive; NULL when absent */
/* An HTTP page's body: every https:// (also JSON-escaped https:\/\/) becomes http://, in place; returns the new length. */
size_t pt_web_downgrade(uint8_t *bytes,size_t n);
#endif
