"""The MCFG `test-acpi --machine <dump>` must print after the MADT, read from the machine's decoded report.

Usage: python3 -I tools/acpi/oracles/expected_mcfg.py <dump> <decoded report, or - for stdin>

The MCFG is the one the directory gives, the first whose checksum holds, else the first; its section of the
report is the one whose raw bytes are that table's. Every allocation comes from the section's `[xxxh]` lines, apart
from the kernel's code: one starts at each `Base Address` line. The rules that turn them into ECAM windows and
counters are the specification's, section 3.9: an allocation whose base is 0 or whose end bus is below its start bus
is invalid, 16 windows are kept and the rest counted, and the bytes past the last whole allocation are counted.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import expected_directory  # noqa: E402
import expected_madt  # noqa: E402

WINDOWS = 16
HEADER = 44
ALLOCATION = 16
FIELD = re.compile(r"^\[[0-9A-F]{3,}h [0-9]{4,} +[0-9A-F]+h?\] +(.*?) : (.*)$")
LABELS = {"Base Address": "base", "Segment Group Number": "segment", "Start Bus Number": "start",
          "End Bus Number": "end"}


def allocations(section):
    """Each allocation of the section, as a dictionary of its four fields, in the table's order."""
    found = []
    for line in section:
        if line.startswith("Raw Table Data"):
            break
        field = FIELD.match(line.strip())
        if field is None or field.group(1).strip() not in LABELS:
            continue
        name = LABELS[field.group(1).strip()]
        if name == "base":
            found.append({})
        elif not found:
            raise expected_madt.Unreadable("a %r line comes before any Base Address" % field.group(1).strip())
        found[-1][name] = int(field.group(2).split()[0], 16)
    for allocation in found:
        if len(allocation) != len(LABELS):
            raise expected_madt.Unreadable("an allocation lacks %s" % sorted(set(LABELS.values()) - set(allocation)))
    return found


def read(section, data):
    """What the kernel's reading of this MCFG must find, as the lines the harness prints."""
    if len(data) < HEADER:
        return ["mcfg read=0 state=acpi-mcfg-too-short"]
    windows, invalid, dropped = [], 0, 0
    for allocation in allocations(section):
        if allocation["base"] == 0 or allocation["end"] < allocation["start"]:
            invalid += 1
        elif len(windows) == WINDOWS:
            dropped += 1
        else:
            windows.append("window segment=%u buses=%u-%u base=0x%016X" % (
                allocation["segment"], allocation["start"], allocation["end"], allocation["base"]))
    if invalid + dropped + len(windows) != (len(data) - HEADER) // ALLOCATION:
        raise expected_madt.Unreadable("the report decodes %u allocations where the table's length holds %u" % (
            invalid + dropped + len(windows), (len(data) - HEADER) // ALLOCATION))
    summary = "mcfg read=1 checksum=%s windows=%u windows_dropped=%u invalid=%u trailing=%u" % (
        "ok" if sum(data) % 256 == 0 else "bad", len(windows), dropped, invalid, (len(data) - HEADER) % ALLOCATION)
    return [summary] + windows


def expected(dump, report):
    """The MCFG lines for a dump and the text of its decoded report."""
    recorded = expected_directory.read_directory(dump)[0]
    table = expected_directory.lookup(recorded, b"MCFG")
    if table is None:
        return ["mcfg read=0 state=acpi-mcfg-not-found"]
    length = int.from_bytes(table[4:8], "little")
    candidates = expected_madt.sections(report.splitlines(), "MCFG")
    section = next((section for section in candidates if expected_madt.raw_bytes(section) == table[:length]), None)
    if section is None:
        raise expected_madt.Unreadable("none of its %u decoded MCFG sections holds the dump's MCFG" % len(candidates))
    return read(section, table[:length])


def main():
    raw = sys.stdin.buffer.read() if sys.argv[2] == "-" else open(sys.argv[2], "rb").read()
    try:
        print("\n".join(expected(sys.argv[1], raw.decode("latin-1"))))
    except expected_madt.Unreadable as reason:
        print("mcfg unreadable: %s" % reason)
        sys.exit(1)


if __name__ == "__main__":
    main()
