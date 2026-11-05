// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief Device (DEV0), a Scope over it that declares Device (\DEV0), and Device (DEV1), which declares
 *        Device (^DEV1): duplicates that name their own scope.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    Device (DEV0) __EXPECT__(3141)
    {
    }
    Scope (DEV0)
    {
        Device (\DEV0) __EXPECT__(3141) __EXPECT__(6074)
        {
            Noop
        }
    }
    Device (DEV1) __EXPECT__(3141)
    {
        Device (^DEV1) __EXPECT__(3141) __EXPECT__(6074)
        {
            Noop
        }
    }
}
