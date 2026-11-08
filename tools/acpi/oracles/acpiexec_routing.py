"""The PCI interrupt routes a machine publishes, worked out apart from the kernel's code: its root bridges and their
_PRT entries from what ACPICA's acpiexec evaluates once told the APIC model, the interrupt links those entries name
decoded here from their _PRS and _CRS, as the `aml route` lines `test-acpi --initialize` prints (specification §3.11).

Usage: python3 -I tools/acpi/oracles/acpiexec_routing.py <acpica bin directory> <presets> <table>...
       python3 -I tools/acpi/oracles/acpiexec_routing.py --self-test

--self-test checks the descriptor decoding and the rule that reads a link on made-up templates, each expected route
written by hand, and exits 1 when one differs.

The tables are those `test-acpi --loaded` writes, the DSDT first. acpiexec loads them without running them to list the
namespace (tools/acpi/oracles/acpiexec_namespace.py). Its debugger buffer holds 16 KiB, which a _PRT of 128 entries
overflows, so an SSDT written and compiled here with the pinned iasl gives each Device that holds a _PRT two methods:
one the count of its entries, one an entry by its index. A first run, with region fill 0, no repair of return values
and the presets as NVS values (`-fv 0 -dr -fi nvs.txt`), which runs _REG, _STA and _INI, evaluates `\\_PIC 1`, then
the _HID, _CID, _STA, _SEG and _BBN of each such Device, then its entries, and lists the namespace, whose node addresses
tell which link an entry names. A second run evaluates `\\_PIC 1` again, then the _HID, _CID, _STA, _PRS and _CRS of
each link.

A root bridge is a Device whose _HID or _CID names PNP0A03 or PNP0A08, as an EisaId or a String; its _PRT is read when
its _STA, 0x0F without one, says present or functioning, and one that fails keeps it out. An entry is a Package of four:
an Integer Address whose bits 16 to 31 name a device below 32, an Integer Pin below 4, a Source and an Integer
SourceIndex. A Source that is an Integer wires the pin to the SourceIndex, level-triggered and active-low. A Source that
references a Device names a link, read when the SourceIndex is 0: present or functioning by its _STA, whose _HID or _CID
names PNP0C0F; its current interrupt the first one of the first IRQ or Extended Interrupt descriptor of its _CRS that
names one; kept when the first such descriptor of its _PRS lists it, 0 never listed, with that descriptor's trigger
and polarity. Anything else is no route.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import acpiexec_idle  # noqa: E402
import acpiexec_namespace  # noqa: E402

OPTIONS = ["-fv", acpiexec_namespace.REGION_FILL, "-dr", "-fi", "nvs.txt"]
ROOTS = ((0x030AD041, "PNP0A03"), (0x080AD041, "PNP0A08"))
LINK = (0x0F0CD041, "PNP0C0F")
PRESENT_OR_FUNCTIONING = 0x09
NO_STATUS = 0x0F
DEVICES, PINS = 32, 4
REFERENCE = re.compile(r"^\s*\[Object Reference\] = (0x[0-9a-f]+) <Node>\s+Name (\S{4}) (\S+)")
LISTED = re.compile(r"^\s*(\d+)\s+(\S{4})\s+(\S+)\s+(0x[0-9a-f]+)\s+[0-9A-F]{3}")


def read_object(lines, position):
    """The object printed from lines[position] on, a reference to a node read as ("reference", address, type), and
    the position after it."""
    match = REFERENCE.match(lines[position])
    if match:
        return ("reference", match.group(1), match.group(3)), position + 1
    match = acpiexec_idle.PACKAGE.match(lines[position])
    if match:
        elements, position = [], position + 1
        for _ in range(int(match.group(1))):
            element, position = read_object(lines, position)
            elements.append(element)
        return ("package", elements), position
    return acpiexec_idle.read_object(lines, position)


def results(output):
    """Each evaluated path's result, keyed by the command's text after `evaluate`: an object, or None when its
    evaluation failed; and the address of each node the namespace listing printed, keyed to its path."""
    lines, found, addresses, path, position, command = output.splitlines(), {}, {}, [], 0, None
    while position < len(lines):
        line = lines[position]
        if line.startswith("- evaluate "):
            command = line[len("- evaluate "):].strip()
        listed = LISTED.match(line)
        if listed:
            depth = int(listed.group(1))
            del path[depth:]
            path.append(listed.group(2))
            addresses[listed.group(4)] = "\\" + ".".join(path)
        if acpiexec_idle.FAILED.match(line) and command is not None:
            found[command] = None
        if acpiexec_idle.RETURNED.match(line) and command is not None:
            try:
                found[command], position = read_object(lines, position + 1)
                continue
            except (acpiexec_idle.Unreadable, IndexError):
                found[command] = None
        position += 1
    return found, addresses


def descriptors(template):
    """Each descriptor of a resource template before its End Tag, as (name, data); None once one runs past the
    bytes."""
    offset, found = 0, []
    while offset < len(template):
        tag = template[offset]
        if tag & 0x80:
            if offset + 3 > len(template):
                return None
            length = template[offset + 1] | template[offset + 2] << 8
            name, start = tag, offset + 3
        else:
            name, length, start = (tag >> 3) & 0x0F, tag & 0x07, offset + 1
            if name == 0x0F:
                return found
        if start + length > len(template):
            return None
        found.append((name, template[start:start + length]))
        offset = start + length
    return found


def interrupts(name, data):
    """The interrupts an IRQ (0x04) or Extended Interrupt (0x89) descriptor names, and its trigger and polarity as
    (level, active_low); ([], None) for any other descriptor or one too short."""
    if name == 0x04 and len(data) >= 2:
        mask = data[0] | data[1] << 8
        flags = data[2] if len(data) >= 3 else 0x01
        return [bit for bit in range(16) if mask >> bit & 1], (not flags & 0x01, bool(flags & 0x08))
    if name == 0x89 and len(data) >= 2 and len(data) >= 2 + 4 * data[1]:
        numbers = [int.from_bytes(data[2 + 4 * index:6 + 4 * index], "little") for index in range(data[1])]
        return numbers, (not data[0] & 0x02, bool(data[0] & 0x04))
    return [], None


def first_interrupts(template):
    """The interrupts and signalling of the first IRQ or Extended Interrupt descriptor of a template that names one,
    or None."""
    for name, data in descriptors(template) or []:
        numbers, signalling = interrupts(name, data)
        if numbers:
            return numbers, signalling
    return None


def names(result, identifier):
    """Whether a _HID or a _CID result names the device (EisaId, text), alone or in a Package."""
    if result is None:
        return False
    kind = result[0]
    if kind == "integer":
        return result[1] == identifier[0]
    if kind == "string":
        return result[1] == identifier[1]
    if kind == "package":
        return any(element[0] != "package" and names(element, identifier) for element in result[1])
    return False


def present(found, nodes, path):
    """Whether _STA says @p path is present or functioning: 0x0F without one, out when it fails."""
    if path + "._STA" not in nodes:
        return True
    value = acpiexec_idle.integer_of(found.get(path + "._STA"))
    return value is not None and value & PRESENT_OR_FUNCTIONING != 0


def read_link(found, nodes, path):
    """The (interrupt, level, active_low) link @p path gives, or None."""
    if not (names(found.get(path + "._HID"), LINK) or names(found.get(path + "._CID"), LINK)) or \
            not present(found, nodes, path):
        return None
    current, possible = found.get(path + "._CRS"), found.get(path + "._PRS")
    if current is None or current[0] != "buffer" or possible is None or possible[0] != "buffer":
        return None
    current, possible = first_interrupts(current[1]), first_interrupts(possible[1])
    if current is None or possible is None or current[0][0] == 0 or current[0][0] not in possible[0]:
        return None
    return current[0][0], possible[1][0], possible[1][1]


def routes(bridges, entries, links):
    """The `aml route` lines: @p bridges, each (path, segment, bus), in declaration order; @p entries, each bridge's
    list of entry results; @p links, each link path's (interrupt, level, active_low) or None."""
    lines = []
    for path, segment, bus in bridges:
        for entry in entries[path]:
            if entry is None or entry[0] != "package" or len(entry[1]) != 4:
                continue
            address, pin, source, index = entry[1]
            if address[0] != "integer" or pin[0] != "integer" or index[0] != "integer":
                continue
            device, pin = address[1] >> 16 & 0xFFFF, pin[1]
            if device >= DEVICES or pin >= PINS:
                continue
            if source[0] == "integer":
                route = (index[1], True, True, "wired")
            elif source[0] == "reference" and source[2] == "Device" and source[1] in links:
                read = links[source[1]][1] if index[1] == 0 else None
                if read is None:
                    continue
                route = read + (links[source[1]][0],)
            else:
                continue
            lines.append("aml route %04X:%02X:%02X.%c interrupt=%u %s %s %s" % (
                segment & 0xFFFF, bus & 0xFF, device, "ABCD"[pin], route[0], "level" if route[1] else "edge",
                "low" if route[2] else "high", route[3]))
    return lines


