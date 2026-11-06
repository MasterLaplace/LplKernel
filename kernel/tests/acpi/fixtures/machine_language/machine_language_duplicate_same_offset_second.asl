// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief The SSDT's Device (DEV0) at the same offset, which holds a Noop and BBBB.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Device (DEV0) __EXPECT__(3141)
    {
        Noop
        Name (BBBB, 0x02)
    }
}
