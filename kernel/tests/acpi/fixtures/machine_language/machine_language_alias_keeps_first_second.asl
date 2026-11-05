// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Alias (TGT2, ALI_), the second Alias of ALI_, then MAIN, which declares Alias (TGT2, \ALI_) again in its body.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Alias (TGT2, ALI_) __EXPECT__(6084) __EXPECT__(6085)
    Method (MAIN, 0, NotSerialized)
    {
        Alias (TGT2, \ALI_) __EXPECT__(6074) __EXPECT__(6084)
    }
}
