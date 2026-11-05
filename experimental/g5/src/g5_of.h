/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_G5_OF_H
#define PT_G5_OF_H
#include "g5.h"
/* Encode OF /memory reg data: two address cells, one or two size cells.
 * A one-cell size is split; no 4-GiB bank may silently become a zero-size bank. */
PtG5Result pt_g5_of_memory(const PtG5Range *, unsigned, unsigned, uint8_t *, size_t, size_t *);
#endif
