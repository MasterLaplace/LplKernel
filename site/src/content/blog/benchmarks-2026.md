---
title: "Benchmarking the Laplace Engine: Methodology and First Real Numbers"
description: "A reproducible micro-benchmark harness for LplPlugin (allocators, ECS, fixed-point math, physics), with honest numbers, their variance, and the caveats that come with them."
date: 2026-07-06
kind: benchmark
draft: false
---

Every figure below comes from `lpl-benchmark`, an in-tree executable
(`apps/benchmark`) that anyone can build and run. Where a result is unflattering,
it stays in: a benchmark you only publish when it looks good is marketing, not
engineering.

## Methodology

A single timed run tells you almost nothing. It conflates the work you actually
care about with scheduler preemption, cold caches, CPU frequency ramp and, in an
optimised build, the compiler quietly deleting code whose result is never read.
The harness answers all four at once.

Warm-up runs prime the caches and branch predictors before anything is measured.
Each timed section then repeats until it reaches a wall-time budget, so a cheap
kernel collects thousands of samples and an expensive one at least a handful.
The numbers come out as a median (which shrugs off preemption outliers), a
coefficient of variation (CV, the relative spread), a min (the best case, where
interference is lowest) and a p99 tail. Finally, a `doNotOptimize()` /
`clobberMemory()` pair stands between the compiler and the result: it forces the
work to be computed and stored. That last point is not academic. It is precisely
why the earlier single-shot harness reported `0.000 ms` on tight numeric loops:
the optimiser had erased them.

### Test machine

| | |
| --- | --- |
| CPU | Intel Core Ultra 7 165H (22 logical cores) |
| RAM | 31 GiB |
| OS | Linux 6.6 (WSL2) on Windows |
| Compiler | GCC 15.2.0, C++23 |
| Build | Release: `-O2`/fastest, `-fno-exceptions`, `-fno-rtti` |

> **Caveat: read the variance, not just the median.** This run was captured
> under WSL2, where the CPU frequency governor is not exposed to the guest, so
> the cores throttle and boost freely mid-run. That inflates the CV on the
> shortest measurements (a few exceed ±20 %). Concretely, these are the numbers
> of a developer laptop, not those of a pinned, isolated, `performance`-governor
> CI box: read them as orders of magnitude and as ratios between approaches, not
> as absolute records. The harness prints the governor and flags it whenever it
> is not `performance`.

## Memory allocators

The same workload, 100 000 fixed 64-byte blocks, run through three strategies.
This is where the custom allocators have to earn their place against `malloc`.

| Allocator | Strategy | Median | vs malloc |
| --- | --- | --- | --- |
| Pool | intrusive free-list, per-block acquire/release | **441 µs** | 3.5× faster |
| Arena | bump-pointer alloc, single O(1) mass reset | 887 µs | 1.7× faster |
| malloc/free | libc baseline | 1.53 ms | 1.0× |

The pool wins because acquire and release are a single linked-list pop and push,
with no size-class lookup on the way. The arena does more work per call here,
since it recomputes alignment on each of the 100k allocations. Its real edge does
not even show up in a per-block micro-benchmark: it is the ability to reclaim the
whole slab in one pointer reset, which only pays off at end-of-frame.

## Fixed-point math

The engine keeps anything that must be deterministic across machines (physics,
spatial hashing) in Q16.16 fixed-point, because IEEE floating-point can differ
bit-for-bit from one CPU to another.

| Kernel | Median | Notes |
| --- | --- | --- |
| `Fixed32` multiply | 1.38 ms / 1M ops | ≈ 1.4 ns per multiply |
| Morton `encode3D` | 2.62 ms / 1M ops | 3D → Z-order interleave |

### CORDIC vs libm: an honest trade-off

`Cordic::sin` computes a sine with nothing but shifts and adds, so it needs no
FPU, which is the whole point on the freestanding i686 kernel target. It is
benchmarked over its convergent domain `[-π/2, π/2]`, since this CORDIC does no
argument range reduction and expects the caller to fold angles into that range
first.

| sin(x), 1M evaluations | Median | Accuracy vs libm |
| --- | --- | --- |
| CORDIC (Fixed32) | 10.7 ms | max abs err 1.8·10⁻⁴, RMS 3.3·10⁻⁵ |
| `std::sin` (double, libm) | 4.3 ms | reference |

The accuracy is excellent for a 16-bit fractional format. The speed is where you
have to be honest: on x86, CORDIC comes out about 2.5× slower than a modern
vectorised libm. What it buys is not throughput but determinism and independence
from the FPU (it runs identically in ring-0 with no floating-point unit enabled).
On the i686 kernel target with soft-float, the comparison flips the other way.

## ECS: entities and lookups

