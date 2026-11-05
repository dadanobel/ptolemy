# Epicycle: PowerPC to ARM64

Epicycle is Ptolemy's CPU. It runs a PowerPC 750 (G3) with FPU and supervisor state, and the G4 (7400, 7447A) with
AltiVec, by translating PowerPC code block by block into ARM64. In Ptolemy the code runs in a
Hypervisor.framework vCPU on Apple silicon (`hvf.c`, driven by `src/firmware.c`). The test stands (`pt` on Linux, `ptv`
on macOS) run user-mode programs (static ELF32 PowerPC, Linux ABI, musl) or bare-metal tests (`-s`: supervisor mode,
exceptions, 750 MMU).

## Files

| File | Contents |
|---|---|
| `ppc.h` | CPU state and the shared interface. A CR field is a 64-bit value V: LT = bit 62, GT = (int64)V > 0, EQ = (uint32)V == 0, SO = bit 61 ^ bit 63 |
| `interp.c` | interpreter (750 with FPU and supervisor state, 7447A additions): the JIT's reference and the fallback for rare instructions |
| `altivec.c` | AltiVec (VMX) interpreter |
| `vmx_neon.h` | integer VMX to NEON emitter used by `jit.c` |
| `jit.c` | block JIT without an IR: superblocks, per-block register cache, block linking, jump cache for indirect branches, cmp+bc fusion, FP fast paths, profiler |
| `mmu.c` | 750 MMU: BATs, segments, hashed page table with R/C bits, 7447A software TLB; memory access for the interpreter |
| `linux.c` | Linux stand: ELF loader, Linux PPC system calls; bare metal with RAM in a memfd and EA windows filled on SIGSEGV |
| `hvf.c` | Hypervisor.framework VM (macOS): ARM page tables that mirror the PPC MMU, filled on access faults; DSI from JIT code; the HVF stand's `main` |
| `hv.entitlements` | the `com.apple.security.hypervisor` entitlement for signing the HVF stand, the probes and the product |
| `profdump.py` | reads `prof.bin`: the hottest PPC blocks and their ARM64 code (capstone) |

Tests live in `../tests/epicycle/`:

| File | Contents |
|---|---|
| `ops.c` | differential test (32/64-bit arithmetic, division, shifts, bit fields, switch, function pointers, FP): one hash natively and under the engine |
| `regress.c` | semantic regression: FPU and integer edge cases per Book I, the PT_CHECK comparator, translation at a page edge |
| `jit_cache.c` | JIT cache invalidation regression: hash collisions, generation wrap, the X_LOOKUP path |
| `lab.py` | counterexample lab: random programs against Unicorn (750 model) with Book I corrections, ddmin minimization |
| `sys1.S` .. `sys9.S`, `sys.ld` | bare metal: exceptions (sys1), MMU (sys2), DEC, POW, icbi, devices, EA wrap, string instructions (sys3), SR switches in real mode (sys4), one VSID in two SRs (sys5), the jump cache across a segment alias (sys6); twi in the VM: every TO bit, the 0xFFF00700 vector, from user mode (sys7); icbi at DR=1, supervisor and user, through an alias no ARM entry holds (sys8); FP and VMX unavailable in the VM, also after an mtmsr in the block (sys9, 7447A) |
| `ref/sys1.bin` .. `ref/sys3.bin`, `ref/sys7.bin` .. `ref/sys9.bin` | reference journals of sys1-sys3 and sys7-sys9 |
| `sys.inc`, `fold.inc` | shared macros: the journal and handler of sys1-sys9; the r26 hash (FOLD, VFOLD) of brl, so, vmsum and vmxops |
| `loop.S`, `atom.S`, `ops2.S`, `fp.S`, `str.S`, `fpa.S` | JIT against the interpreter (PT_CHECK=1): a side exit in a loop; lwarx/stwcx., dcbz; CR logic, sraw, lswi; FP loads, fmr...; lswx/stswx, mffs/mtfsf; FP arithmetic (also with FPSCR[NI]=1) |
| `vmsum.S` | vmsumshm/vmsumshs, JIT against the interpreter (`PT_G4=1 PT_VMX_NEON=1 PT_VMX_MHADD=1`, with PT_CHECK=1) |
| `vmxops.S` | the other multiply-sums, vsum*, vrl*, vpkpx, vctuxs/vctsxs (threshold lanes, VSCR.NJ), JIT against the interpreter (`PT_G4=1 PT_VMX_NEON=1 PT_VMX_INT2=1`, with PT_CHECK=1) |
| `so.S` | XER[SO]=1: blocks for SO=1 (compares, record forms, OE forms, stwcx., mtxer switches, jump cache), JIT against the interpreter (with PT_CHECK=1, PT_CR_FUSE=1) |
| `brl.S` | conditional bcctrl/bclrl on a CR bit, taken and not taken (LR written either way), JIT against the interpreter (with PT_CHECK=1) |
| `hello.c`, `coremark/` | guest programs (CoreMark 1.0 by EEMBC, see `coremark/LICENSE.md`) |
| `hvprobe.c`, `hvc_matrix.c`, `uma_probe.m` | macOS probes: HVF big-endian support, granule and exit cost; `hvc` behaviour; memory shared by the VM and Metal |

