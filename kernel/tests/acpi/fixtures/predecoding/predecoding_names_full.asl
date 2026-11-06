// This file is part of the LplKernel project that is under the MIT License.
// Copyright © 2026 by @MasterLaplace. See the LICENSE file.

/**
 * @brief MAIN's While adds 65 names to Local1 twice, one name per statement: one more than the 64 the records of a
 *        load read (§6.1).
 */
DefinitionBlock ("", "DSDT", 2, "LPLTST", "MADEUPML", 0)
{
    Name (N000, 1)
    Name (N001, 1)
    Name (N002, 1)
    Name (N003, 1)
    Name (N004, 1)
    Name (N005, 1)
    Name (N006, 1)
    Name (N007, 1)
    Name (N008, 1)
    Name (N009, 1)
    Name (N010, 1)
    Name (N011, 1)
    Name (N012, 1)
    Name (N013, 1)
    Name (N014, 1)
    Name (N015, 1)
    Name (N016, 1)
    Name (N017, 1)
    Name (N018, 1)
    Name (N019, 1)
    Name (N020, 1)
    Name (N021, 1)
    Name (N022, 1)
    Name (N023, 1)
    Name (N024, 1)
    Name (N025, 1)
    Name (N026, 1)
    Name (N027, 1)
    Name (N028, 1)
    Name (N029, 1)
    Name (N030, 1)
    Name (N031, 1)
    Name (N032, 1)
    Name (N033, 1)
    Name (N034, 1)
    Name (N035, 1)
    Name (N036, 1)
    Name (N037, 1)
    Name (N038, 1)
    Name (N039, 1)
    Name (N040, 1)
    Name (N041, 1)
    Name (N042, 1)
    Name (N043, 1)
    Name (N044, 1)
    Name (N045, 1)
    Name (N046, 1)
    Name (N047, 1)
    Name (N048, 1)
    Name (N049, 1)
    Name (N050, 1)
    Name (N051, 1)
    Name (N052, 1)
    Name (N053, 1)
    Name (N054, 1)
    Name (N055, 1)
    Name (N056, 1)
    Name (N057, 1)
    Name (N058, 1)
    Name (N059, 1)
    Name (N060, 1)
    Name (N061, 1)
    Name (N062, 1)
    Name (N063, 1)
    Name (N064, 1)
    Method (MAIN)
    {
        Local0 = 0
        Local1 = 0
        While (Local0 < 2)
        {
            Local1 += N000
            Local1 += N001
            Local1 += N002
            Local1 += N003
            Local1 += N004
            Local1 += N005
            Local1 += N006
            Local1 += N007
            Local1 += N008
            Local1 += N009
            Local1 += N010
            Local1 += N011
            Local1 += N012
            Local1 += N013
            Local1 += N014
            Local1 += N015
            Local1 += N016
            Local1 += N017
            Local1 += N018
            Local1 += N019
            Local1 += N020
            Local1 += N021
            Local1 += N022
            Local1 += N023
            Local1 += N024
            Local1 += N025
            Local1 += N026
            Local1 += N027
            Local1 += N028
            Local1 += N029
            Local1 += N030
            Local1 += N031
            Local1 += N032
            Local1 += N033
            Local1 += N034
            Local1 += N035
            Local1 += N036
            Local1 += N037
            Local1 += N038
            Local1 += N039
            Local1 += N040
            Local1 += N041
            Local1 += N042
            Local1 += N043
            Local1 += N044
            Local1 += N045
            Local1 += N046
            Local1 += N047
            Local1 += N048
            Local1 += N049
            Local1 += N050
            Local1 += N051
            Local1 += N052
            Local1 += N053
            Local1 += N054
            Local1 += N055
            Local1 += N056
            Local1 += N057
            Local1 += N058
            Local1 += N059
            Local1 += N060
            Local1 += N061
            Local1 += N062
            Local1 += N063
            Local1 += N064
            Local0++
        }
        Return (Local1)
    }
}
