/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "cpu_access.h"
#include <string.h>
/* Decodes the ARM64 load or store that faulted on GPU-shared memory: size, register, direction and signedness, for
 * emulating it. */
int optics_a64_memory_decode(uint32_t in, OpticsA64Memory *out) {
    if (!out)
        return 0;
    OpticsA64Memory m = {.instruction = in, .rn = (in >> 5) & 31, .rm = 31, .rt = in & 31};
    unsigned size = in >> 30, op = (in >> 22) & 3;
    m.simd = (in >> 26) & 1;
    if (m.simd) {
        if (op >= 2 && size)
            return 0;
        m.bytes = (uint8_t)(op >= 2 ? 16 : 1u << size);
        m.load = op & 1;
    } else {
        if ((op == 2 && size == 3) || (op == 3 && size >= 2))
            return 0;
        m.bytes = (uint8_t)(1u << size);
        m.load = op != 0;
        m.sign = op >= 2;
        m.destination64 = op == 2 || size == 3;
    }
    if ((in & 0x3b000000u) == 0x39000000u) {
        m.mode = 0;
        m.immediate = ((in >> 10) & 4095) * m.bytes;
    } else if ((in & 0x3b200c00u) == 0x38000000u) {
        m.mode = 1;
        m.immediate = (int32_t)(in << 11) >> 23;
    } else if ((in & 0x3b200c00u) == 0x38200800u) {
        m.mode = 2;
        m.rm = (in >> 16) & 31;
        m.extend = (in >> 13) & 7;
        if (m.extend != 2 && m.extend != 3 && m.extend != 6 && m.extend != 7)
            return 0;
        if (in & (1u << 12)) {
            while ((1u << m.shift) < m.bytes)
                m.shift++;
        }
    } else
        return 0;
    *out = m;
    return 1;
}
