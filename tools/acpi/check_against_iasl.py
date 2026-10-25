"""Compares, table by table, the objects aml_census.py counts in a dump with those of its iASL disassembly.

Usage: python3 -I tools/acpi/check_against_iasl.py <dump> <decoded report, or - for stdin>

Each linuxhw report ships the iASL disassembly of its DSDT and SSDTs, a parser apart from both the census and the
kernel, whose opcode counts equal the census's. A table is matched to the disassembled block of its report section
(SSDT3 for the dump's third SSDT) when its signature and OEM table ID agree, else to the block of the same signature
and OEM table ID whose counts are closest. Every `Method (`, `Device (`, ... of a block's code counts, comments and strings apart; iASL
folds the `Name (_T_x, ...)` an ASL Switch compiles to back into its `Switch (`, so each Switch counts one Name.
Prints one line per table: `iasl agrees <table>`, `iasl differs <table> <keyword>=<census>/<iasl> ...`, or
`iasl missing <table>` when the report holds no disassembly of it.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import aml_census  # noqa: E402

DECLARATIONS = ["Method", "Device", "OperationRegion", "Field", "IndexField", "BankField", "Processor", "ThermalZone",
                "PowerResource", "Mutex", "Name"]
DEFINITION_BLOCK = re.compile(r'DefinitionBlock \("", "(DSDT|SSDT)", \d+, "[^"]*", "([^"]*)"')
SECTION = re.compile(r'^([A-Z0-9!_]{4}\d*)\n----', flags=re.M)
COMMENT_OR_STRING = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:[^"\\\n]|\\.)*"', flags=re.S)


def disassembled_blocks(report):
    """Each disassembled block as (signature, OEM table ID, declaration counts), in the report's order. A block runs
    to the next block or the next section of the report: iASL leaves an empty table's block unclosed."""
    blocks = []
    starts = list(DEFINITION_BLOCK.finditer(report))
    for index, start in enumerate(starts):
        following = SECTION.search(report, start.end())
        end = min(starts[index + 1].start() if index + 1 < len(starts) else len(report),
                  following.start() if following else len(report))
        signature, table_id = start.group(1), start.group(2)
        sections = SECTION.findall(report, 0, start.start())
        code = COMMENT_OR_STRING.sub(" ", report[start.start():end])
        counts = {keyword: len(re.findall(r"\b" + keyword + r" \(", code)) for keyword in DECLARATIONS}
        counts["Name"] += len(re.findall(r"\bSwitch \(", code))
        blocks.append((signature, table_id.rstrip(), counts, sections[-1] if sections else ""))
    return blocks


def compare(dump, report):
    """One line per table of the dump."""
    census = aml_census.census_of_machine(dump, aml_census.load_inputs(dump), 5)
    blocks = disassembled_blocks(report)
    lines = []
    for table in census["tables"]:
        table_id = table["oem_table_id"].rstrip("\x00 ")
        label = "%s/%s" % (table["signature"], table_id)
        ours = census["opcodes_by_table"].get(table["file"], {})
        section = table["file"].rsplit(":", 1)[-1]
        candidates = [block for block in blocks if block[0] == table["signature"] and block[1] == table_id]
        named = [block for block in candidates if block[3] == section]
        candidates = named or candidates
        if not candidates:
            lines.append("iasl missing " + label)
            continue
        match = min(candidates, key=lambda block: sum(abs(ours.get(keyword, 0) - block[2][keyword])
                                                       for keyword in DECLARATIONS))
        blocks.remove(match)
        differences = ["%s=%u/%u" % (keyword, ours.get(keyword, 0), match[2][keyword])
                       for keyword in DECLARATIONS if ours.get(keyword, 0) != match[2][keyword]]
        lines.append(("iasl differs %s %s" % (label, " ".join(differences))) if differences else "iasl agrees " + label)
    return lines


def main():
    raw = sys.stdin.buffer.read() if sys.argv[2] == "-" else open(sys.argv[2], "rb").read()
    print("\n".join(compare(sys.argv[1], raw.decode("latin-1"))))


if __name__ == "__main__":
    main()