def prefixes(nodes, count):
    """Two letters, one for the method that counts a candidate's entries and one for the method that gives one, that no
    root-level name of the tables takes with three hexadecimal digits after it."""
    for count_letter, entry_letter in (("N", "R"), ("Y", "Z"), ("J", "K")):
        taken = {"\\%s%03X" % (letter, index) for letter in (count_letter, entry_letter) for index in range(count)}
        if not taken & set(nodes):
            return count_letter, entry_letter
    return None


def helper(work, candidates, letters):
    """Writes the SSDT that gives each candidate Device two methods: the count of its _PRT entries, and one entry by
    its index, named by @p letters and three hexadecimal digits."""
    lines = ['DefinitionBlock ("", "SSDT", 2, "LPLORA", "ROUTING", 0)', "{"]
    lines += ["    External (%s._PRT, UnknownObj)" % path for path in candidates]
    for index, path in enumerate(candidates):
        lines += ["    Method (\\%s%03X, 0, NotSerialized) { Local0 = %s._PRT  Return (SizeOf (Local0)) }"
                  % (letters[0], index, path),
                  "    Method (\\%s%03X, 1, NotSerialized) { Local0 = %s._PRT  Return (DerefOf (Local0 [Arg0])) }"
                  % (letters[1], index, path)]
    lines.append("}")
    with open(os.path.join(work, "routing.asl"), "w") as source:
        source.write("\n".join(lines) + "\n")


