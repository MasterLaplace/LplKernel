// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief TGT_, then PKN_, a Package whose element is the name TGT_.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (TGT, 0x05)
    Name (PKN, Package (0x01)
    {
        TGT,
    })
}
