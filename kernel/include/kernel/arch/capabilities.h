/**
 * @file capabilities.h
 * @brief What a target HAS, declared once, so portable code stops assuming.
 *
 * Four axes, and confusing them is what makes a multi-architecture tree rot:
 *
 *   1. ISA / MODE      — how the processor is programmed: instruction encoding,
 *                        page table format, interrupt entry, register width.
 *                        `arch/i386`, `arch/x86_64`, `arch/arm64`, `arch/riscv64`,
 *                        `arch/xtensa`.
 *   2. PLATFORM        — what hardware is on the board: interrupt controller,
 *                        timers, buses, devices, firmware tables.
 *                        `arch/x86` (the PC), `arch/raspberry_pi`, `arch/esp32`,
 *                        `arch/apple_silicon`, `arch/virt`.
 *   3. ACCELERATOR     — what the kernel can HAND WORK TO and does not run on:
 *                        a GPU, an NPU, and one day a quantum processing unit.
 *                        Optional, enumerable, never the thing that boots.
 *   4. PROFILE         — what the image is for. Already exists and is NOT an
 *                        architecture: `--server`, `--client`, `--satellite`.
 *
 * A TARGET is a PAIR of the first two axes, which is why the declarations below
 * are named for both. A Raspberry Pi and an Apple M-series are the same ISA
 * (arm64) and share nothing else; an AMD desktop and an Intel desktop are the same
 * ISA *and* the same platform — "AMD" is not a port, it is the target that already
 * exists under another name. A VR headset is not an architecture at all: a
 * standalone one is arm64 on an Android platform, a tethered one is a display
 * attached to a host that is already supported. That is a profile.
 *
 * ── Where a quantum processor goes, and why it is not axis 1 ────────────────
 *
 * On axis 3, with the GPU. A QPU has no instruction pointer to boot, no page
 * table to install, no interrupt to take and no stack to switch: a kernel does not
 * RUN on one, it submits a circuit and collects a measurement. Giving it an
 * `arch/` directory would model it as something to boot on, which is the one thing
 * it can never be. Chapter 8 of the book describes the scheduler-side of this —
 * decoherence-aware ordering, error correction, the no-cloning constraint on state
 * copies — and every bit of it lives above an accelerator seam, not below a mode
 * layer.
 *
 * And the determinism contract already decides the hard part: a measurement is
 * irreducibly probabilistic, so a QPU result can never feed authoritative Fixed32
 * state. It is subject to exactly the rule float already obeys — usable in the
 * non-authoritative half, never in the half that must fold bit-identically on two
 * machines.
 *
 * ── Why capabilities and not #ifdef per target ──────────────────────────────
 *
 * Portable code must never ask "am I on x86". It must ask "do I have an MMU",
 * because that is the question that changes what it should do, and because the
 * answer stays right when a sixth target appears. A target declares what it has;
 * portable code tests the capability. Adding a target then means writing one
 * declaration and the code behind it, never editing portable code.
 *
 * These are compile-time, not runtime: one image targets one machine, and a
 * runtime branch would carry the dead half into an image that has 200 KiB to
 * spare.
 *
 * ── The honest part ────────────────────────────────────────────────────────
 *
 * Declaring a capability does not implement it. `tools/arch_conformance.sh`
 * reports, per target, how much of the arch contract is actually provided — so a
 * directory that declares a lot and defines nothing reads as 0 %, and can never be
 * mistaken for a working port.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#ifndef KERNEL_ARCH_CAPABILITIES_H
#define KERNEL_ARCH_CAPABILITIES_H

/*
** The target's own declaration. `KERNEL_ARCH_TARGET_<name>` is defined by the
** selected arch's make.config, which is the single entry point the build already
** uses (`arch/$(HOSTARCH)/make.config`). One include here, one file per target,
** and no include-path juggling.
*/
#if defined(KERNEL_ARCH_TARGET_I386_PC)
#    include <kernel/arch/targets/i386_pc.h>
#elif defined(KERNEL_ARCH_TARGET_X86_64_PC)
#    include <kernel/arch/targets/x86_64_pc.h>
#elif defined(KERNEL_ARCH_TARGET_ARM64_RASPBERRY_PI)
#    include <kernel/arch/targets/arm64_raspberry_pi.h>
#elif defined(KERNEL_ARCH_TARGET_ARM64_APPLE_SILICON)
#    include <kernel/arch/targets/arm64_apple_silicon.h>
#elif defined(KERNEL_ARCH_TARGET_RISCV64_VIRT)
#    include <kernel/arch/targets/riscv64_virt.h>
#elif defined(KERNEL_ARCH_TARGET_XTENSA_ESP32)
#    include <kernel/arch/targets/xtensa_esp32.h>
#else
#    error "No KERNEL_ARCH_TARGET_* defined: arch/<isa>/make.config must declare one."
#endif

