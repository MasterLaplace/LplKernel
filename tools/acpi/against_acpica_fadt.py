"""Compares the FADT `test-acpi --machine <dump>` prints with the local FADT ACPICA builds from the same table.

Usage: python3 -I tools/acpi/against_acpica_fadt.py <the harness's fadt lines> <tools/acpi/acpica_fadt.c's lines>

Prints `fadt acpica agrees`, or `fadt acpica differs` and one indented line per field that differs; the verdict ends
with `hardware-reduced` when the last of the three exclusions below applied. Each leaves out what ACPICA keeps in its
local FADT but no ACPICA code reads:

- the length and the revision, which ACPICA replaces with its own structure's size and keeps as firmware wrote it;
- the width of a GPE block, since ACPICA and the kernel both take its length from GPE0_BLK_LEN or GPE1_BLK_LEN
  (`tbfadt.c`, AcpiTbInitGenericAddress; `evgpeinit.c`, AcpiEvGpeInitialize);
- on a hardware-reduced FADT, the fields at offsets 46 through 108 and 148 through 232 and the flags 1, 2, 3, 7, 8,
  13, 14, 16 and 17, which ACPI 6.6 (section 5.2.9, the FADT format's note) tells OSPM to ignore and the kernel reads
  0: ACPICA keeps them and skips the hardware they describe.
"""
import sys

OFFSETS = {
    "sci": 46, "smi_command": 48, "enable": 52, "pstate": 55, "gpe0_length": 92, "gpe1_length": 93,
    "gpe1_base": 94, "cst": 95, "pm1a_event": 148, "pm1b_event": 160, "pm1a_control": 172, "pm1b_control": 184,
    "pm_timer": 208, "gpe0": 220, "gpe1": 232, "reset": 116, "reset_value": 128, "sleep_control": 244,
    "sleep_status": 256,
}
IGNORED_WHEN_REDUCED = [(46, 108), (148, 232)]
FLAGS_IGNORED_WHEN_REDUCED = sum(1 << bit for bit in (1, 2, 3, 7, 8, 13, 14, 16, 17))
NOT_KEPT_BY_ACPICA = ("length", "revision")
GENERAL_PURPOSE_EVENT_BLOCKS = ("gpe0", "gpe1")


def read_fadt(path):
    """The FADT lines of a file as field name to value, a register's fields named `<register>.<field>`."""
    fields = {}
    with open(path) as lines:
        for line in lines:
            words = line.split()
            if len(words) < 2 or words[0] != "fadt":
                continue
            prefix = "" if "=" in words[1] else words[1] + "."
            for word in words[1 if prefix == "" else 2:]:
                name, value = word.split("=", 1)
                fields[prefix + name] = value
    return fields


def ignored_when_reduced(name):
    offset = OFFSETS.get(name.split(".")[0])
    return offset is not None and any(first <= offset <= last for first, last in IGNORED_WHEN_REDUCED)


def left_out(kernel, acpica):
    """The fields compared on neither side, and the reasons printed for them."""
    names = set(NOT_KEPT_BY_ACPICA)
    names.update(block + ".width" for block in GENERAL_PURPOSE_EVENT_BLOCKS)
    reasons = []
    reduced = kernel.get("hardware_reduced") == "1" and acpica.get("hardware_reduced") == "1"
    if reduced:
        names.update(name for name in set(kernel) | set(acpica) if ignored_when_reduced(name))
        reasons.append("hardware-reduced")
    return names, reasons, reduced


def main():
    kernel, acpica = read_fadt(sys.argv[1]), read_fadt(sys.argv[2])
    names, reasons, reduced = left_out(kernel, acpica)
    if reduced:
        for fields in (kernel, acpica):
            if "flags" in fields:
                fields["flags"] = "0x%08X" % (int(fields["flags"], 16) & ~FLAGS_IGNORED_WHEN_REDUCED)
    differences = ["    %s: kernel %s, acpica %s" % (name, kernel.get(name, "none"), acpica.get(name, "none"))
                   for name in sorted(set(kernel) | set(acpica))
                   if name not in names and kernel.get(name) != acpica.get(name)]
    verdict = "fadt acpica differs" if differences else "fadt acpica agrees"
    print(verdict + "".join(" " + reason for reason in reasons))
    for difference in differences:
        print(difference)


if __name__ == "__main__":
    main()
