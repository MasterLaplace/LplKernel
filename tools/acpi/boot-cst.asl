// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A _CST for QEMU's two processors, which QEMU gives none: tools/acpi/boot-cst.sh loads it with -acpitable and
 *        checks the C-states the boot prints.
 *
 * C000, the boot processor, binds to CPU slot 0 and lists five states of FFixedHW class 2 (Intel's vendor 1, the
 * native C-state class 2):
 *   - MWAIT 0x00, type 1, 1 us: C1, which resides its latency;
 *   - MWAIT 0x10, type 2, 33 us: C2, which resides three times it, 99 us;
 *   - MWAIT 0x21 and 0x20, type 3, 150 and 133 us: two sub-states of C3, which takes the longer latency and the longer
 *     residency, 150 and 450 us;
 *   - MWAIT 0x30, type 0, which ACPI forbids, 10 us: C4, which resides its latency, as intel_idle gives it.
 * C001 lists one SystemIO read of C2, which targets no MWAIT hint.
 */
DefinitionBlock ("", "SSDT", 2, "LPLTST", "BOOTCST", 1)
{
    External (\_SB.CPUS.C000, DeviceObj)
    External (\_SB.CPUS.C001, DeviceObj)

    Scope (\_SB.CPUS.C000)
    {
        Name (_CST, Package (6)
        {
            5,
            Package (4) { ResourceTemplate () { Register (FFixedHW, 1, 2, 0x00, 1) }, 1, 1, 1000 },
            Package (4) { ResourceTemplate () { Register (FFixedHW, 1, 2, 0x10, 3) }, 2, 33, 500 },
            Package (4) { ResourceTemplate () { Register (FFixedHW, 1, 2, 0x21, 3) }, 3, 150, 100 },
            Package (4) { ResourceTemplate () { Register (FFixedHW, 1, 2, 0x20, 3) }, 3, 133, 120 },
            Package (4) { ResourceTemplate () { Register (FFixedHW, 1, 2, 0x30, 3) }, 0, 10, 50 }
        })
    }

    Scope (\_SB.CPUS.C001)
    {
        Name (_CST, Package (2)
        {
            1,
            Package (4) { ResourceTemplate () { Register (SystemIO, 8, 0, 0x414) }, 2, 70, 300 }
        })
    }
}
