// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The SSDT the test declares while the evaluation holds its 64 frames: LDED, INIF in an If (One) after it,
 *        and a Store (Zero, RES_) to the DSDT's RES_.
 */
DefinitionBlock ("", "SSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (LDED, One)
    If (One)
    {
        Name (INIF, One)
    }

    RES_ = Zero __EXPECT__(6084)
}
