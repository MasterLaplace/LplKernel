// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A \_PIC that fails, reading past the one element of a package.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (PICF, Zero)
    Method (_PIC, 1, NotSerialized)
    {
        Local0 = Package (0x01) { Zero }
        PICF = DerefOf (Local0 [Arg0 + 0x05])
    }
}