## Building

Linux stand `pt` (AArch64 Linux, no VM), from `epicycle/`:

    gcc -O2 -fno-delete-null-pointer-checks -pthread -o pt interp.c mmu.c linux.c jit.c altivec.c -lm

The guest normally sits at host address 0 (`c->mem == NULL`), hence `-fno-delete-null-pointer-checks`.

HVF stand `ptv` (macOS on Apple silicon), from `epicycle/`:

    clang -O2 -DPT_HVF -o ptv hvf.c linux.c interp.c mmu.c jit.c altivec.c -framework Hypervisor
    codesign -s - -f --entitlements hv.entitlements ptv

The product links the same sources with `-DPT_HVF -DPT_NO_MAIN` (`tools/build-optics.sh`); `build.sh` also builds
CPU unit tests from `interp.c`, `mmu.c` and `altivec.c`.

PowerPC test programs:

- C (Linux ABI): `python3 -m ziglang cc -target powerpc-linux-musleabihf -O2 -mcpu=750 -static`.
- User-mode assembly tests (`_start`, exit through `sc`): `powerpc-linux-gnu-gcc -nostdlib -static -mcpu=750 -o atom atom.S`.
- Bare metal: see the header of `sys1.S` (zig `cc` and `ld.lld -N -e reset -T sys.ld`; GNU as and ld work too).
- Assemble in `tests/epicycle`: `.include "sys.inc"` and `.include "fold.inc"` are looked up in the working directory.

## Running

    ./pt prog.ppc [args]          # JIT
    ./pt -i prog.ppc [args]       # interpreter
    ./pt -s test.elf > out.bin    # bare metal: the 4 KB result journal goes to stdout when the test reaches `b .`
                                  # (a 750; PT_SYS_G4=1: the 7447A with VMX)

`ptv` takes the same options. Environment switches:

- `PT_CHECK=1`: every instruction is compared with the interpreter. It uses one-instruction blocks, so a bug that
  needs several instructions in one block does not show; a run with only `PT_MAXBLK=1` tells whether a failure goes
  away with such blocks. `PT_CHECK=lo-hi` starts comparing when pc first enters [lo, hi). Stands only.
- `PT_PROF=1`: profile. The Linux stand writes `prof.bin` (`python3 profdump.py prof.bin [N|regs]`); `ptv` prints a
  per-instruction histogram with `PT_STATS`.
- `PT_STATS=1`: exit, time and device statistics.
- `PT_MAXBLK=n` (1..256), `PT_NOLINK=1`, `PT_NOJC=1`, `PT_NOZB=1` (no guest at host address 0): diagnostics.
- `PT_INTERP_BATCH=n` (1..4096): after an untranslatable instruction the interpreter goes on until more than 8
  translatable instructions come in a row, at most n instructions. Default 256 on the machine, 1 in the stands;
  `PT_CHECK` and `PT_MAXBLK=1` force 1.
- `PT_VMRFI`, `PT_VMSC`, `PT_VMMSR`: rfi, sc (with it the twi trap and FP/VMX unavailable) and mode-changing mtmsr
  directly in the VM (default 1, machine only; 0: through the environment; 2: also on the bare-metal stand, so that
  the sys tests exercise these paths).
