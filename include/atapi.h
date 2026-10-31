/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_ATAPI_H
#define PT_ATAPI_H
#include "ata.h"
void pt_atapi_bind(PtAta *,PtScsi *);
void pt_atapi_reset(PtAta *);
void pt_atapi_command(PtAta *,uint8_t);
int pt_atapi_data(PtAta *,uint8_t *,unsigned,int);
int pt_atapi_service(PtAta *);
#endif
