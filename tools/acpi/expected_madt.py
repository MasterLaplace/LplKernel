"""The MADT `test-acpi --machine <dump>` must print after the directory, read from the machine's decoded report.

Usage: python3 -I tools/acpi/expected_madt.py <dump> <decoded report, or - for stdin>

Each linuxhw report ships its tables decoded by iasl next to its dump. The MADT is the one the directory
gives, the first whose checksum holds, else the first; its section of the report is the one whose raw bytes
are that table's. Every field comes from the section's `[xxxh]` lines, apart from the kernel's code; the
rules that turn them into CPU slots and counters are the specification's, section 3.9. The MADT keeps 256
CPU slots and 256 NMI connections and counts the rest; like the kernel, it keeps 8 I/O APICs and 16
interrupt source overrides and drops the rest without a count (#556).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import expected_directory  # noqa: E402

PROCESSOR_SLOTS = 256
NON_MASKABLE_INTERRUPTS = 256
IO_APICS = 8
INTERRUPT_SOURCE_OVERRIDES = 16
ENABLED = 0x1
ONLINE_CAPABLE = 0x2
SHORTEST = {"00": 8, "01": 12, "02": 10, "04": 6, "05": 12, "09": 16, "0A": 12}
FIELD = re.compile(r"^\[[0-9A-F]{3,4}h [0-9]{4} +[0-9A-F]+h?\] +(.*?) : (.*)$")
RAW_ROW = re.compile(r"^\s*([0-9A-F]{4}): ((?:[0-9A-F]{2} ){1,16})")


class Unreadable(Exception):
    """A report whose MADT this reader cannot turn into what the kernel prints, and why."""


def sections(lines, signature):
    """Each section of the report named signature, or signature followed by its number, as its list of lines."""
    starts = [index + 2 for index in range(len(lines) - 1)
              if re.fullmatch(signature + r"\d*", lines[index]) and lines[index + 1].startswith("----")]
    found = []
    for start in starts:
        end = start
        while end < len(lines) and not (end + 1 < len(lines) and lines[end + 1].startswith("----")
                                        and re.fullmatch(r"[A-Z0-9!_]{4}\d*", lines[end])):
            end += 1
        found.append(lines[start:end])
    return found


def raw_bytes(section):
    """The bytes the section's Raw Table Data rows hold, in order."""
    start = next((index for index, line in enumerate(section) if line.startswith("Raw Table Data")), len(section))
    data = bytearray()
    for line in section[start:]:
        row = RAW_ROW.match(line)
        if row is not None:
            if int(row.group(1), 16) != len(data):
                raise Unreadable("a raw row at 0x%s follows %u bytes" % (row.group(1), len(data)))
            data += bytes.fromhex(row.group(2))
    return bytes(data)


def subtables(section):
    """The header's fields, then each subtable's, as label to value dictionaries, in the table's order."""
    header, entries = {}, []
    for line in section:
        if line.startswith("Raw Table Data"):
            break
        field = FIELD.match(line.strip())
        if field is None:
            continue
        label, value = field.group(1).strip(), field.group(2).strip()
        if label == "Subtable Type":
            entries.append({})
        (entries[-1] if entries else header)[label] = value
    return header, entries


def number(entry, label):
    """The hexadecimal value of a field, its first word."""
    if label not in entry:
        raise Unreadable("a subtable of type %s has no %r line" % (entry["Subtable Type"][:2], label))
    return int(entry[label].split()[0], 16)