- Fast paths that are off by default in the engine: `PT_FPR_CACHE`, `PT_FP_HOIST`, `PT_FP_LAZY`, `PT_FP_ROUND_FAST`,
  `PT_FMA_FAST`, `PT_FMA_DIRECT_S`, `PT_FP_MIXED`, `PT_FP_MORE`, `PT_CR_FUSE`, `PT_VMX_NEON`, `PT_VMX_INT2`,
  `PT_VMX_MHADD`, `PT_VMX_FP2` (`=1` enables). A window launch enables the accepted FP paths for every CPU model and VMX paths for G4
  (`include/pt_cpu_profile.h`). `PT_CPU_FAST=0` disables these defaults for fixtures; `=1` also enables them
  headlessly. Explicit per-feature values win. `PT_FP_FR`, `PT_FP_SGL`, `PT_VMX_MEM`, `PT_VMX_VSCR`, `PT_VMX_FP` are on unless set to 0.

## Testing

- `ops.c`: build it natively and for PowerPC (at -O0, -O2 and -Os); the native run, `./pt -i` and `./pt` must print
  the same hash.
- `regress.c`, from the repository root (the JIT checks run only on AArch64 Linux):

      cc -O1 -I epicycle -o regress tests/epicycle/regress.c epicycle/interp.c epicycle/mmu.c epicycle/altivec.c -lm && ./regress

- `jit_cache.c` includes `jit.c`; build it like `regress.c` on AArch64 Linux or macOS.
- `lab.py` needs the `unicorn` and `capstone` Python packages: `python3 tests/epicycle/lab.py epicycle/pt [N [SEED]]`
  (exit 0: no mismatches).
- Assembly tests under `PT_CHECK=1`: `atom.S` exits with 7, `loop.S` with 3, `str.S` with 63; `ops2.S`, `fp.S`,
  `fpa.S`, `vmsum.S`, `vmxops.S`, `so.S` and `brl.S` exit with a hash that must equal the interpreter's (`-i`).
- Bare metal: for sys1-sys3 and sys7-sys9 (sys9 with `PT_SYS_G4=1`) every mode (interpreter, JIT, `PT_CHECK=1`) must
  reproduce `ref/sysN.bin` byte for byte;
  for sys4-sys6 the JIT (on `ptv` with `PT_VMRFI=2 PT_VMMSR=2`) must match the interpreter (`-i -s`).
- Probes: build with clang and `-framework Hypervisor` (`uma_probe.m` also needs Metal and Foundation) and sign with
  `hv.entitlements`; `hvc_matrix.c` gives its command in its header.

## Design notes

- Registers: r0-r12 and r31 stay in fixed ARM registers; the most used other GPRs get a per-block cache (x15, x18-x25,
  and x28 when the guest sits at host address 0).
