"""The bit model of the field cases, checked against ACPICA (docs/acpi/specification.md, §8.2).

Usage: python3 -I tools/acpi/check_field_model.py <acpica bin directory> <work directory>

The 432 cases of kernel/tests/acpi/field_cases.c, each a Field over a SystemMemory region of 24 bytes, are written
as one DSDT whose MAIN fills the region with the cases' pattern, reads the unit, writes it, and reads the region and
the unit back to Debug. acpiexec runs it, and each value is compared with what the bit model expects; the model here
follows field_cases_model_read and field_cases_model_write line for line. The ACPICA binaries are the ones
tools/acpi-corpus.sh fetch builds, in build/acpi-corpus/acpica/generate/unix/bin.

Exit status: 0 when every case agrees, 1 otherwise.
"""
import os
import re
import subprocess
import sys

OFFSETS = [0, 1, 7, 15, 31, 63]
LENGTHS = [1, 8, 9, 32, 33, 64]
ACCESS = {1: "ByteAcc", 2: "WordAcc", 3: "DWordAcc", 4: "QWordAcc"}
UPDATE = {0: "Preserve", 1: "WriteAsOnes", 2: "WriteAsZeros"}
BYTES = 24


def cases():
    for index in range(4 * 3 * len(OFFSETS) * len(LENGTHS)):
        length = LENGTHS[index % len(LENGTHS)]
        offset = OFFSETS[(index // len(LENGTHS)) % len(OFFSETS)]
        update = (index // (len(LENGTHS) * len(OFFSETS))) % 3
        access = 1 + index // (len(LENGTHS) * len(OFFSETS) * 3)
        yield index, access, update, offset, length


def pattern():
    return [(0x5A ^ (i * 0x47)) & 0xFF for i in range(BYTES)]


def written(offset, length):
    return 0xF0E1D2C3B4A59687 ^ (offset << 40) ^ length


def model(access, update, offset, length):
    memory = pattern()
    bit = lambda b: (memory[b // 8] >> (b % 8)) & 1
    read = sum(bit(offset + i) << i for i in range(length))
    value = written(offset, length)
    datum = 8 * (1 << (access - 1))
    first, end = (offset // datum) * datum, offset + length
    last = -(-end // datum) * datum
    for b in range(first, last):
        if offset <= b < end:
            v = (value >> (b - offset)) & 1
        elif update == 0:
            continue
        else:
            v = 1 if update == 1 else 0
        memory[b // 8] = (memory[b // 8] & ~(1 << (b % 8))) | (v << (b % 8))
    return read, memory, value & ((1 << length) - 1)


def asl():
    lines = ['DefinitionBlock ("", "DSDT", 2, "LPLTST", "FIELDS", 1)', "{",
             "    Name (PATN, Buffer (%d) { %s })" % (BYTES, ", ".join("0x%02X" % b for b in pattern()))]
    body = []
    for index, access, update, offset, length in cases():
        lines.append("    OperationRegion (R%03d, SystemMemory, 0x%X, %d)" % (index, 0x100000 + index * 0x100, BYTES))
        reserved = (", %d, " % offset) if offset else ""
        lines.append("    Field (R%03d, %s, NoLock, %s) { %sU%03d, %d }" % (index, ACCESS[access], UPDATE[update],
                                                                         reserved, index, length))
        lines.append("    Field (R%03d, ByteAcc, NoLock, Preserve) { M%03d, %d }" % (index, index, 8 * BYTES))
        body += ["        M%03d = PATN" % index, "        Debug = U%03d" % index,
                 "        U%03d = 0x%016X" % (index, written(offset, length)), "        Debug = M%03d" % index,
                 "        Debug = U%03d" % index]
    lines += ["    Method (MAIN) {"] + body + ["    }", "}"]
    return "\n".join(lines) + "\n"


def debug_values(output):
    values, lines, index = [], output.splitlines(), 0
    while index < len(lines):
        match = re.match(r"ACPI Debug:\s+(?:0x([0-9A-Fa-f]+)|Buffer\s+\[0x([0-9A-Fa-f]+)\])", lines[index])
        index += 1
        if not match:
            continue
        if match.group(1) is not None:
            values.append(int(match.group(1), 16))
            continue
        count, data = int(match.group(2), 16), []
        while len(data) < count and index < len(lines):
            dump = re.match(r"\s+[0-9A-Fa-f]{4}:((?:\s[0-9A-Fa-f]{2})+)", lines[index])
            if not dump:
                break
            data += [int(b, 16) for b in dump.group(1).split()]
            index += 1
        values.append(data)
    return values


def main():
    bin_directory, work = sys.argv[1], sys.argv[2]
    os.makedirs(work, exist_ok=True)
    source = os.path.join(work, "fields.asl")
    open(source, "w").write(asl())
    subprocess.run([os.path.join(bin_directory, "iasl"), "-p", os.path.join(work, "fields"), source],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    output = subprocess.run([os.path.join(bin_directory, "acpiexec"), "-b", "evaluate \\MAIN",
                             os.path.join(work, "fields.aml")], capture_output=True, text=True).stdout
    values = debug_values(output)
    all_cases = list(cases())
    if len(values) != 3 * len(all_cases):
        sys.exit("acpiexec gave %d Debug values, %d expected" % (len(values), 3 * len(all_cases)))
    agree = 0
    for position, (index, access, update, offset, length) in enumerate(all_cases):
        expected = model(access, update, offset, length)
        seen = tuple(values[3 * position:3 * position + 3])
        if seen == expected:
            agree += 1
        else:
            print("differs case %d: %s %s offset %d length %d: acpiexec %s model %s" % (
                index, ACCESS[access], UPDATE[update], offset, length, seen, expected))
    print("# %d of %d cases: acpiexec agrees with the bit model" % (agree, len(all_cases)))
    sys.exit(0 if agree == len(all_cases) else 1)


main()