def run(bin_directory, work, tables, commands):
    """What acpiexec prints, given the tables, then @p commands on its standard input."""
    return acpiexec_idle.run(bin_directory, work, tables, commands, OPTIONS)


def self_test():
    """Checks the decoding and the link rule on made-up templates; returns the count of cases that differ."""
    end = bytes([0x79, 0x00])

    def irq(mask, flags=None):
        information = bytes([flags]) if flags is not None else b""
        return bytes([0x22 + len(information), mask & 0xFF, mask >> 8]) + information

    def extended(flags, *numbers):
        return bytes([0x89, 2 + 4 * len(numbers), 0, flags, len(numbers)]) + \
            b"".join(number.to_bytes(4, "little") for number in numbers)

    cases = [
        ("IRQ 11 in _CRS, IRQ 10 and 11 level low in _PRS", irq(1 << 11, 0x18), irq(0x0C00, 0x18), (11, True, True)),
        ("Extended 17 edge low in _CRS, 16 and 17 level high in _PRS", extended(0x07, 17), extended(0x09, 16, 17),
         (17, True, False)),
        ("IRQ 12 in _CRS, not among 10 and 11", irq(1 << 12, 0x18), irq(0x0C00, 0x18), None),
        ("an empty IRQ before Extended 16 in _CRS", irq(0, 0x18) + extended(0x09, 16), extended(0x09, 16),
         (16, True, False)),
        ("IRQ 0 in _CRS, never listed", irq(1, 0x18), irq(0x0003, 0x18), None),
        ("IRQ 5 without its information byte in both", irq(1 << 5), irq(1 << 5), (5, False, False)),
        ("IRQ 9 level active-low exclusive in _PRS", irq(1 << 9, 0x08), irq(1 << 9, 0x08), (9, True, True)),
        ("IRQ 9 level active-high shared in _PRS", irq(1 << 9, 0x10), irq(1 << 9, 0x10), (9, True, False)),
        ("a _CRS that runs past its bytes", bytes([0x89, 0x10, 0x00, 0x09]), extended(0x09, 16), None),
    ]
    failures = 0
    for what, current, possible, expected in cases:
        found = {"\\LNKA._HID": ("integer", LINK[0]), "\\LNKA._CRS": ("buffer", current + end),
                 "\\LNKA._PRS": ("buffer", possible + end)}
        got = read_link(found, {"\\LNKA": ("Device", "")}, "\\LNKA")
        if got != expected:
            print("self-test: %s reads %s, expected %s" % (what, got, expected))
            failures += 1
    print("self-test: pass" if failures == 0 else "self-test: %d failure(s)" % failures)
    return failures


