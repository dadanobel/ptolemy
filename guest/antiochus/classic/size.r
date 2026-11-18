/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "Types.r"
resource 'SIZE' (-1) {
    reserved, acceptSuspendResumeEvents, reserved, canBackground,
    multiFinderAware, backgroundAndForeground, dontGetFrontClicks, ignoreChildDiedEvents,
    is32BitCompatible, isHighLevelEventAware, onlyLocalHLEvents, notStationeryAware,
    dontUseTextEditServices, reserved, reserved, reserved,
    8 * 1024 * 1024, 4 * 1024 * 1024
};
