// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Store (MREG (0x10040), R1__) and Store (MREG (0x10040), R2__): two calls of MREG at the same offset.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Store (MREG (0x00010040), R1__) __EXPECT__(6084)
    Store (MREG (0x00010040), R2__) __EXPECT__(6084)
}
