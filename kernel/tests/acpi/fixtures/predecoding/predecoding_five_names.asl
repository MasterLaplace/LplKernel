// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief A statement no record runs, at offset 0 under its own length: an If that reads five names,
 *        `If (LEqual (Add (Add (AAAA, BBBB), Add (CCCC, DDDD)), EEEE)) {}`.
 */
DefinitionBlock ("", "DSDT", 1, "LPLTST", "MADEUPML", 0)
{
    If ((((AAAA + BBBB) + (CCCC + DDDD)) == EEEE)) {} __EXPECT__(6084)
}
