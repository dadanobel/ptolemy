# Experimental G5 execution and integration module

2026-11-05. Apple Silicon only. **This module does not boot Leopard and is not a production G5 JIT.**

It connects previously independent 970MP helpers into an executable reference machine.
One, two and four persistent CPU owners execute PowerPC instructions against the same
memory, reservation and interrupt domains. This is suitable for integration, fault
reproduction and differential checks while the production backend is widened.

## Build and check

From the Ptolemy source root:

```sh
sh experimental/g5/tools/build-library.sh
sh experimental/g5/tools/check.sh
sh experimental/g5/tools/check-runtime.sh
python3 experimental/g5/tools/negative_controls.py
PT_G5_BUILD_DIR=build-tsan PT_G5_SANITIZER=thread sh experimental/g5/tools/check-runtime.sh
```

`PT_G5_TESTS=1 sh build.sh --test` includes the non-TSan G5 checks in the normal product
test entry point. Unset/`=0` leaves experimental checks off. It does not enable a G5
model in the app. Library output is `experimental/g5/build/libptg5.a`; `cpu-layout.json`
records actual canonical state offsets for a native backend. No ROM, SDK or guest
software is supplied. Build outputs stay outside the patch package.

## What can be connected now

- `src/g5_cpu.h/.c`: canonical 64-bit GPR/PC/LR/CTR/MSR and save/restore state,
  raw FPR/vector storage, a reference instruction dispatcher, precise implemented
  storage faults and privilege/unavailable exceptions. `rfid` preserves the high PC
  in SF=1; SF=0 wraps effective addresses without truncating GPR storage. Unknown
  implementation paths stop with an explicit result and exact PC/opcode.
- `src/g5_machine.h/.c`: per-owner execution, bounded instruction batches, shared
  RAM/DMA, MPIC connection, global invalidation tickets, deferred `tlbsync`, joined
  teardown and acknowledged stop/restart. RAM callbacks keep 64-bit addresses.
- `pt_g5_machine_timebase_enable`: per-core TBEN connection for the eventual board
  platform function/GPIO. It gates the selected CPU's TB and DEC source; other CPUs
  continue. `mttbl/mttbu` change the CPU's writable TB offset, not DEC.
- `g5_memory`, `g5_mmu`, `g5_dart`, `g5_pic`, `g5_dec`, `g5_of`: existing tested
  foundation. The HPT walker now exports precise protection/fetch reason bits to
  the CPU exception boundary. PTE.C still gates native writable mappings.
- `g5_owners`/`g5_fabric`: stop completion is queryable/waitable after coordinator
  acknowledgment, not merely after a backend started processing STOP.

`tests/machine.c` is a complete embedding example. Supply RAM banks, one thread-safe
monotonic virtual oscillator, an optional MMIO adapter, CPU count and initial MSR.
Open the machine, load bytes through `pt_g5_machine_dma_write`, then start owners.
Stop before taking an architectural snapshot; close before freeing bank storage.
Start/stop/close are controller operations, not guest CPU callbacks.

For firmware secondary startup, connect the **actual** reset/platform-function
protocol to this API. The test's arbitrary entry address and r3 argument are a
fixture interface, not proof of the MacRISC4 reset protocol.

The supplied tests execute a contended PPC `ldarx/addi/stdcx./bc` workload at physical
address `0x200000000` using 1/2/4 owners, with global `tlbie/tlbsync` and restart.
They verify the exact final count and each owner's completion. Only small backing
banks are allocated: this is high-address validation, not an 8-GiB RAM soak test.

## Deliberate boundaries

This reference backend walks translations on each access and serializes RAM updates
through the conservative reference bus. **It is a correctness tool, not a speed
upgrade.** Its TLB job advances generation because it has no native mappings or
compiled links. A JIT adapter must replace that operation with actual invalidation
before acknowledging. Making the acknowledgment early is a tested failure.

