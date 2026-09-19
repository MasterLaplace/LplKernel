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

export type Project = "kernel" | "plugin" | "convergence" | "assistant" | "knowledge";
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
  assistant: {
    label: "LplAssistant",
    blurb: "The local mind — inference in ring 3, directing a deterministic engine.",
    accent: "#ff9d4d",
  },
  knowledge: {
    label: "LplKnowledge",
    blurb: "The library — a corpus the engine can read, and the demon can cite.",
    accent: "#cc5500",
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

export const PROJECT_ORDER: Project[] = ["kernel", "convergence", "plugin", "assistant", "knowledge"];

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
    detail:
      "High-frequency USB/serial polling for the OpenBCI headset with native Notch/bandpass filtering. On the storage half the recommendation has changed: NVMe before ATA PIO. ATA teaches a dead protocol whose only virtue is being short; NVMe teaches the queue model that RDMA, io_uring, AF_XDP and virtio all reuse. The simplest thing today is the detour tomorrow.",
    dependsOn: ["kernel-p5"],
    tags: ["drivers", "bci"],
  },
  {
    id: "kernel-p5-nvme",
    title: "NVMe driver",
    project: "kernel",
    track: "Phase 5 · Device drivers",
    phase: "P5",
    status: "planned",
    detail:
      "Where three separate reports converge. NVMe has the SAME architecture as the RDMA verbs — a submission/completion queue pair in shared memory, an MMIO doorbell, descriptors pointing at physical pages, a phase bit — but its spec is free, QEMU emulates it exactly, and a minimal driver is under a thousand lines. Learning that shape here is what makes the shape reusable everywhere else.",
    dependsOn: ["kernel-p5"],
    tags: ["drivers", "storage", "data-movement"],
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
    status: "in-progress",
    progress: 40,
    detail:
      "kernel/power/ ships three modules: MONITOR/MWAIT with a HLT fallback, a tickless timer that REFUSES to engage while a World is running, and a frequency governor split in two so that the half which cannot be applied is counted as refused rather than silently skipped. Measured duty cycle per profile: 4 ‰ server, 0 ‰ client, 1 ‰ xmake, with 73 to 320 periodic interrupts avoided. What is left is on the track below — and it is mostly instruments, because what is not measured cannot be improved.",
    dependsOn: ["kernel-p3"],
    tags: ["realtime", "power"],
  },

  {
    id: "kernel-rocev2",
    title: "A software RoCEv2 endpoint",
    project: "kernel",
    track: "Phase 7–11 · Advanced",
    status: "idea",
    detail:
      "RDMA's engine lives in the NIC's silicon and QEMU removed every RDMA device in 9.1, so the driver route is closed. But RoCEv2 is InfiniBand transport inside ordinary UDP on port 4791 — Linux proves it is implementable in software with rdma_rxe. Writing the endpoint over our own NIC driver yields a parity gate whose OTHER SIDE IS A LINUX KERNEL, which is strictly stronger than a gate we write both halves of.",
    dependsOn: ["kernel-p5-nic", "kernel-p8"],
    tags: ["net", "parity", "data-movement"],
  },

  // ----- Power & data movement ---------------------------------------------
  {
    id: "kernel-wakeup-accounting",
    title: "Wake-up accounting & timer coalescing",
    project: "kernel",
    track: "The bottleneck is data movement",
    status: "planned",
    detail:
      "\"Who woke me up?\" — one counter per wake source, powertop's core idea in about thirty lines. Everything else on this track is blind without it: you cannot shorten a wake-up you cannot name. Coalescing rides along, because a tickless kernel that still fires ten timers one millisecond apart wakes ten times where once would do.",
    dependsOn: ["kernel-p11"],
    tags: ["power", "measurement"],
  },
  {
    id: "kernel-cstate-hints",
    title: "Actually sleeping deeply",
    project: "kernel",
    track: "The bottleneck is data movement",
    status: "planned",
    detail:
      "processor_sleep.c passes EAX = 0 to MWAIT, which is the shallowest hint: the kernel sleeps in C1 — clock stopped, power maintained — while book §9.5.1 claims C3/C6. That is the difference between attacking dynamic power and attacking leakage, i.e. between 0 % and the 30–70 % the book itself attributes to static power. The hint cannot be computed: it is model-specific, so it comes from ACPI _CST or a per-model table. Same fix guards CPUID.05H:ECX bit 1 before setting the interrupt-break extension.",
    dependsOn: ["kernel-p11"],
    tags: ["power", "cpu"],
  },
  {
    id: "kernel-monitor-wake",
    title: "Waking on a device write",
    project: "kernel",
    track: "The bottleneck is data movement",
    status: "planned",
    detail:
      "processor_sleep_until_write arms MONITOR on a device's write index and wakes when the DMA lands — no IPI, no IRQ. It is written, it is correct, and it has ZERO callers: the one mechanism that distinguishes this idle loop from everyone else's is an orphan. hal_audio.h already says the power floor will need it. First site: the HDA capture ring. Second: the NIC receive ring.",
    dependsOn: ["kernel-p11", "kernel-p5-nic"],
    tags: ["power", "drivers"],
  },
  {
    id: "kernel-aperf-mperf",
    title: "The governor's missing feedback loop",
    project: "kernel",
    track: "The bottleneck is data movement",
    status: "planned",
    detail:
      "There is not a single rdmsr anywhere in the kernel. The governor is FED its load as a parameter, and — the part that matters — it writes IA32_PERF_CTL and cannot know whether the silicon obeyed: firmware, HWP or a thermal cap may all ignore it. APERF/MPERF is the loop that closes it, and book §9.4.3 already describes it as though it were there.",
    dependsOn: ["kernel-p11"],
    tags: ["power", "cpu"],
  },
  {
    id: "kernel-platform-power",
    title: "Platform power: ASPM, APST, EEE",
    project: "kernel",
    track: "The bottleneck is data movement",
    status: "idea",
    detail:
      "Book §9 covers the silicon and stops at the edge of the package. An active PCIe link burns watts with no traffic; L1.2 turns off the PLLs, receivers and transmitters for a 10–100 µs exit. That latency makes it a PROFILE decision, not a global constant: fatal to a 1 kHz loop, free to a satellite sleeping between 40 ms frames. NVMe APST and 802.3az LPI are the same argument on the other two links.",
    dependsOn: ["kernel-p5-nvme"],
    tags: ["power", "drivers"],
  },
  {
    id: "kernel-energy-gate",
    title: "Joules per tick",
    project: "kernel",
    track: "The bottleneck is data movement",
    status: "idea",
    detail:
      "Twenty-one parity gates, and not one measures what the machine costs. Duty cycle is a proxy that cannot see the difference between C1 and C6 — the very defect above. RAPL gives real joules (MSR 0x606/0x611), but QEMU only exposes it under KVM on an Intel host and reports the whole host package, so this is a REGRESSION BUDGET on real hardware, not a parity gate: energy is not bit-identical across targets and never will be.",
    dependsOn: ["kernel-wakeup-accounting", "kernel-cstate-hints"],
    tags: ["power", "measurement"],
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

  // ===== PLUGIN — parity gates ==============================================
  //
  //  A SEPARATE track on purpose. The engine's research phases above are numbered
  //  1–6, and the determinism gates are numbered P6–P10; the two numbering schemes
  //  are unrelated, and "Phase 7" would read as the successor of FullDive rather
  //  than as the world-generation gate. So a gate is always written "gate P7", and
  //  it lives here rather than being appended to the phase list.
  //
  //  A gate is one claim: a signature folded on the Linux oracle equals the one
  //  the i686 kernel folds, bit for bit. It either holds or the build is red.
  {
    id: "plugin-gate-p6",
    title: "gate P6 — rendering & scene",
    project: "plugin",
    track: "Determinism gates",
    phase: "P6",
    status: "done",
    progress: 100,
    detail: "The first cross-ring gate: cube-pile state and image folds identical between the host oracle and the booted kernel.",
    tags: ["parity"],
  },
  {
    id: "plugin-gate-p7",
    title: "gate P7 — world generation",
    project: "plugin",
    track: "Determinism gates",
    phase: "P7",
    status: "done",
    progress: 100,
    detail: "One recipe, one world: entity count, state, height and biome signatures all fold identically host-side and in ring 0.",
    dependsOn: ["plugin-gate-p6"],
    tags: ["parity", "procgen"],
  },
  {
    id: "plugin-gate-p8",
    title: "gate P8 — the living world",
    project: "plugin",
    track: "Determinism gates",
    phase: "P8",
    status: "done",
    progress: 100,
    detail: "A simulation that RUNS under contract: population, genome, stigmergy and social folds over a whole ecological run.",
    dependsOn: ["plugin-gate-p7"],
    tags: ["parity", "ai", "ecology"],
  },
  {
    id: "plugin-gate-p9",
    title: "gate P9 — the endless world",
    project: "plugin",
    track: "Determinism gates",
    phase: "P9",
    status: "done",
    progress: 100,
    detail: "Chunked generation with no seams: a chunk generated alone folds like the same chunk generated among its neighbours.",
    dependsOn: ["plugin-gate-p7"],
    tags: ["parity", "streaming"],
  },
  {
    id: "plugin-gate-p10",
    title: "gate P10 — botany",
    project: "plugin",
    track: "Determinism gates",
    phase: "P10",
    status: "done",
    progress: 100,
    detail: "L-system growth under the same contract: conifer, broadleaf and shrub folds, with segment and leaf counts.",
    dependsOn: ["plugin-gate-p8"],
    tags: ["parity", "procgen"],
  },
  {
    id: "plugin-gate-p11",
    title: "gate P11 — erasure codes",
    project: "plugin",
    track: "Determinism gates",
    phase: "P11",
    status: "done",
    progress: 100,
    detail:
      "The first gate whose two sides run DIFFERENT code: the host XORs 128 bits at a time, ring 0 word by word, and the folds must still match — because addition over GF(2) is associative, commutative and free of rounding. The script fails if both sides take the same path.",
    dependsOn: ["plugin-gate-p10"],
    tags: ["parity", "codec"],
  },
  {
    id: "plugin-gate-p12",
    title: "gate P12 — the Rosetta plate",
    project: "plugin",
    track: "Determinism gates",
    phase: "P12",
    status: "done",
    progress: 100,
    detail:
      "Ten opcodes, a reference interpreter, and a specification written in bytes rather than prose. What is folded is that a machine rebuilt FROM the engraving runs the canonical program identically on both targets — a specification sufficient on one and not the other is sufficient for nobody.",
    dependsOn: ["plugin-gate-p11"],
    tags: ["parity", "rosetta"],
  },
  {
    id: "plugin-gate-p13",
    title: "gate P13 — the reconstructed past",
    project: "plugin",
    track: "Determinism gates",
    phase: "P13",
    status: "done",
    progress: 100,
    detail:
      "A confidence in Fixed32 decides which of two contradictory claims becomes the consensus view, so it is authoritative state: a rounding that differed between targets would give two histories from one corpus. Checked alongside the folds — the losing account is still reachable.",
    dependsOn: ["plugin-gate-p12"],
    tags: ["parity", "history"],
  },
  {
    id: "assistant-gate-p14",
    title: "gate P14 — the mind",
    project: "assistant",
    track: "Determinism gates",
    phase: "P14",
    status: "done",
    progress: 100,
    detail:
      "A transformer thinking in ring 0, and thinking the same thought as the host: eight-bit weights, Q16.16 activations, an exponential built from a shift and six Taylor terms, rotary angles from CORDIC. Weights, prompt, scores, residual stream, free tokens and grammar-constrained tokens each fold on their own.",
    dependsOn: ["plugin-gate-p13"],
    tags: ["parity", "inference"],
  },
  {
    id: "assistant-gate-p15",
    title: "gate P15 — the satellite",
    project: "assistant",
    track: "Determinism gates",
    phase: "P15",
    status: "done",
    progress: 100,
    detail:
      "The first gate whose consumers will not all be x86: a hosted room node, the kernel's satellite profile and an eventual microcontroller share no register and must still decide the same things about the same audio — when to send, when to stop, whether the wake word was heard, whether the node is hearing itself. Features come from a Haar cascade because a mel filterbank needs cosines and nothing in ring 0 may compute one.",
    dependsOn: ["assistant-gate-p14"],
    tags: ["parity", "audio", "power"],
  },
  {
    id: "assistant-gate-p16",
    title: "gate P16 — agency",
    project: "assistant",
    track: "Determinism gates",
    phase: "P16",
    status: "done",
    progress: 100,
    detail:
      "One turn of thought, folded stage by stage — and what it folds is a sequence of DECISIONS rather than a computation: which note survived a full memory, which action was reached for first, whether the turn ended in an answer or a question. Identity is data, not a fine-tune; a budget counts work and never milliseconds, because a wall clock makes a replay depend on how fast the machine was that day.",
    dependsOn: ["assistant-gate-p15"],
    tags: ["parity", "agent"],
  },
  {
    id: "assistant-gate-p17",
    title: "gate P17 — reasoning",
    project: "assistant",
    track: "Determinism gates",
    phase: "P17",
    status: "done",
    progress: 100,
    detail:
      "The same turn, with every move chosen by the transformer running in ring 0 under a grammar rebuilt from the world at each step — so an action already tried stops being spellable, and the anti-loop guard lives in the language rather than in a filter. Measured both ways: under grammar, zero illegal actions; the same weights generating unconstrained named a legal action 0 times out of 8.",
    dependsOn: ["assistant-gate-p16"],
    tags: ["parity", "agent", "inference"],
  },
  {
    id: "knowledge-gate-p18",
    title: "gate P18 — the corpus",
    project: "knowledge",
    track: "Determinism gates",
    phase: "P18",
    status: "done",
    progress: 100,
    detail:
      "The first gate that folds a TRANSLATION rather than a computation. The canonical corpus of gate P13 is baked into a .lplknow image by a host tool, read back in ring 0, and the history is rebuilt from what came back — so its timeline, chronicle and minority signatures must equal P13's own. An image that opened cleanly and had quietly rounded one confidence would pass every other check, and the consequence would be a different consensus about how a king died.",
    dependsOn: ["plugin-gate-p13", "assistant-gate-p17"],
    tags: ["parity", "knowledge", "format"],
  },

  {
    id: "plugin-gate-p19",
    title: "gate P19 — the caves",
    project: "plugin",
    track: "Determinism gates",
    phase: "P19",
    status: "done",
    progress: 100,
    detail:
      "The first gate whose subject is a WALKED world rather than a computed one. Three folds that fail differently — the warren itself, where the rock is, and what a body makes of it — plus the counters that give them meaning: a run where the body never enters folds perfectly on both targets and proves nothing. The control is the same warren with its mouth filled in: sealed_in = 0.",
    dependsOn: ["plugin-gate-p9"],
    tags: ["parity", "procgen", "world"],
  },
  {
    id: "plugin-gate-p20",
    title: "gate P20 — the journey",
    project: "plugin",
    track: "Determinism gates",
    phase: "P20",
    status: "done",
    progress: 100,
    detail:
      "The bridge between the corpus and the world: a dated constraint seeds someone, he walks of his own accord, and his arrivals return to the chronicle as Cause::Emergent — where a divergence score can judge them. The fixture attests a road to the FAR place while a nearer one stays unlinked, so a walk that ignored the corpus would arrive elsewhere first while every signature stayed perfectly stable.",
    dependsOn: ["plugin-gate-p13", "knowledge-gate-p18"],
    tags: ["parity", "history", "world"],
  },
  {
    id: "plugin-gate-p21",
    title: "gate P21 — the relief",
    project: "plugin",
    track: "Determinism gates",
    phase: "P21",
    status: "done",
    progress: 100,
    detail:
      "The first gate whose ground is not invented: real elevation tiles projected, resampled, baked into a .lplknow section, reopened and walked. It proves the ARITHMETIC agrees — projection, lookup, edge blend, detail, walk. The counters are what give the signatures meaning, and the control is the same world without a survey, which must come out DIFFERENT.",
    dependsOn: ["plugin-gate-p9", "knowledge-gate-p18"],
    tags: ["parity", "world", "format"],
  },

  // ===== ASSISTANT — Caine, then Jarvis =====================================
  {
    id: "assistant-caine",
    title: "Caine — the AI as artistic director",
    project: "assistant",
    track: "Caine · directing a world",
    status: "in-progress",
    progress: 60,
    detail: "The inversion: the model decides WHAT in a few hundred tokens, the deterministic engine decides HOW. Tool surface, JSON-Schema and GBNF grammar derived from one component declaration; deterministic critics instead of a vision model; every act journalled, so a session is undoable and replayable.",
    dependsOn: ["plugin-gate-p7"],
    tags: ["agent", "procgen"],
  },
  {
    id: "assistant-grammar",
    title: "Constrained decoding",
    project: "assistant",
    track: "Caine · directing a world",
    status: "done",
    progress: 100,
    detail: "The grammar is regenerated every step and only names what is callable now — a tool the world cannot perform is one the sampler cannot spell.",
    dependsOn: ["assistant-caine"],
    tags: ["agent"],
  },
  {
    id: "assistant-jarvis",
    title: "Jarvis — the local mind",
    project: "assistant",
    track: "Jarvis · the local mind",
    status: "planned",
    detail: "llama.cpp and whisper.cpp behind a process boundary, pgvector memory, deep research with GBNF-constrained tool calls. Inference stays in ring 3 and speaks to the kernel through the data plane.",
    dependsOn: ["assistant-caine"],
    tags: ["inference"],
  },

  {
    id: "assistant-paged-model",
    title: "The expert is a page",
    project: "assistant",
    track: "Jarvis · the local mind",
    status: "idea",
    detail:
      "Decode is bandwidth-bound — two floating-point operations per weight read — so tokens/s is bandwidth divided by ACTIVE weights. A Mixture of Experts decouples that from capacity: a 236B model reads only 21B per token, ending up 3.4× larger in memory than a dense 70B and 3.3× faster. Which makes paging the right frame: map the whole model, let a page fault pull an expert straight off NVMe by DMA, pin the shared experts and evict the specialists. And the part that exists nowhere else — the MoE router knows one token ahead which experts are needed, so the kernel can prefetch BEFORE the fault. Not statistical speculation: an exact oracle handed down by the application. Needs a .lplmind laid out for paging — experts on page boundaries, table in front — decided before the loader, not after.",
    dependsOn: ["assistant-gate-p14", "kernel-p5-nvme"],
    tags: ["inference", "data-movement"],
  },

  // ===== KNOWLEDGE — Alexandrie =============================================
  {
    id: "knowledge-corpus",
    title: "The corpus and its extraction",
    project: "knowledge",
    track: "Alexandrie · the library",
    status: "in-progress",
    progress: 35,
    detail: "Every algorithm the project rests on, traced back to the report that argued for it and forward to the code that implements it — the reverse index the book will be written from.",
    tags: ["research"],
  },
  {
    id: "knowledge-reader",
    title: "A library the engine can read",
    project: "knowledge",
    track: "Alexandrie · the library",
    status: "planned",
    detail: "A baked, bounded reader on the ring-0 side of the reader/writer line, so a world can cite what it was built from.",
    dependsOn: ["knowledge-corpus"],
    tags: ["pack"],
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
