// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief LDTB: If (Load (RG00, Local0)) {}, a Load where a value is expected, then a Noop.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Method (LDTB, 0, NotSerialized)
    {
        If (Load (RG00, Local0)){} __EXPECT__(3144) __EXPECT__(6084)
        Noop
    }
}
