/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#import "renderer.h"
extern "C" {
#include "diagnostics.h"
}
/* Renderer owner only; forward the resulting VALUE to the CPU diagnostics cache.
 * It never sets OPTICS_CPU_GUARDS or OPTICS_HARDWARE_FRONTEND; the frontend's own status reports those. */
void optics_status_renderer(OpticsStatus *,PtR2Renderer *);
