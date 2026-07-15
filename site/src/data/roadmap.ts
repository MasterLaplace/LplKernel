// =============================================================================
//  ROADMAP — source of truth for the Laplace project roadmap page (/roadmap).
// =============================================================================
//
//  This file is HAND-EDITED. It is the single source of truth for the roadmap
//  shown on the site. It deliberately does NOT sync with the narrative docs
//  (LplKernel/docs/ROADMAP.md, the wiki Roadmap/Implementation-Status/
//  Future-Ideas pages, or the book) — those stay as human reference. When
//  something changes, edit the matching item HERE.
//
//  --- HOW TO EDIT (the "standard") --------------------------------------------
//  Add / change one entry in the `items` array below. Each entry is one object:
//
//    {
//      id: "kernel-p5",              // unique, stable slug (kebab-case). Used by
//                                    //   `dependsOn` links — don't rename lightly.
//      title: "Device drivers",      // short human label
//      project: "kernel",            // "kernel" | "plugin" | "convergence"
//      track: "Phase 5 · Device drivers", // grouping label within the project
//      phase: "P5",                  // short tag shown as a chip (optional)
//      status: "in-progress",        // "done" | "in-progress" | "planned" | "idea"
//      progress: 29,                 // 0..100, optional (drives the bar)
//      detail: "PS/2 + PCI done; ...",// one-line note (optional)
//      dependsOn: ["kernel-p3"],     // ids this builds on (optional). Cross-
//                                    //   project deps render as "merge" links —
//                                    //   this is the git-tree-with-merges idea.
//      tags: ["drivers"],            // free labels (optional)
//    }
//
//  Ordering: items render grouped by `project` then `track`, in first-seen
//  order below. So the order of this array is the order on the page.
//
//  The build will FAIL if `status`/`project` is misspelled or a type is wrong —
//  that's the safety net, treat a red build as "fix the typo".
// =============================================================================

export type Project = "kernel" | "plugin" | "convergence";
export type Status = "done" | "in-progress" | "planned" | "idea";

export interface RoadmapItem {
  id: string;
  title: string;
  project: Project;
  track: string;
  phase?: string;
  status: Status;
  progress?: number;
  detail?: string;
  dependsOn?: string[];
  tags?: string[];
}

// -- Display metadata ---------------------------------------------------------

export const PROJECT_META: Record<Project, { label: string; blurb: string; accent: string }> = {
  kernel: {
    label: "LplKernel",
    blurb: "The freestanding i686 OS — OSDev learning path, phases 0→11.",
    accent: "#ff8c00",
  },
  convergence: {
    label: "Convergence",
    blurb: "The two-repo merge: the engine linked natively into ring-0 (Model B, then U1→U5).",
    accent: "#ffb454",
  },
  plugin: {
    label: "LplPlugin",
    blurb: "The cross-platform engine — simulation, network, BCI, rendering.",
    accent: "#ff6b00",
  },
};

export const STATUS_META: Record<Status, { label: string; glyph: string; color: string }> = {
  done: { label: "Done", glyph: "✓", color: "#4ec9b0" },
  "in-progress": { label: "In progress", glyph: "▸", color: "#ff8c00" },
  planned: { label: "Planned", glyph: "○", color: "#7a9cc6" },
  idea: { label: "Idea", glyph: "◇", color: "#888888" },
};

export const PROJECT_ORDER: Project[] = ["kernel", "convergence", "plugin"];

// -- The roadmap itself -------------------------------------------------------