| Operation | Median | Rate |
| --- | --- | --- |
| Create + destroy 10k entities | 332 µs | — |
| Create 100k entities (batch) | 1.84 ms | ≈ 54M entities/s |
| SparseSet lookup (O(1)) | 468 ns / 1k lookups | ~0.47 ns each |
| Linear scan (O(n)) | 2.18 ms / 1k lookups | ~4600× slower |

That last row is the whole reason the ECS keys entities through a sparse set. At
10k entities, an O(1) resolution is already about 4600× faster than a naïve scan,
and since the scan is linear in the entity count, the gap only widens as the
world grows.

### Data layout: SoA vs AoS

Summing a single component (`x`) across 1M elements, Structure-of-Arrays against
Array-of-Structs:

| Layout | Median (best-case min) |
| --- | --- |
| SoA (`float[]`) | 453 µs (436 µs) |
| AoS (`Vec3[]`) | 562 µs (503 µs) |

SoA runs about 1.2 to 1.3× faster here because it touches only the cache lines it
needs, whereas AoS drags `y` and `z` through cache for nothing. The margin is
modest at this stride, and noisy under WSL, but it is the right sign, and it
grows with wider components or with real per-element work. This is why the ECS
stores components column-major.

## Physics

The CPU physics backend runs the full pipeline on every step: semi-implicit Euler
integration, then AABB collision detection with impulse resolution, then
sleeping. The collision pass is O(n²) per chunk, and it dominates everything
else.

| Entities | Median step | Throughput | Verdict @ 60 fps |
| --- | --- | --- | --- |
| 1 000 | 3.30 ms | 3.0·10⁵ ent/s | real-time |
| 2 000 | 6.74 ms | 3.0·10⁵ ent/s | real-time |
| 5 000 | 16.1 ms | 3.1·10⁵ ent/s | playable |
| 10 000 | 31.2 ms | 3.2·10⁵ ent/s | below 30 fps |
| 20 000 | 51.4 ms | 3.9·10⁵ ent/s | below 30 fps |

Read the two trends together: the throughput stays flat while the step time
roughly doubles with each doubling of the entity count. That is the quadratic
wall. Brute-force collision does not scale, and past a few thousand bodies a
single 16.6 ms frame budget is already gone. This is the measured motivation for
the broad-phase and GPU paths, rather than a number picked to look impressive.

### Broad-phase pays for itself

Isolating collision at 2 000 bodies, spatial partitioning against brute force:

| Method | Median |
| --- | --- |
| Broad-phase `queryRadius` (WorldPartition cells) | **588 µs** |
| N² AABB pair check | 4.58 ms |

A 7.8× win at 2k, and it grows with n. The spatial index that buys it is cheap to
build: populating a 10k-entity `WorldPartition` costs 1.42 ms.

> **A note on `WorldPartition::step()`.** Its CPU path is, by design, only a
> gateway that dispatches to the GPU: it does no integration. Timing it directly
> therefore returns a meaningless ~17 ns, which is the cost of a function call.
> The physics numbers above come from `CpuPhysicsBackend`, which does the real
> work. Measuring the gateway instead of the engine is exactly the mistake this
> harness exists to prevent.

## Concurrency & containers

| Operation | Median |
| --- | --- |
| `FlatAtomicHashMap` insert 16k | 313 µs |
| `FlatAtomicHashMap` get 16k | 327 µs |
| `FlatAtomicHashMap` forEach 16k | 318 µs |
| `FlatAtomicHashMap` forEach parallel 16k (4 threads) | 739 µs |
| `FlatAtomicHashMap` insert/remove churn 16k | 5.31 ms |
| `ThreadPool` dispatch 10k tasks | ~35 ms (high variance) |

Two results are worth stopping on. The parallel `forEach` is slower than the
serial one at 16k elements, which looks wrong until you count the work: a single
add per element cannot cover the cost of dispatching threads and bouncing cache
lines between them, so parallelism only starts to help once the per-element work
is heavy enough to hide that overhead. The `ThreadPool` figure is similar in
spirit: dispatching 10k trivial tasks is dominated by the per-task `std::future`
allocation, and the wide CV says as much. It stresses the queue, it does not
model a realistic workload.

## Reproducing

```sh
git clone --recursive <LplPlugin>
cd LplPlugin
xmake f -m release -y
xmake build lpl-benchmark
xmake run lpl-benchmark      # or: ./build/linux/<arch>/release/lpl-benchmark
```

The binary opens with its own system-info block (CPU, cores, RAM, OS, compiler,
build config, governor), so every set of results carries its own context. For
low-variance numbers, run it on a machine set to the `performance` governor with
the process pinned to an isolated core. The WSL2 figures here are, on purpose,
plain developer-laptop conditions.

## What's next

The network path (zero-copy from NIC to engine to GPU) is not measured yet: the
harness above is the template it will use once the in-kernel driver path is wired
end-to-end. When it lands, it gets the same treatment. Real numbers, their
variance, and the caveats, or it does not get published.
