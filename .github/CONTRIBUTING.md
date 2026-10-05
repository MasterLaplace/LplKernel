# Contributing to LplKernel

The flow (issues, branches, pull requests, commit titles, labels) and the C and C++ rules shared by
every Laplace repository are in the
[shared CONTRIBUTING](https://github.com/MasterLaplace/.github/blob/main/.github/CONTRIBUTING.md).
How to build, run and debug the kernel is in the [README](../README.md). This page adds what only
holds here.

## Where a change goes

LplKernel holds no engine logic. Rendering, scenes, the ECS, physics and the maths live in
[LplPlugin](https://github.com/MasterLaplace/LplPlugin), which the kernel links as `libengine.a`
behind the thin C layer of `kernel/include/kernel/hal/hal.h`.

- An engine change is made in LplPlugin. When it is meant for the kernel, make it through the
  `LplPlugin/` submodule: another checkout of LplPlugin is another working tree, and the kernel does
  not build from it.
- [LplAssistant](https://github.com/Christian-guajardo/LplAssistant) and
  [LplKnowledge](https://github.com/MasterLaplace/LplKnowledge) are sibling checkouts, found through
  `LPLASSISTANT_ROOT` and `LPLKNOWLEDGE_ROOT`. Without them the kernel builds and boots without those
  layers.
- The link order is `-lknowledge -lassistant -lengine -lkxx -lk`. Each archive calls into the ones
  after it, and a static archive only resolves what is already pending when the linker reaches it:
  never reorder it.

## The determinism contract

It binds every line of engine code linked into the kernel, and it is not negotiable.

- Authoritative state is Fixed32 (Q16.16) and CORDIC, and it is bit-identical between the Linux
  oracle and the i686 kernel.
- Floating point is allowed only on paths that are not authoritative, such as rendering, compiled
  with the flags of `libengine/arch/i386/make.config`, which the host oracle shares. No float result
  flows back into authoritative state.
- No libm and no builtin transcendental (`tanf`, `powf`, `expf`, ...) in code linked into the
  kernel: derive from CORDIC and use integer powers. The hardware square root is the only one called
  directly.
- i686 has no 128-bit integer: `Fixed64` and every `__int128` exist only where the compiler defines
  `__SIZEOF_INT128__`, so code linked into the kernel uses `Fixed32` alone.

Four consequences, each written down after it cost a real defect:

- **Whatever decides which branch the simulation takes is authoritative**, even when no arithmetic
  touches it: a confidence, a genome, a trait, an identifier, a coordinate. It is an integer or a
  Fixed32, never a float.
- **Two world-scale Q16.16 values are never multiplied together**: the product overflows near 181
  units. Compare with the larger of the two axis distances, divide before multiplying, or work in
  offsets local to a chunk.
- **No wall clock on a path that is replayed or compared.** A budget counts work (turns, tokens,
  steps), and a function whose output is compared takes its timestamp as a parameter.
- **A parity signature folds only fixed-width data.** Anything holding a pointer or a `size_t` (a
  struct size, an arena's occupancy) differs between a 64-bit host and i686: it is printed, never
  compared.

## A slice: one feature, proven on both targets

1. The engine code, in its LplPlugin module.
2. A host test in LplPlugin's `tests/` that prints the oracle signatures. It is a `test-*` target of
   `tests/xmake.lua`, and it ends on `ALL PASS (0 failures, N checks)`.
3. A smoke, `libengine/src/smoke/pNN_<name>_smoke.cpp`, that runs the same slice in ring 0 and folds
   the same signatures (FNV-1a, offset `0x811C9DC5`, prime `0x01000193`), reported from
   `kernel/kernel/testing/smoke_libengine.c`. The battery is behind `LPL_KERNEL_ENABLE_SMOKE_TESTS`
   and is compiled out of release images.
4. Every build list that names its neighbours names the new files too. The shell build lists every
   object: `libengine/arch/i386/make.config` for engine sources, `libengine/Makefile` for smokes,
   `kernel/Makefile` and `kernel/arch/i386/make.config` for the kernel. `xmake.lua` lists the engine
   sources by hand. A file left out fails at the kernel link with `undefined reference`, never at
   compile time (#119 tracks a single list).
5. Boot the debug kernel with `./qemu.sh --server`. The serial output prints in the terminal: each
   gate's line must equal the host test's line, bit for bit.

A gate prints counters next to its signatures, and carries a control that must come out different: a
run that never exercises the feature folds just as well on both targets, and only the counters and
the control tell the two apart. A new measurement is printed as one `[LPLTLM] <domain> key=value ...`
line, whose values hold no space and no `=`.

## Numbering

Gates are written `gate P13 history`: the word, the number and the name, never `P13` alone. A gate
takes the next number when it ships, in delivery order, and a gate number is never reserved. Phases
are written `Phase 11`, never `P11`, so a phase and a gate never share a label.

## Kernel code

On top of the shared C and C++ rules:

- The version in the header titles is the version of `kernel/include/kernel/config.h`, and bumping it
  updates every title. `libc/include/lpl_string.h` keeps its own block.
- An instruction worth reusing becomes a routine of `kernel/arch/i386/lib/asmutils.s`, declared and
  documented in `kernel/include/kernel/lib/asmutils.h`. Any other assembly is a `.s` or `.S` of its own
  under `kernel/arch/i386/`, listed in `kernel/arch/i386/make.config`.
- A kernel C header that C++ includes wraps its declarations in `extern "C"`. Without it the link
  fails on a mangled `_Z...` name.

## Traps that are not checks yet

- `xmake` resolves its project from the current directory: run it at the root, or pass `-P <dir>`.
- `./iso.sh` rebuilds the profile it is given, so `./build.sh --server && ./iso.sh` produces a client
  image. Use `./iso.sh --server`, or `./pipeline_profiles.sh` for both profiles (#418).
- `xmake iso` packs the first `lpl.kernel` it finds under `build/`, debug or release (#419).
- After a header is renamed or removed, stale `.d` files stop `make` with "No rule to make target":
  run `./clean.sh` (#420).
