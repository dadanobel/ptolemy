/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PT_G5_LOWER_H
#define PT_G5_LOWER_H
#include "g5.h"
/* Leaf lowering for a future register-resident G5 region backend.
 * Caller supplies PPC source values in x9/x10 and consumes x11.
 * No memory, flags, PC update, exception delivery or register allocation here. */
PtG5Result pt_g5_lower_leaf(uint32_t instruction, uint32_t *word);
#endif