def main():
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(1 if self_test() else 0)
    bin_directory, presets, work = os.path.abspath(sys.argv[1]), acpiexec_idle.read_presets(sys.argv[2]), tempfile.mkdtemp()
    try:
        tables = []
        for index, table in enumerate(sys.argv[3:]):
            tables.append("table%03u.dat" % index)
            shutil.copyfile(table, os.path.join(work, tables[-1]))
        with open(os.path.join(work, "nvs.txt"), "w") as nvs:
            nvs.write("".join(line + "\n" for line in presets))
        nodes = {}
        for line in acpiexec_namespace.namespace(bin_directory, tables, work):
            match = acpiexec_idle.NODE.match(line)
            if match:
                nodes[match.group(1)] = (match.group(2), match.group(3) or "")
        candidates = [path for path, (kind, _) in nodes.items() if kind == "Device" and path + "._PRT" in nodes]
        if not candidates:
            return
        letters, compiled = prefixes(nodes, len(candidates)), "routing.aml"
        if letters is None:
            print("aml route unevaluated helper names")
            return
        helper(work, candidates, letters)
        subprocess.run([os.path.join(bin_directory, "iasl"), "-p", "routing", "routing.asl"], cwd=work,
                       capture_output=True, check=False)
        if not os.path.exists(os.path.join(work, compiled)):
            print("aml route unevaluated helper")
            return
        identity = ["evaluate %s.%s" % (path, name) for path in candidates
                    for name in ("_HID", "_CID", "_STA", "_SEG", "_BBN") if path + "." + name in nodes]
        counts = ["evaluate \\%s%03X" % (letters[0], index) for index in range(len(candidates))]
        found, _ = results(run(bin_directory, work, tables + [compiled], ["evaluate \\_PIC 1"] + identity + counts))
        bridges, commands = [], []
        for index, path in enumerate(candidates):
            if not (any(names(found.get(path + "._HID"), root) or names(found.get(path + "._CID"), root)
                        for root in ROOTS)) or not present(found, nodes, path):
                continue
            count = acpiexec_idle.integer_of(found.get("\\%s%03X" % (letters[0], index)))
            if count is None:
                continue
            segment = acpiexec_idle.integer_of(found.get(path + "._SEG")) or 0
            bus = acpiexec_idle.integer_of(found.get(path + "._BBN")) or 0
            bridges.append((path, index, segment, bus, count))
            commands += ["evaluate \\%s%03X %u" % (letters[1], index, entry) for entry in range(count)]
        entries_found, addresses = results(run(bin_directory, work, tables + [compiled],
                                               ["evaluate \\_PIC 1"] + commands + ["namespace"]))
        entries, wanted = {}, {}
        for path, index, _, _, count in bridges:
            entries[path] = [entries_found.get("\\%s%03X %u" % (letters[1], index, entry)) for entry in range(count)]
            for entry in entries[path]:
                if entry is not None and entry[0] == "package" and len(entry[1]) == 4 and \
                        entry[1][2][0] == "reference" and entry[1][2][1] in addresses:
                    wanted[entry[1][2][1]] = addresses[entry[1][2][1]]
        link_commands = ["evaluate %s.%s" % (path, name) for path in sorted(set(wanted.values()))
                         for name in ("_HID", "_CID", "_STA", "_PRS", "_CRS") if path + "." + name in nodes]
        link_found, _ = results(run(bin_directory, work, tables, ["evaluate \\_PIC 1"] + link_commands))
        links = {address: (path, read_link(link_found, nodes, path)) for address, path in wanted.items()}
        print("\n".join(routes([(path, segment, bus) for path, _, segment, bus, _ in bridges], entries, links)))
    finally:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
