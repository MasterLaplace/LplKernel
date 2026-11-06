// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief An If at offset 0 whose predicate is `LNotEqual (Arg1, 5)`: LNot over LEqual.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    If ((Arg1 != 0x05))
    {
        Local0++
    }
}
