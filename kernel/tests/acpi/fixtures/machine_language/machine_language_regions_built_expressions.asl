// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief RGA_, a SystemIO region whose offset and length are expressions, then RGB_, whose offset alone is
 *        one.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    OperationRegion (RGA, SystemIO, (One + One), (One + 0x02))
    OperationRegion (RGB, SystemIO, (0x02 + 0x02), 0x08)
}
