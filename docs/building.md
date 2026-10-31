# Building

Ptolemy has four parts that are built separately: the engine, the firmware ROM, Ptolemy Tools (which run inside
Tiger) and the app.

## The engine

On a Mac with Apple silicon, the Xcode command line tools and Python 3:

```sh
sh build.sh            # build/ptolemy-engine
sh build.sh --test     # the same, then the unit and contract tests
```

`build.sh` builds the device library and its tests; `tools/build-optics.sh` builds the engine itself (Epicycle,
Optics, the devices and the window) and signs it with the hypervisor entitlement.

Run a machine from the command line:

```sh
build/ptolemy-engine --rom ROM --disk Tiger.sparsebundle --radeon-fast --audio --network-user --of 'auto-boot?=true' \
    --window --seconds 0
```

`--seconds 0` runs until Tiger shuts down. Other options: `--cpu 750|7400|7447a`, `--ram-mb N`, `--nvram FILE`,
`--cd IMAGE`, `--boot-cd`, `--overlay` (keep disk writes in memory), `--shots DIR SECONDS` (a screenshot every SECONDS),
`--input-script FILE` (scripted input for tests). The engine prints its options when given an unknown one.

## The firmware ROM

See [firmware/README.md](../firmware/README.md). It is built on Linux with a PowerPC cross compiler and is
reproducible.

## Ptolemy Tools

Ptolemy Agent is compiled inside Tiger with the GCC 4.0 and Mac OS X 10.4 SDK from your own copy of Xcode 2 for
Tiger; neither is part of Ptolemy.

1. Extract the compiler on the Mac from the Xcode installer's `Packages` folder:
   `python3 tools/extract-tiger-toolchain.py /Volumes/Xcode\ Tools devkit`
2. Make a local development medium that carries the compiler and Ptolemy's guest sources:
   `python3 tools/build-guest-media.py --devkit devkit --out dev-tools.dmg`
3. Insert it in a running machine (Media menu) and open its `install.command` in Tiger. It builds Ptolemy Agent
   with `guest/build-tiger.sh` and installs it.
4. Copy `build/tiger/PtolemyAgent.app` from Tiger back to the Mac and make the release medium, which contains no
   Apple files: `python3 tools/build-guest-media.py --binaries DIR --out PtolemyTools.dmg`

The development medium contains Apple's compiler: keep it to yourself.

## Ptolemy.app

```sh
PT_PRODUCT_ENGINE=build/ptolemy-engine PT_PRODUCT_ROM=ROM PT_PRODUCT_TOOLS=PtolemyTools.dmg \
    sh tools/build-product.sh --test
```

The app is `build/Ptolemy.app`, signed ad hoc. Without the three inputs it builds an app that cannot start a machine.
It needs macOS 26 or later.

## Tests

`sh build.sh --test` runs the device, CPU and Optics tests that need no guest. `tests/` also holds tests that need a
running Tiger or a Linux AArch64 host; [epicycle/README.md](../epicycle/README.md) describes the CPU engine's.
