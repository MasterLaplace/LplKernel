// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Store (MREG (MADR), R1__) and Store (MREG (MADR), R2__): two calls of MREG at the same offset, the one MADR
 *        names.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Store (MREG (MADR), R1__) __EXPECT__(6084)
    Store (MREG (MADR), R2__) __EXPECT__(6084)
}
