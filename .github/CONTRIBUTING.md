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

- An engine change is made in LplPlugin, in its checkout next to this one (`../LplPlugin`).
  [LplAssistant](https://github.com/Christian-guajardo/LplAssistant) and
  [LplKnowledge](https://github.com/MasterLaplace/LplKnowledge) are siblings too, one working tree
  per repository. `LPLPLUGIN_ROOT`, `LPLASSISTANT_ROOT` and `LPLKNOWLEDGE_ROOT` point at another
  checkout, and `none` builds without that layer: without LplPlugin the kernel is plain C, without
  the other two it boots without a mind or a memory.
- `DEPENDENCIES.lock` names the commit of each sibling this kernel was tested with, and CI fetches
  those commits and no others (`tools/deps.sh fetch`). `tools/deps.sh status` says how far your
  checkouts are from it. When a kernel change needs a change in a sibling, the sibling's pull request
  is merged first; then `tools/deps.sh lock` records the merged commit, in the kernel's pull request.
- The link order is `-lknowledge -lassistant -lengine -lkxx -lk`. Each archive calls into the ones
  after it, and a static archive only resolves what is already pending when the linker reaches it:
  never reorder it.

## The determinism contract

It binds every line of engine code linked into the kernel, and it is not negotiable.

- Authoritative state is Fixed32 (Q16.16) and CORDIC, and it is bit-identical between the Linux
  oracle and the i686 kernel.
- Floating point is allowed only on paths that are not authoritative, such as rendering, compiled
  with the flags of `libengine/arch/i386/make.config`, which the host oracle must share
  (MasterLaplace/LplPlugin#388). No float result flows back into authoritative state.
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

1. The code, in its module of LplPlugin, LplAssistant or LplKnowledge.
2. A test, an `LPL_TEST` in that repository's `tests/<module>/`: it checks the feature's claims and
   measures the values both targets must agree on, folded as FNV-1a (offset `0x811C9DC5`, prime
   `0x01000193`). A debug kernel runs it after its own tests; a release image compiles it out. A
   test of what only this kernel provides goes in `libengine/tests/` or `libassistant/tests/`.
3. Every build list that names its neighbours names the new files too: `libengine/arch/i386/make.config`
   and `xmake.lua` list the engine sources by hand, `kernel/Makefile` and `kernel/arch/i386/make.config`
   the kernel's. A file left out fails at the kernel link with `undefined reference`, never at
   compile time (#119 tracks a single list). A module new to the kernel goes in `ENGINE_MODULES` of
   `libengine/Makefile` and `kEngineModules` of `xmake.lua`, which bring its headers and its tests;
   `ASSISTANT_MODULES` and `KNOWLEDGE_MODULES` (`kAssistantModules`, `kKnowledgeModules`) bring
   the tests of LplAssistant and LplKnowledge.
4. Compare the runs: `xmake run -P ../LplPlugin test-engine > engine.log`, the same for
   `test-assistant` and `test-knowledge` in their repositories, then `./qemu.sh --server | tee serial.log`
   until it prints `# Totals:`, then `tools/parity.sh engine.log assistant.log knowledge.log serial.log`.

A gate checks counters next to its signatures, and a control that must come out different: a run
that never exercises the feature folds just as well on both targets, and only the counters and the
control tell the two apart. A new boot measurement is printed as one
`[LPLTLM] <domain> key=value ...` line, whose values hold no space and no `=`.

## Numbering

Gates are written `gate P13 history`: the word, the number and the name, never `P13` alone. A gate
takes the next number when it ships, in delivery order, and a gate number is never reserved. Phases
are written `Phase 11`, never `P11`, so a phase and a gate never share a label.

## Kernel code

On top of the shared C and C++ rules:

- The version is written once, in `kernel/include/kernel/config.h`, a copy of the shared template:
  the header titles carry none. That file's requirement block names the oldest LplPlugin the kernel
  builds with, and the compiler refuses an older one. `libc/include/lpl_string.h` keeps its own block.
- An instruction worth reusing becomes a routine of `kernel/arch/i386/lib/asmutils.s`, declared and
  documented in `kernel/include/kernel/lib/asmutils.h`. Any other assembly is a `.s` or `.S` of its own
  under `kernel/arch/i386/`, listed in `kernel/arch/i386/make.config`.
- A kernel C header that C++ includes wraps its declarations in `extern "C"`. Without it the link
  fails on a mangled `_Z...` name.
- A kernel test is a `KERNEL_TEST(name)` in `kernel/tests/<area>/`, or `kernel/arch/<arch>/tests/<area>/`
  for architecture code, under one `KERNEL_TEST_SUITE` per file: no list to update. It checks its own
  claims with `kernel_test_check` and reports in KTAP, measures included. A test that halts the machine
  is `KERNEL_TEST_MANUAL` and runs only when the boot command line names it in full:
  `qemu-system-i386 -kernel kernel/lpl.kernel -append lpl.test=exceptions.breakpoint`.
- A switch no gate profile turns on is code nothing compiles. `tools/unbuilt-branches.sh` lists every
  source, `#if` branch, object and `-D` macro the gate profiles leave out, and fails on one that
  `tools/unbuilt-branches.declared` does not explain: a new one gets a profile that builds it, or a
  line there saying why not.

## Traps that are not checks yet

- `xmake` resolves its project from the current directory: run it at the root, or pass `-P <dir>`.
- `./iso.sh` rebuilds the profile it is given, so `./build.sh --server && ./iso.sh` produces a client
  image. Use `./iso.sh --server`, or `./pipeline_profiles.sh` for both profiles (#418).
- After a header is renamed or removed, stale `.d` files stop `make` with "No rule to make target":
  run `./clean.sh` (#420).