/*
** Every capability below must be defined to 0 or 1 by the target header. They are
** listed here rather than left to each target so that adding a capability is a
** change to ONE list, and so a target that forgets one fails to compile instead of
** silently reading as absent — `#if UNDEFINED_MACRO` is 0 in C, which is exactly
** how a capability check turns into a lie.
*/
#if !defined(KERNEL_ARCH_HAS_MEMORY_MANAGEMENT_UNIT) || !defined(KERNEL_ARCH_HAS_PAGING) ||                            \
    !defined(KERNEL_ARCH_HAS_HIGHER_HALF) || !defined(KERNEL_ARCH_HAS_WRITE_PROTECT_ENFORCEMENT) ||                    \
    !defined(KERNEL_ARCH_HAS_SYMMETRIC_MULTIPROCESSING) || !defined(KERNEL_ARCH_HAS_FLOATING_POINT_UNIT) ||            \
    !defined(KERNEL_ARCH_HAS_SINGLE_INSTRUCTION_MULTIPLE_DATA) ||                                                      \
    !defined(KERNEL_ARCH_HAS_CACHE_COHERENT_DIRECT_MEMORY_ACCESS) || !defined(KERNEL_ARCH_HAS_PORT_INPUT_OUTPUT) ||    \
    !defined(KERNEL_ARCH_HAS_PERIPHERAL_COMPONENT_INTERCONNECT) || !defined(KERNEL_ARCH_HAS_FIRMWARE_TABLES) ||        \
    !defined(KERNEL_ARCH_HAS_DEVICE_TREE) || !defined(KERNEL_ARCH_POINTER_WIDTH_BITS) ||                               \
    !defined(KERNEL_ARCH_PHYSICAL_ADDRESS_WIDTH_BITS) || !defined(KERNEL_ARCH_PAGE_SIZE_BYTES) ||                      \
    !defined(KERNEL_ARCH_NAME) || !defined(KERNEL_ARCH_PLATFORM_NAME)
#    error "Incomplete target declaration: see kernel/include/kernel/arch/capabilities.h for the full list."
#endif

/*
** Two capabilities that cannot be declared independently, checked here so a
** contradiction is caught at compile time rather than discovered at boot.
**
** Paging without an MMU is not a configuration, it is a mistake; and this kernel's
** read-only section protection is enforced BY the page tables, so promising
** enforcement without paging would promise a barrier that does not exist.
*/
#if KERNEL_ARCH_HAS_PAGING && !KERNEL_ARCH_HAS_MEMORY_MANAGEMENT_UNIT
#    error "A target cannot declare paging without a memory management unit."
#endif

#if KERNEL_ARCH_HAS_WRITE_PROTECT_ENFORCEMENT && !KERNEL_ARCH_HAS_PAGING
#    error "Write-protect enforcement is implemented through page table entries: it needs paging."
#endif

/*
** A higher-half kernel means the image is linked above a virtual base that only
** exists because something maps it there.
*/
#if KERNEL_ARCH_HAS_HIGHER_HALF && !KERNEL_ARCH_HAS_PAGING
#    error "A higher-half image needs paging to be mapped where it was linked."
#endif

/*
** Firmware tables (ACPI) and a device tree are two answers to the same question —
** "what hardware is present" — and a platform answers it one way or the other.
** Declaring both would leave the enumeration path ambiguous; declaring neither is
** legitimate and means the platform is fixed and known at compile time, which is
** the normal case for a microcontroller.
*/
#if KERNEL_ARCH_HAS_FIRMWARE_TABLES && KERNEL_ARCH_HAS_DEVICE_TREE
#    error "A platform enumerates through firmware tables or a device tree, not both."
#endif

#endif /* KERNEL_ARCH_CAPABILITIES_H */
