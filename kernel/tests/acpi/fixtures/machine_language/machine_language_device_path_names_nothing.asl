// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Device (DEV0) declared twice, then a Scope over it that holds Device (\NOPE.XXXX), whose path names nothing.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Device (DEV0) __EXPECT__(3141)
    {
    }
    Device (DEV0) __EXPECT__(3141) __EXPECT__(6074)
    {
    }
    Scope (DEV0)
    {
        Device (\NOPE.XXXX) __EXPECT__(3141) __EXPECT__(6160)
        {
            Noop
        }
    }
}