export const items: RoadmapItem[] = [
  // ===== KERNEL — OSDev phases ==============================================
  {
    id: "kernel-p0",
    title: "Prerequisites & environment",
    project: "kernel",
    track: "Foundations (Phase 0–4)",
    phase: "P0",
    status: "done",
    detail: "i686-elf cross-toolchain (gcc14/C++23), Makefiles + xmake, QEMU, GDB/VSCode F5.",
    tags: ["toolchain"],
  },
  {
    id: "kernel-p1",
    title: "Bare-bones kernel",
    project: "kernel",
    track: "Foundations (Phase 0–4)",
    phase: "P1",
    status: "done",
    detail: "GRUB multiboot, VGA text mode, serial COM1, scrolling, colors, multiboot parsing.",
  },
  {
    id: "kernel-p2",
    title: "CPU init & protection",
    project: "kernel",
    track: "Foundations (Phase 0–4)",
    phase: "P2",
    status: "done",
    detail: "Higher-half at 0xC0000000, boot + runtime paging, GDT (6 segments), TSS loaded. Ring 3 deferred to P9.",
  },
  {
    id: "kernel-p3",
    title: "Interrupts, APIC & SMP",
    project: "kernel",
    track: "Foundations (Phase 0–4)",
    phase: "P3",
    status: "done",
    detail: "IDT/ISR 0–47, PIC remap, dedicated #PF/#GP/#DF handlers, LAPIC/IOAPIC, x2APIC, MADT, AP bring-up (SMP=2 validated).",
    tags: ["smp"],
  },
  {
    id: "kernel-p4",
    title: "Memory management",
    project: "kernel",
    track: "Foundations (Phase 0–4)",
    phase: "P4",
    status: "done",
    detail: "PMM (buddy server / free-list client), kmalloc/kfree, slab caches, frame/pool/ring allocators, pinned DMA, VMM range manager.",
    tags: ["memory"],
  },
  {
    id: "kernel-p5",
    title: "Device drivers",
    project: "kernel",
    track: "Phase 5 · Device drivers",
    phase: "P5",
    status: "in-progress",
    progress: 35,
    detail: "PS/2 keyboard (QWERTY/AZERTY layouts) ✓, PCI bus enumeration + BARs ✓. Next: storage (ATA PIO), then USB.",
    dependsOn: ["kernel-p3"],
    tags: ["drivers"],
  },
  {
    id: "kernel-p5-nic",
    title: "e1000 network driver",
    project: "kernel",
    track: "Phase 5 · Device drivers",
    phase: "P5",
    status: "planned",
    detail: "Intel 82540EM at BAR0 0xFEB80000 (found by the PCI scan). Native replacement for /dev/lpl0 — entry point for the data-plane convergence.",
    dependsOn: ["kernel-p5"],
    tags: ["drivers", "net"],
  },
  {
    id: "kernel-p5-storage",
    title: "Storage & BCI input",
    project: "kernel",
    track: "Phase 5 · Device drivers",
    phase: "P5",
    status: "planned",
    detail: "ATA PIO first (simplest), then AHCI/NVMe. High-frequency USB/serial polling for the OpenBCI headset with native Notch/bandpass filtering.",
    dependsOn: ["kernel-p5"],
    tags: ["drivers", "bci"],
  },
  {
    id: "kernel-p6",
    title: "Multitasking & scheduling",
    project: "kernel",
    track: "Phase 6 · Scheduling",
    phase: "P6",
    status: "planned",
    detail: "Context switch (FXSAVE/XSAVE), round-robin then priority, EDF hard real-time (Liu & Layland) for VR guarantees, sync primitives, IPC.",
    dependsOn: ["kernel-p4"],
    tags: ["realtime"],
  },
  {
    id: "kernel-p7",
    title: "File systems",
    project: "kernel",
    track: "Phase 7–11 · Advanced",
    phase: "P7",
    status: "planned",
    detail: "VFS, initrd (no disk driver needed) first, then FAT / Ext2.",
    dependsOn: ["kernel-p5-storage"],
  },
  {
    id: "kernel-p8",
    title: "High-performance networking",
    project: "kernel",
    track: "Phase 7–11 · Advanced",
    phase: "P8",
    status: "planned",
    detail: "Ethernet/ARP/IP/TCP/UDP, then data-plane bypass (DPDK-style), zero-copy DMA NIC→pinned memory, Receive-Side-Scaling across SMP cores.",
    dependsOn: ["kernel-p5-nic"],
    tags: ["net"],
  },
  {
    id: "kernel-p9",
    title: "User space & syscalls",
    project: "kernel",
    track: "Phase 7–11 · Advanced",
    phase: "P9",
    status: "planned",
    detail: "Ring 3, ELF loading. Zero-syscall async interface (SPSC submission/completion rings), SASOS single-address-space, PKeys isolation.",
    dependsOn: ["kernel-p6"],
  },
  {
    id: "kernel-p10",
    title: "64-bit, UEFI & shell",
    project: "kernel",
    track: "Phase 7–11 · Advanced",
    phase: "P10",
    status: "planned",
    detail: "Long mode (4-level paging), UEFI boot, interactive shell, newlib/libsupc++ porting.",
    dependsOn: ["kernel-p9"],
  },
  {
    id: "kernel-p11",
    title: "Power management & green computing",
    project: "kernel",
    track: "Phase 7–11 · Advanced",
    phase: "P11",
    status: "idea",
    detail: "C-states (HLT/MWAIT), P-states, tickless kernel (NO_HZ), DVFS driven by the EDF scheduler, clock/power gating. Crucial for BCI/VR hardware.",
    dependsOn: ["kernel-p6"],
    tags: ["realtime"],
  },

  // ===== CONVERGENCE — Model B + U-track ====================================
  {
    id: "conv-modelb",
    title: "Model B: engine → libengine.a",
    project: "convergence",
    track: "Model B · engine in ring-0",
    status: "done",
    progress: 100,
    detail: "P0→P6 complete: the portable engine compiles -ffreestanding into the kernel behind a thin C HAL. All 7 oracle parity suites bit-identical Linux↔i686; QEMU boots with matching FNV signatures.",
    dependsOn: ["kernel-p4", "plugin-p2"],
    tags: ["determinism"],
  },
  {
    id: "conv-engine-reparent",
    title: "Engine.cpp reparent off GLFW/sockets",
    project: "convergence",
    track: "Model B · engine in ring-0",
    status: "planned",
    detail: "The last deferred Model-B item: the full facade still drags in the net/BCI/GLFW stack and stays host-only. The freestanding render path is already proven via libengine.",
  },
  {
    id: "conv-u1",
    title: "U1 · Deterministic foundations",
    project: "convergence",
    track: "Engine convergence (U1–U5)",
    phase: "U1",
    status: "planned",
    detail: "LplKernel provides the stable tick contract (clock_*) consumed by LplPlugin.",
    dependsOn: ["conv-modelb", "kernel-p3"],
  },
  {
    id: "conv-u2",
    title: "U2 · Simulation & network authority",
    project: "convergence",
    track: "Engine convergence (U1–U5)",
    phase: "U2",
    status: "planned",
    detail: "Robust prediction & reconciliation under 50–200 ms network jitter.",
    dependsOn: ["conv-u1", "plugin-p3", "kernel-p8"],
  },
  {
    id: "conv-u3",
    title: "U3 · Closing the BCI loop",
    project: "convergence",
    track: "Engine convergence (U1–U5)",
    phase: "U3",
    status: "planned",
    detail: "Telemetry + action with a measured motion-to-photon latency < 20 ms.",
    dependsOn: ["conv-u1", "plugin-p4", "kernel-p5-storage"],
    tags: ["bci"],
  },
  {
    id: "conv-u4",
    title: "U4 · Kernel-centric convergence",
    project: "convergence",
    track: "Engine convergence (U1–U5)",
    phase: "U4",
    status: "idea",
    detail: "Critical engine components integrated directly as kernel modules.",
    dependsOn: ["conv-u2", "kernel-p6"],
  },
  {
    id: "conv-u5",
    title: "U5 · Scaling & validation",
    project: "convergence",
    track: "Engine convergence (U1–U5)",
    phase: "U5",
    status: "idea",
    detail: "Load-test and validate the unified SMP architecture.",
    dependsOn: ["conv-u4"],
  },

  // ===== PLUGIN — engine phases =============================================
  {
    id: "plugin-p1",
    title: "Core engine foundations",
    project: "plugin",
    track: "Phase 1–2 · Foundations",
    phase: "1",
    status: "done",
    progress: 100,
    detail: "ECS + kernel ring buffer PoC, CUDA gravity kernel, pinned memory zero-copy. Validated: 62.55 µs latency, 0% loss, 495 pkt/s.",
  },
  {
    id: "plugin-p2",
    title: "Architecture refactor & modularization",
    project: "plugin",
    track: "Phase 1–2 · Foundations",
    phase: "2",
    status: "done",
    progress: 100,
    detail: "20 flat modules, Make→xmake (C++23, -fno-rtti/-fno-exceptions), Vulkan backend alongside the software rasterizer, engine facade, CUDA physics port.",
    tags: ["architecture"],
  },
  {
    id: "plugin-p3",
    title: "Simulation & network research",
    project: "plugin",
    track: "Phase 3 · Simulation & network",
    phase: "3",
    status: "in-progress",
    progress: 70,
    detail: "Done: authoritative server, client prediction, EntityRegistry, DAG scheduler, double buffering, octree broadphase. Left: SIMD physics, dirty-list, Hermite prediction, 50–200 ms latency tests, anti-tunneling.",
    dependsOn: ["plugin-p2"],
    tags: ["ecs", "net"],
  },
  {
    id: "plugin-p4",
    title: "BCI & neurofeedback research",
    project: "plugin",
    track: "Phase 4 · BCI",
    phase: "4",
    status: "in-progress",
    progress: 70,
    detail: "Done: OpenBCI Cyton driver, per-channel FFT/PSD, Schumacher R(t), Riemannian δ_R/Mahalanobis, NeuralMetrics. In progress: auto-calibration. Left: LSL outlet, OpenViBE boxes, feedback loop, motor-imagery decoding.",
    dependsOn: ["plugin-p2"],
    tags: ["bci"],
  },
  {
    id: "plugin-p5",
    title: "Massive simulation & infrastructure",
    project: "plugin",
    track: "Phase 5 · Massive simulation",
    phase: "5",
    status: "planned",
    progress: 20,
    detail: "Done: octree broadphase, AABB narrow-phase, impulse resolution. Target: 100k+ entities @ 60 FPS — GPU streaming, state compression, session RCU, server meshing, NUMA-aware allocators.",
    dependsOn: ["plugin-p3"],
    tags: ["scaling"],
  },
  {
    id: "plugin-p6",
    title: "Total immersion (FullDive)",
    project: "plugin",
    track: "Phase 6 · Immersion",
    phase: "6",
    status: "idea",
    detail: "Photorealistic rendering (NeRF or PBR), haptic feedback, spatial audio, custom RTOS for strict determinism, GPUDirect RDMA (NIC→VRAM).",
    dependsOn: ["plugin-p5"],
    tags: ["render", "haptic"],
  },
];