Implemented instruction families include the earlier integer64 subset, immediate
and indexed integer loads/stores, update/sign-extension/byte-reverse forms, word and
doubleword reservations, branches, common SPRs, MSR changes, `rfid`, segment/SLB
operations and conservative TLB synchronization. Coverage is **not** the complete
970MP instruction set. FP/VMX execution, HID side effects, SCOM, trace/debug/HDEC,
power states, remaining integer/cache instructions and other unsupported forms
still stop; unavailable exceptions are handled when FP/VMX are disabled.

The CPU state is an experimental canonical ABI, not a complete validated 970 register
model. There are no production JIT recovery maps, native ARM table roots or ASIDs
in this new machine. The earlier eleven ARM leaf lowerings and optional HVF fixtures
remain separate. Four owner threads are not four pinned performance cores.

The motherboard is not implemented here: SMU, U4/CPC945 register personality, DART
register bank, PCIe/HT topology, Mac platform functions and OpenBIOS/BootX handoff
must be connected and accepted. No factory PowerMac identity is fabricated.

## Production integration order

1. Add this directory unchanged in the experimental source area and keep G4 as the
   regression oracle. The source package pins preimages; do not replay old G4
   `tlbie` experiments over the accepted segment-key fix.
2. Build a per-CPU native engine context around `PtG5Cpu`, using the generated
   layout. Never cast it to `epicycle/ppc.h:CPU`: that structure has 32-bit PC,
   LR/CTR, physical-region bases and device callback addresses. The current JIT
   and HVF globals also cannot be shared between four owner threads.
3. Implement the missing decode/SPR/exception ledger in dependency order using
   reference-vs-native full-state tests. Add recovery state before enabling stores,
   faults or interrupts in translated regions. Port accepted FP algorithms rather
   than bypassing FPSCR for a score.
4. Native MMU: include all mapping context, PTE permissions/history, instruction
   versus data access, owner and reset epoch. A G4-style fixed engine window at
   8 GiB cannot simply coexist with arbitrary 64-bit guest EAs. Use guarded native
   address windows plus a full-width slow path, or an explicit guest-segment-to-ARM
   projection. Never truncate high bits to make an ARM virtual address fit. Keep
   code identity and data mappings distinct. Use a machine-wide ASID policy and
   complete required invalidations before recycling any root or identifier.
5. Connect the platform: coherent RAM/ROM/MMIO map and OF memory cells, MPIC
   destinations, actual secondary reset entry and per-core TBEN. Route every DMA
   writer through the same coherency/reservation protocol. A legacy 32-bit device
   address must go through DART or be rejected; it must not be silently narrowed.
6. Boot one G5 with small RAM and an updated Tiger/Leopard guest, then validate
   high RAM, then two and four active guest CPUs. Report actual BootX/XNU milestones.
   App integration and performance acceptance follow successful boot/correctness.

Apple's published `MacRISC4CPU::start`, `startCPU`, `signalCPU` and
`enableCPUTimeBase` show why reset, IPI routing and timebase platform services are
required. The public default branch is evidence for the interface, not certification
of the owner's shipping kext. Pin the appropriate guest version before firmware work.
[Apple source](https://github.com/apple-oss-distributions/AppleMacRISC4PE/blob/main/MacRISC4CPU.cpp).

CPU semantics used here: IBM 970MP UM 2.3, sections 2.1, 4.3–4.5 and 5; PEM64 3.0,
exception tables and instruction definitions. These manuals document 64-bit GPR
storage in SF=0, HIOR relocation, preserved reservations across exceptions and the
negative-level DEC behavior. [970MP manual](https://datasheet.datasheetarchive.com/originals/library/Datasheets-SW3/DSASW0048145.pdf),
[PEM64](https://ftp.jaist.ac.jp/pub/NetBSD/misc/macallan/pem_64bit_v3.0.2005jul15.pdf).

`RESEARCH-2026-11-04.md` preserves the earlier architectural research. Its coverage
table predates this connected executor; this README describes the current module.