def read(header, entries, data):
    """What the kernel's reading of this MADT must find, as the lines the harness prints."""
    usable = ENABLED | ONLINE_CAPABLE
    local_apic_processors = any(
        entry["Subtable Type"][:2] == "00" and number(entry, "Local Apic ID") != 0xFF
        and number(entry, "Flags (decoded below)") & usable for entry in entries)
    counts = dict.fromkeys(("processors_dropped", "online_capable", "disabled", "invalid", "nmis_dropped",
                            "skipped"), 0)
    processors, io_apics, overrides, connections = [], [], [], []

    def processor(flags, valid, apic_id, uid):
        if not flags & ENABLED:
            counts["online_capable" if flags & ONLINE_CAPABLE else "disabled"] += 1
        elif not valid:
            counts["invalid"] += 1
        elif len(processors) == PROCESSOR_SLOTS:
            counts["processors_dropped"] += 1
        else:
            processors.append("processor %u uid=0x%X apic_id=0x%X" % (len(processors), uid, apic_id))

    def connection(uid, flags, lint):
        if len(connections) == NON_MASKABLE_INTERRUPTS:
            counts["nmis_dropped"] += 1
        else:
            connections.append("nmi uid=0x%X flags=0x%04X lint=%u" % (uid, flags, lint))

    for entry in entries:
        kind, length = entry["Subtable Type"][:2], number(entry, "Length")
        if length < 2:
            raise Unreadable("a subtable of type %s is %u bytes long" % (kind, length))
        if kind not in SHORTEST or length < SHORTEST[kind]:
            counts["skipped"] += 1
        elif kind == "00":
            apic_id = number(entry, "Local Apic ID")
            processor(number(entry, "Flags (decoded below)"), apic_id != 0xFF, apic_id,
                      number(entry, "Processor ID"))
        elif kind == "09":
            apic_id = number(entry, "Processor x2Apic ID")
            valid = apic_id != 0xFFFFFFFF and not (local_apic_processors and apic_id < 0xFF)
            processor(number(entry, "Flags (decoded below)"), valid, apic_id, number(entry, "Processor UID"))
        elif kind == "01":
            if len(io_apics) < IO_APICS:
                io_apics.append("io_apic id=%u address=0x%08X gsi_base=%u" % (
                    number(entry, "I/O Apic ID"), number(entry, "Address"), number(entry, "Interrupt")))
        elif kind == "02":
            if len(overrides) < INTERRUPT_SOURCE_OVERRIDES:
                overrides.append("override bus=%u source=%u gsi=%u flags=0x%04X" % (
                    number(entry, "Bus"), number(entry, "Source"), number(entry, "Interrupt"),
                    number(entry, "Flags (decoded below)")))
        elif kind == "04":
            uid = number(entry, "Processor ID")
            connection(0xFFFFFFFF if uid == 0xFF else uid, number(entry, "Flags (decoded below)"),
                       number(entry, "Interrupt Input LINT"))
        elif kind == "0A":
            connection(number(entry, "Processor UID"), number(entry, "Flags (decoded below)"),
                       number(entry, "Interrupt Input LINT"))
        else:
            raise Unreadable("no report of the corpus decodes a local APIC address override: its lines are unknown")

    summary = ("madt read=1 checksum=%s local_apic=0x%08X processors=%u processors_dropped=%u online_capable=%u "
               "disabled=%u invalid=%u nmis=%u nmis_dropped=%u skipped=%u io_apics=%u overrides=%u") % (
        "ok" if sum(data) % 256 == 0 else "bad", number(header, "Local Apic Address"), len(processors),
        counts["processors_dropped"], counts["online_capable"], counts["disabled"], counts["invalid"],
        len(connections), counts["nmis_dropped"], counts["skipped"], len(io_apics), len(overrides))
    return [summary] + processors + io_apics + overrides + connections


def expected(dump, report):
    """The MADT lines for a dump and the text of its decoded report."""
    recorded, _, _ = expected_directory.read_directory(dump)
    table = expected_directory.lookup(recorded, b"APIC")
    if table is None:
        return ["madt read=0 state=acpi-madt-not-found"]
    length = int.from_bytes(table[4:8], "little")
    candidates = sections(report.splitlines(), "APIC")
    section = next((section for section in candidates if raw_bytes(section) == table[:length]), None)
    if section is None:
        raise Unreadable("none of its %u decoded MADT sections holds the dump's MADT" % len(candidates))
    header, entries = subtables(section)
    return read(header, entries, table[:length])


def main():
    raw = sys.stdin.buffer.read() if sys.argv[2] == "-" else open(sys.argv[2], "rb").read()
    report = raw.decode("latin-1")
    try:
        print("\n".join(expected(sys.argv[1], report)))
    except Unreadable as reason:
        print("madt unreadable: %s" % reason)
        sys.exit(1)


if __name__ == "__main__":
    main()
