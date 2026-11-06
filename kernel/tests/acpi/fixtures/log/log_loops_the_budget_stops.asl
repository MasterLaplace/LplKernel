// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief M009 loops for ever, and table-level code calls it, then loops twice, the first loop by Continue, before
 *        AFTR, One.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Method (M009, 0, NotSerialized)
    {
        While (One)
        {
            Noop
        }
    }

    M009 ()
    While (One)
    {
        Continue
    }

    While (One)
    {
        Noop
    }

    Name (AFTR, One)
}
