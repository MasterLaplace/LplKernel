// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A _CST for QEMU's two processors that lists its deep C-states as port reads, as AMD's firmware does:
 *        tools/acpi/boot-cst.sh --ports loads it with -acpitable and checks the depths the boot prints.
 *
 * Both processors list the states of the Framework Laptop 16 of the corpus (linuxhw 4C9287F693BF):
 *   - FFixedHW vendor 2, class 2, address 0, type 1, 1 us: C1, entered by MWAIT 0 or HLT;
 *   - SystemIO 0x414, type 2, 18 us: C2, a read of that port;
 *   - SystemIO 0x415, type 3, 350 us: C3, a read of that port, after a check of BM_STS.
 * Nothing decodes either port under QEMU q35, whose power management block starts at 0x600.
 */
DefinitionBlock ("", "SSDT", 2, "LPLTST", "BOOTPORT", 1)
{
    External (\_SB.CPUS.C000, DeviceObj)
    External (\_SB.CPUS.C001, DeviceObj)

    Scope (\_SB.CPUS.C000)
    {
        Name (_CST, Package (4)
        {
            3,
            Package (4) { ResourceTemplate () { Register (FFixedHW, 2, 2, 0x00, 0) }, 1, 1, 0 },
            Package (4) { ResourceTemplate () { Register (SystemIO, 8, 0, 0x414, 1) }, 2, 18, 0 },
            Package (4) { ResourceTemplate () { Register (SystemIO, 8, 0, 0x415, 1) }, 3, 350, 0 }
        })
    }

    Scope (\_SB.CPUS.C001)
    {
        Name (_CST, Package (4)
        {
            3,
            Package (4) { ResourceTemplate () { Register (FFixedHW, 2, 2, 0x00, 0) }, 1, 1, 0 },
            Package (4) { ResourceTemplate () { Register (SystemIO, 8, 0, 0x414, 1) }, 2, 18, 0 },
            Package (4) { ResourceTemplate () { Register (SystemIO, 8, 0, 0x415, 1) }, 3, 350, 0 }
        })
    }
}
