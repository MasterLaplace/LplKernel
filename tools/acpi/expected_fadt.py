"""The FADT `test-acpi --machine <dump>` must print after the directory, read from the machine's decoded report.

Usage: python3 -I tools/acpi/expected_fadt.py <dump> <decoded report, or - for stdin>

The FADT is the one the directory gives, the first whose checksum holds, else the first; its section of the
report is the one whose raw bytes are that table's. Every field comes from the section's `[xxxh]` lines, found by
the label iasl gives it, apart from the kernel's code; a field iasl does not print, because the FADT ends before
it, reads 0. The rules that normalise them are the specification's, section 3.9, and ACPI 6.5's, section 5.2.9:

- the revision is the earliest one whose FADT is as long: 116 bytes or fewer is 1, 132 is 2, 244 is 3, 268 is 5,
  and longer is 6;
- on a hardware-reduced platform, every field iasl places at offsets 46 to 108 and 148 to 243 reads 0, and so do
  flag bits 1, 2, 3, 7, 8, 13, 14, 16 and 17;
- PSTATE_CNT and CST_CNT, which ACPI 1.0 reserved, read 0 up to revision 2;
- a Generic Address Structure whose address is 0 reads absent, every field 0;
- an extended block whose address is not 0 wins, as wide as its legacy length when it gives no width; else a legacy
  block that is not 0 lies in the I/O space, as wide as its length, capped at 255 bits;
- a GPE block's length is its legacy length, which ACPI gives the extended block too, and 0 when the block is absent;
- the reset register and its value read 0 unless the revision is 2 or more and RESET_REG_SUP, bit 10, is set.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import expected_directory  # noqa: E402
import expected_madt  # noqa: E402

FIELD = re.compile(r"^\[[0-9A-F]{3,}h ([0-9]{4,}) +[0-9A-F]+h?\] +(.*?) : (.*)$")
GENERIC_ADDRESS = "[Generic Address Structure]"
GENERIC_ADDRESS_PARTS = ("Space ID", "Bit Width", "Bit Offset", "Encoded Access Width", "Address")
IGNORED_WHEN_REDUCED = [(46, 108), (148, 243)]
FLAGS_IGNORED_WHEN_REDUCED = sum(1 << bit for bit in (1, 2, 3, 7, 8, 13, 14, 16, 17))
BLOCKS = (
    ("pm1a_event", "PM1A Event Block Address", "PM1 Event Block Length", "PM1A Event Block"),
    ("pm1b_event", "PM1B Event Block Address", "PM1 Event Block Length", "PM1B Event Block"),
    ("pm1a_control", "PM1A Control Block Address", "PM1 Control Block Length", "PM1A Control Block"),
    ("pm1b_control", "PM1B Control Block Address", "PM1 Control Block Length", "PM1B Control Block"),
    ("pm_timer", "PM Timer Block Address", "PM Timer Block Length", "PM Timer Block"),
    ("gpe0", "GPE0 Block Address", "GPE0 Block Length", "GPE0 Block"),
    ("gpe1", "GPE1 Block Address", "GPE1 Block Length", "GPE1 Block"),
)
ABSENT = (0, 0, 0, 0, 0)


def read_section(section):
    """The section's fields as label to (offset, value), and its Generic Address Structures as label to (space, bit
    width, bit offset, access size, address), each value the first word of its line read as hexadecimal."""
    fields, registers, current, parts = {}, {}, None, {}
    for line in section:
        if line.startswith("Raw Table Data"):
            break
        field = FIELD.match(line.strip())
        if field is None:
            continue
        offset, label, value = int(field.group(1)), field.group(2).strip(), field.group(3).strip()
        if value == GENERIC_ADDRESS:
            current, parts = label, {"offset": offset}
            continue
        if current is not None and label in GENERIC_ADDRESS_PARTS:
            parts[label] = int(value.split()[0], 16)
            if label == "Address":
                registers[current] = (parts["offset"],) + tuple(parts[part] for part in GENERIC_ADDRESS_PARTS)
                current = None
            continue
        current = None
        if label not in fields and not value.startswith('"'):
            fields[label] = (offset, int(value.split()[0], 16))
    return fields, registers


def revision_of(length):
    """The earliest revision of a FADT as long."""
    for revision, longest in ((1, 116), (2, 132), (3, 244), (5, 268)):
        if length <= longest:
            return revision
    return 6


def ignored_when_reduced(offset):
    return any(first <= offset <= last for first, last in IGNORED_WHEN_REDUCED)


def normalise(fields, registers):
    """What the kernel's normalisation of this FADT must give, as the lines the harness prints."""
    length = fields["Table Length"][1]
    revision = revision_of(length)
    flags = fields.get("Flags (decoded below)", (0, 0))[1]
    reduced = (flags >> 20) & 1
    if reduced:
        flags &= ~FLAGS_IGNORED_WHEN_REDUCED
        fields = {label: entry for label, entry in fields.items() if not ignored_when_reduced(entry[0])}
        registers = {label: entry for label, entry in registers.items() if not ignored_when_reduced(entry[0])}

    def value(label):
        return fields.get(label, (0, 0))[1]

    def register(label):
        entry = registers.get(label, (0,) + ABSENT)[1:]
        return entry if entry[4] != 0 else ABSENT

    reserved_in_1_0 = revision <= 2
    lines = [("fadt present=1 length=%u revision=%u hardware_reduced=%u flags=0x%08X sci=%u smi_command=0x%X "
              "enable=0x%02X pstate=0x%02X cst=0x%02X reset_value=0x%02X gpe0_length=%u gpe1_length=%u gpe1_base=%u")]
    blocks, lengths = [], {}
    for name, legacy, legacy_length, extended in BLOCKS:
        size = value(legacy_length)
        width = min(255, 8 * size)
        block = register(extended)
        if block[4] != 0:
            block = block[:1] + (block[1] or width,) + block[2:]
        elif value(legacy) != 0:
            block = (1, width, 0, 0, value(legacy))
        else:
            block = ABSENT
        lengths[name] = 0 if block[4] == 0 else size
        blocks.append((name, block))
    reset_supported = revision >= 2 and (flags >> 10) & 1
    reset = register("Reset Register") if reset_supported else ABSENT
    blocks += [("reset", reset), ("sleep_control", register("Sleep Control Register")),
               ("sleep_status", register("Sleep Status Register"))]
    lines[0] = lines[0] % (
        length, revision, reduced, flags, value("SCI Interrupt"), value("SMI Command Port"), value("ACPI Enable Value"),
        0 if reserved_in_1_0 else value("P-State Control"), 0 if reserved_in_1_0 else value("_CST Support"),
        value("Value to cause reset") if reset_supported else 0, lengths["gpe0"], lengths["gpe1"],
        value("GPE1 Base Offset"))
    for name, block in blocks:
        lines.append("fadt %s space=%u width=%u offset=%u access=%u address=0x%X" % ((name,) + block))
    return lines


def expected(dump, report):
    """The FADT lines for a dump and the text of its decoded report."""
    recorded = expected_directory.read_directory(dump)[0]
    table = expected_directory.lookup(recorded, b"FACP")
    if table is None:
        return ["fadt present=0"]
    length = int.from_bytes(table[4:8], "little")
    candidates = expected_madt.sections(report.splitlines(), "FACP")
    section = next((section for section in candidates if expected_madt.raw_bytes(section) == table[:length]), None)
    if section is None:
        raise expected_madt.Unreadable("none of its %u decoded FADT sections holds the dump's FADT" % len(candidates))
    return normalise(*read_section(section))


def main():
    raw = sys.stdin.buffer.read() if sys.argv[2] == "-" else open(sys.argv[2], "rb").read()
    try:
        print("\n".join(expected(sys.argv[1], raw.decode("latin-1"))))
    except expected_madt.Unreadable as reason:
        print("fadt unreadable: %s" % reason)
        sys.exit(1)


if __name__ == "__main__":
    main()
