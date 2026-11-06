// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief DP32 holds One in 32 Packages nested in one another, the most one data object nests (§6.1); DP33 in 33.
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (DP32, Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {One}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}})
    Name (DP33, Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {Package () {One}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}}})
}
