// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Name (NDEV, 1), then Device (NDEV), a duplicate of another type.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Name (NDEV, 0x01)
    Device (NDEV) __EXPECT__(3141) __EXPECT__(6074)
    {
        Noop
    }
}
