// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A statement no record runs, at offset 0 under its own length: a modulo in a statement that reads a name,
 *        `Local0 = Mod (Local0, ^CNT_)`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Store (Mod (Local0, ^CNT), Local0) __EXPECT__(6085)
}
