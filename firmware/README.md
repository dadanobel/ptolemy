# Firmware

`openbios/` is Ptolemy's fork of [OpenBIOS](https://github.com/openbios/openbios), the free implementation of
IEEE 1275 Open Firmware, licensed GPL-2.0. The fork starts from upstream commit
`e5ac46dd24e6216c36aa80462af25457e7029440` (29 June 2026). Ptolemy is not affiliated with the OpenBIOS project.

## Changes from upstream

| File | Change |
|---|---|
| `Makefile.target`, `config/scripts/ptolemy-build-date.py` | The banner's build date comes from `SOURCE_DATE_EPOCH`, so builds are reproducible |
| `arch/ppc/qemu/init.c` | The board signature `PTOL`; a 1.25 GiB PCI memory window (the Radeon's 256 MiB VRAM BAR fits); the 7447A's L2 cache node |
| `drivers/macio.c` | The Screamer sound node Tiger's audio driver expects |
| `drivers/pmu.c` | The VIA handshake is left as Apple's firmware leaves it before the OS starts |
| `drivers/pci.c`, `drivers/pci_database.c` | The Radeon 9700 (`1002:4e44`, `ATY,R300`) gets the boot display FCode below |
| `drivers/ptolemy-radeon.fs`, `drivers/build.xml`, `arch/ppc/build.xml`, `arch/ppc/ppc.fs` | Boot display FCode for the Radeon model: a linear 32-bit framebuffer set up by the host, and the ATI identity properties Tiger's Radeon driver reads |
| `forth/admin/banner.fs` | The banner names the fork |

## Building the ROM

On Linux (tested on Ubuntu 24.04 with `powerpc-linux-gnu-gcc` 13.3.0, `toke` 1.0.2 from fcode-utils and `xsltproc`
with libxml 2.9.14):

```sh
sudo apt install gcc-powerpc-linux-gnu fcode-utils xsltproc make python3
cd firmware/openbios
config/scripts/switch-arch ppc
SOURCE_DATE_EPOCH=1792972800 make
```

The ROM is `obj-ppc/openbios-qemu.elf`. With that epoch and toolchain its SHA-256 is
`cf9e57a70b6c9df10a4019d5d5df64e537fa157fa88bd26fb041b6db73229bda`, the ROM of Ptolemy 1.0 beta 1.
The engine loads it with `--rom`; Ptolemy.app carries it as `openbios-ppc`.
