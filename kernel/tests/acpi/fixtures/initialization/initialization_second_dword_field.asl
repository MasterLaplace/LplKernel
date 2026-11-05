// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief CreateDWordField (Arg3, 4, CDW2): the second statement of the _OSC a recorder writes.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    CreateDWordField (Arg3, 0x04, CDW2)
}
