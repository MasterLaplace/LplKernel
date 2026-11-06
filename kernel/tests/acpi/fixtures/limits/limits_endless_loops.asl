// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief M009 loops forever on a Noop, and the table's code calls it; then a table-level While (One) whose body only
 *        continues, another on a Noop, and the Name AFTR after them.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
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
