/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ANT_IMAGE_H
#define ANT_IMAGE_H
#include <stddef.h>
size_t ant_compare_rgba(const unsigned char *a,const unsigned char *b,size_t pixels,
    unsigned tolerance,unsigned char *difference,unsigned *maximum);
#endif
