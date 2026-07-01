# LplKernel / LplPlugin — Contribution Guidelines (DRAFT)

> Draft derived from the conventions already in force across the P0–P6
> convergence work. Review and amend together; nothing here is frozen.

## 1. The two repositories

- **LplPlugin** is the single source of truth for all engine logic (math, ECS,
  physics, render, scene, image, platform, gpu, …). It stays cross-platform and
  builds on Windows/macOS/Linux via **xmake**.
- **LplKernel** is the freestanding OS that links LplPlugin as a native module
  (`libengine.a`, Model B) plus a thin C graphics/platform **HAL**. The kernel
  contains **zero** renderer/scene/ECS/math logic.
- Edit the **submodule** `LplKernel/LplPlugin` for the kernel port — never the
  sibling checkout. The sibling is a separate working tree.

## 2. Determinism contract (HARD — non-negotiable)

- Authoritative state is **Fixed32 (Q16.16) / CORDIC** and must be
  **bit-identical** across the Linux oracle and the i686 kernel.
- Float is allowed **only** in non-authoritative render paths, compiled
  `-msse2 -mfpmath=sse -ffp-contract=off -fno-math-errno`. The host oracle uses
  the same flags. No float result may flow back into authoritative Fixed32.
- **No libm / builtin transcendentals** in engine code linked into the kernel
  (`tanf`/`powf`/`expf` forbidden — neither freestanding-linkable nor
  deterministic). Derive `tan(fov/2)` from CORDIC; use integer-exponent
  repeated-square `intPow`; only hardware `sqrt` (sqrtss) is used directly.
- Guard `__int128` / `Fixed64` behind `LPL_NO_INT128` (absent on i686).

## 3. Per-feature ("slice") workflow

Every engine slice ships as a complete, verifiable unit:
1. Engine header(s)/source in the relevant LplPlugin module.
2. A Linux **parity test** (`tests/parity/test_*.cpp`) that prints the oracle
   signatures.
3. A `libengine/src/*_smoke.cpp` that reproduces the slice in-kernel and folds
   the same signatures (FNV-1a, offset `0x811C9DC5`, prime `0x01000193`).
4. **Both** kernel build paths updated: the xmake glob (auto) **and** the
   `libengine/Makefile` `LOCAL_OBJS` list (explicit).
5. QEMU boot proves the in-kernel folded signature equals the Linux oracle
   **bit-for-bit**.

Diagnostic smoke output lives in `kernel/core/libengine_smoke.c` (one block per
slice), **not** in `kernel.c`. The whole battery is gated by
`LPL_KERNEL_ENABLE_SMOKE_TESTS` and is compiled out of release images.

## 4. Naming

- **Spell out acronyms fully** in identifiers
  (`peripheral_component_interconnect_*`, not `pci_*`).
- C kernel code: `snake_case`, files under `kernel/...`.
- C++ engine code: existing LplPlugin style (`lpl::module::Type`, `PascalCase`
  types, `camelCase` methods).

## 5. Build & toolchain

- Kernel builds two ways: the zero-dependency shell scripts
  (`./build.sh` + Makefiles) and native **xmake** (`xmake f -m debug|release`).
  Keep them in lock-step; a new kernel source must be added to both.
- Cross toolchain: **i686-elf-gcc 14 / C++23** (`~/opt/cross14`, overridable via
  `$CROSS_BIN`). A move to x86_64 long mode is under evaluation.
- **debug** mode ships the smoke battery; **release** compiles it out. The
  determinism opt level is pinned to `-O2` in both modes so signatures never
  depend on the mode.
- **Dev loop:** `xmake` (build), `xmake iso`, `xmake qemu` (run), `xmake debug`
  (build + QEMU halted on `tcp::1234` for gdb). VSCode **F5** runs `xmake debug`
  then attaches gdb to `build/.../debug/lpl.kernel` and breaks at `kernel_main`;
  it prompts for profile (server/client) × keyboard × smoke.
- **Optional engine:** the LplPlugin module is auto-detected
  (`os.isdir(core/include)` in xmake; `ENABLE_LIBENGINE` from `config.sh`). When
  absent, the kernel still builds + boots standalone (`-lengine` dropped, smoke
  battery forced off, `LPL_PLUGIN_UNAVAILABLE` defined) — the no-xmake/no-plugin
  fallback.

## 6. Build targets / run modes

LplKernel is built in two specialised profiles, sharing one taxonomy:
- **server** — tty/console, deterministic authoritative tick (`REALTIME_MODE`
  off / buddy PMM).
- **client** — graphics (HAL display), future BCI & VR (`GRAPHICS_MODE` /
  `REALTIME_MODE` / Free-List PMM).

## 7. Git / commits

- Commits authored **MasterLaplace**, **GPG-signed** (`-S`).
- **No** `Co-Authored-By` trailers and **no** AI-generated footers.
- Never commit `.claude/`. `docs/` is gitignored (scratch/backups/plans live
  there).
- Push only via SSH, only on explicit request — never prompt to push.

## 8. Out of scope (deferred deliberately)

- Full `Engine.cpp` freestanding reparent (pulls in net/sockets/BCI/multiplayer)
  — host-only for now; the render path is already proven freestanding via
  `libengine`.