- Blocks never cross a 4 KB page and are keyed by `pc | PR << 1 | IR`, XER.SO and the physical page, so processes that share
  code (dyld, a program after fork) share translations; an SO change inside a block (mtxer, an OE overflow) goes on
  through the jump cache keyed with the new SO. Direct links stay within a page; other targets go through the
  jump cache (64 Ki cells of 32 bytes, two ways; the key holds pc, the mode and, with IR=1, the target segment's SR).
  In the VM each cell also keeps the EA page generation that tlbie bumps; the Linux stand clears the jump cache on
  tlbie. icbi followed by context synchronization drops the blocks of that physical page. JIT code queues the page
  itself (in the VM it finds it with AT) and skips a page without translated code (a byte map), so XNU's sync_cache64
  over every new page does not leave the JIT; only the isync after a queued page does.
- VM: guest physical memory is the IPA space; code runs at EL1 and leaves with `brk #0` (not `hvc`, see
  `hvc_matrix.c`). Data goes through three TTBR0 roots (real mode, DR=1 supervisor, DR=1 user) with their own ASIDs
  and up to 8 cached SR configurations. A DSI from JIT code restores the precise PPC state from the host PC
  (`jit_restore`).
- Devices: everything outside `preg[]` goes to `env_mmio` (bytes in memory order). From JIT code the Linux stand
  decodes the faulting ldr/str on SIGSEGV; the VM uses stage-2 data aborts (EC 0x24 with ISV). A firmware instruction
  (pc >= 0xFFF00000) that has left the VM 256 times for a device is then run by the interpreter.
- Interrupts: the environment raises `c->irq`; the JIT polls it on block entry and backward branches, the interpreter
  before every instruction. External beats DEC; MSR.POW sleeps only with HID0 DOZE, NAP or SLEEP (NAP or SLEEP on
  the 7447A).
- Default clock: TB at 25 MHz from the host clock, DEC interrupts on the 0 -> -1 transition. An environment may supply
  its own (`env_spr`, `env_irq`); on the machine, JIT code reads TB and DEC from CNTVCT without leaving the VM.
- In supervisor mode an illegal instruction raises a program exception and is reported on stderr
  (`ptolemy: invalid instruction ...`, the first 20).

## FPU status

Covered by `lab.py` and `regress.c`: double and single arithmetic (one rounding), fma, all RN modes, NaN precedence,
compares, fctiw*, frsp, lfs/stfs, FPSCR (sticky bits, FX/FEX/VX, mtfsf/mtfsfi/mtfsb/mcrfs, FPRF, FI, Rc=1). FR is
computed (`PT_FP_FR=0` turns it off) and checked by `tests/fpscr-fr.c` against vectors from `tests/fp-oracle.py`;
the lab cannot check it because Unicorn has no FR.

- FP enabled-exception program interrupts (MSR.FE0/FE1) are not implemented.
- OE/UE=1 exponent scaling is implemented for single results, and for double multiply and divide with `PT_FP_RANGE=1`.
- With VE/ZE=1 only `regress.c` checks the results against Book I (Unicorn does not model them).
- fres and frsqrte return the computed value, not the hardware estimate.
- The lab rounds the inputs of single operations to single (Book I leaves other inputs undefined), so it does not test
  SNaN inputs of single operations.
- Unverified: the high word of FRT after fctiw*/mffs is 0xFFF80000 (architecturally undefined; G4 behaviour unknown).
- Unverified: 0*inf+QNaN sets no VXIMZ and 0*inf-inf sets no VXISI on the 750/G4 (not checked on hardware).

## Known limitations

- A BAT, SDR1 or machine state change (`xgen`) flushes the whole JIT cache and all host mappings.
- Ordinary loads and stores raise no alignment exception; direct-store segments (T=1) are not supported.
- Rare instructions run in the interpreter: FP record forms (Rc=1), fres, mcrfs, mtfsb0/mtfsb1, BAT writes, FP special
  cases, and frsqrte/stfiwx unless `PT_FP_MORE=1`.
- An EA past 2^32 (base + positive offset, formed in 64 bits by the JIT) wraps in the VM, where VA 4-8 GiB aliases
  EA 0-4 GiB in every root (the engine lives from 8 GiB); the Linux stand does not wrap it.
- x18 belongs to the register cache: fine on Linux and in the VM, not for a macOS user-mode host.

`PT_FP_MOVES=1` is an experimental bit-preserving FP-register path for fmr/fneg/fabs/fnabs.
It preserves the existing write-through recovery contract and does not extend the single-format cache.

`PT_FP_DIRECTED=1` adds a guarded native path for finite RN=1/2/3 arithmetic and `frsp`.
RN=0 keeps the existing backend. NI, enabled exceptions, exceptional results and record forms
retain the interpreter. The path restores ARM FPCR before publishing results or leaving the block;
each directed operation ends its block and may continue through the native jump cache.
Both new FP flags default to zero pending application-level acceptance; `=0` disables them.

With `PT_STATS=1`, `FP_SITE` reports up to twelve PC/opcode/FPSCR groups per snapshot,
with a separate count for omitted groups. `tools/fp-window.py` requires explicit benchmark
start/end markers; exit counts alone are not a time profile. `PT_POWER_TRACE=1` records
bounded HID0/POW/PMU diagnostics; unset or `=0` disables them.

## Experimental PPC970 interpreter

`PT_BUILD_970=1 sh build.sh --test` also links `interp970.c` and `mmu970.c` into
the engine. Unset or `PT_BUILD_970=0` keeps the existing product selection. This
does not select a G5 board or make the application boot Leopard. `jit_run`
deliberately rejects a 970 CPU; use `interp_step` with an attached CPU for
firmware and instruction bring-up. The ordinary build tests include the focused
970 test even when the product does not link the experimental interpreter.

The bridge preserves every existing G4 field offset. Its appended high halves
are an interpreter compatibility layout, not a proposed native JIT layout.
It shares this project's integer, HPT and reservation implementation with
`experimental/g5`, and reuses the existing FP and AltiVec arithmetic.

Integration contract:

1. Allocate zero-initialized `CPU` objects and nonoverlapping RAM/ROM banks.
   Create one `Pt970Bus` with `ppc970_bus_create` for all CPUs and DMA writers.
   Banks use full-width physical addresses and remain alive until bus destruction.
2. Set each CPU's `env_ticks970` before `ppc970_attach`. It returns a monotonic
   virtual oscillator in timebase ticks, before the writable TB offset. Pausing
   the machine must pause this oscillator. DEC must not come from `env_irq`:
   the 970 path uses that callback only for `PEND_EXT`.
3. Call `ppc970_attach(cpu, bus, pir, boot_hior)` before starting threads; PIR is
   unique and below four. `boot_hior` is a board decision, aligned to 1 MiB and
   within the implemented 42-bit physical address range. Reset starts at
   `boot_hior + 0x100`, with SF and the nonpartitioned MSR bit 3 set.
4. Route every RAM write, including DMA, through `ppc970_bus_write`. A direct
   host pointer store bypasses reservation invalidation and is not supported.
   Canonicalize board aliases before this bus. ROM banks are read-only.
5. Supply `env_mmio970` for devices; zero means success, nonzero means bus error.
   Bytes are in guest memory order. The legacy MMIO callback is a compatibility
   fallback only below 4 GiB. Existing 32-bit translation callers refuse a
   high physical result rather than truncate it; those callers still need a
   full-width platform ABI before high RAM can be used by the entire product.
6. One host owner mutates each CPU. Attach all owners before execution; join
   them before destroying the bus, then free CPUs and bank storage. This API
   does not implement CPU hotplug or a whole-machine warm reset.
7. `ppc970_execute` holds a shared instruction gate; global `tlbie`, `tlbsync`
   and `icbi` take it exclusively to drain older accesses, including accesses
   that have already translated their address. Fetch is also protected.
   Platform code performing an extra CPU-side translation/access outside the
   interpreter must bracket that entire operation with `ppc970_bus_enter(bus, 0)`
   and `ppc970_bus_leave(bus)`. Do not nest either entry around `interp_step`
   or `ppc970_execute`, and do not reenter a CPU from an MMIO callback. Direct
   physical DMA uses the bus API without taking this instruction gate.

`sh tools/check-g5-core.sh` runs ASan/UBSan instruction, MMU, FP and 1/2/4-owner
tests without a guest OS. Use `PT_970_SANITIZER=thread` for the concurrency test,
and `python3 tools/check-g5-core-negative.py` for deliberate semantic faults.

The interpreter reports an unsupported operation with halt status 78 and the
exact PC/instruction; it does not silently continue. Unimplemented table state
stops with status 70, and a machine check with ME clear stops with status 71.
NI arithmetic, enabled precise FP traps, tracing, SCOM, performance monitoring,
active data watchpoints, deep nap and some instruction forms remain unsupported.
All FP opcodes currently stop when NI is set, including FPSCR control forms;
this conservative restriction must be narrowed when the NI contract is added.
The HPT walk is uncached and the shared RAM bus serializes accesses. These are
correctness tools, not a performance backend or evidence of Leopard compatibility.

The 970 alignment paths preserve DSISR except its store bit, handle cache-inhibited
multiple/string operations, and distinguish the HID5 illegal-dcbz mode from an
alignment fault. Cache flush instructions still validate their data translation;
cache timing and most HID implementation modes are not modeled. The mode-ring
alternative that makes cache-inhibited dcbz a no-op is not implemented.
VMX diagnostic counters are per host owner; collect them before each owner exits.
