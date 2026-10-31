# Third-party components

Ptolemy's own code is licensed GPL-2.0-or-later (SPDX headers; the GPLv2 text is in `LICENSE`).

## Included

| Component | Where | Use | License |
|---|---|---|---|
| [OpenBIOS](https://github.com/openbios/openbios) | `firmware/openbios/` | The machine's Open Firmware, as Ptolemy's fork (see `firmware/README.md`) | GPL-2.0 |
| [CoreMark](https://github.com/eembc/coremark) | `tests/epicycle/coremark/` | A CPU benchmark for the engine's tests; not part of the engine | EEMBC CoreMark license, `tests/epicycle/coremark/LICENSE.md` |

The notices are in `THIRD-PARTY-NOTICES.txt`.

## Used by tests only, not included

- [Unicorn](https://www.unicorn-engine.org/) and [Capstone](https://www.capstone-engine.org/) (Python packages):
  the instruction tests `tests/epicycle/lab.py` and `tests/altivec-compare.py` compare the engine with Unicorn.
- [Zig](https://ziglang.org/): assembles the bare-metal PowerPC tests.

## Written from specifications

All device models are Ptolemy's own, written from hardware and protocol documentation and from Tiger's observed
behaviour; no code from other emulators is included. Sources consulted include the OpenPIC Register Interface
Specification 1.2 (AMD 19725), the USB HID Usage Tables 1.12, AMD's R3xx/R5xx documentation and the register
definitions of the Linux and Mesa Radeon drivers (`radeon_reg.h`, `r300_reg.h`), Apple's IOGraphics and
Designing PCI Cards and Drivers, Motorola/Freescale's MPC7450 and AltiVec manuals, and the battery reply format
in Linux `drivers/macintosh/via-pmu.c`. No source code was copied from them.

## Not included

Ptolemy contains no Apple software: no Mac OS, ROMs, kernel extensions, drivers or SDKs. Mac OS X Tiger, its updates
and Xcode come from the user. The `tests/epicycle/ref/sys*.bin` files are outputs of Ptolemy's own CPU tests.
