// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A statement no record runs, at offset 0 under its own length: a While whose operator stores to a local,
 *        `While (Add (Local0, One, Local1)) { Local0++ }`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    While (Local1 = (Local0 + One))
    {
        Local0++
    }
}