// -- Derived helpers (used by the page; no need to edit) ----------------------

export interface Track {
  name: string;
  project: Project;
  items: RoadmapItem[];
  progress: number; // 0..100, averaged over items that carry a progress/status
}

const statusWeight: Record<Status, number> = {
  done: 100,
  "in-progress": 50,
  planned: 0,
  idea: 0,
};

/** Group items into ordered tracks per project (first-seen order). */
export function tracksByProject(project: Project): Track[] {
  const order: string[] = [];
  const map = new Map<string, RoadmapItem[]>();
  for (const it of items) {
    if (it.project !== project) continue;
    if (!map.has(it.track)) {
      map.set(it.track, []);
      order.push(it.track);
    }
    map.get(it.track)!.push(it);
  }
  return order.map((name) => {
    const its = map.get(name)!;
    const pct =
      its.reduce((s, it) => s + (it.progress ?? statusWeight[it.status]), 0) / its.length;
    return { name, project, items: its, progress: Math.round(pct) };
  });
}

/** Overall counts by status, optionally scoped to a project. */
export function statusCounts(project?: Project): Record<Status, number> {
  const c: Record<Status, number> = { done: 0, "in-progress": 0, planned: 0, idea: 0 };
  for (const it of items) {
    if (project && it.project !== project) continue;
    c[it.status]++;
  }
  return c;
}

export const itemById = new Map(items.map((it) => [it.id, it]));
