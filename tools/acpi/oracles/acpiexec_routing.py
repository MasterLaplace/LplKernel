"""The PCI interrupt routes a machine publishes, worked out apart from the kernel's code: its root bridges, the
bridges below them and their _PRT entries from what ACPICA's acpiexec evaluates once told the APIC model, the interrupt
links those entries name decoded here from their _PRS and _CRS, as the `aml route` lines `test-acpi --initialize`
prints (specification §3.11).

Usage: python3 -I tools/acpi/oracles/acpiexec_routing.py <acpica bin directory> <presets> <table>...
       python3 -I tools/acpi/oracles/acpiexec_routing.py --self-test

--self-test checks the descriptor decoding and the rule that reads a link on made-up templates, then the lines a swapped
entry, an entry whose link gives no interrupt and a wired pin print, then the bridges taken below a made-up root bridge,
each expected route and bridge written by hand, and exits 1 when one differs.

The tables are those `test-acpi --loaded` writes, the DSDT first. acpiexec loads them without running them to list the
namespace (tools/acpi/oracles/acpiexec_namespace.py). Its debugger buffer holds 16 KiB, which a _PRT of 128 entries
overflows, so an SSDT written and compiled here with the pinned iasl gives each Device that holds a _PRT two methods:
one the count of its entries, one an entry by its index. Each run has region fill 0, no repair of return values and
the presets as NVS values (`-fv 0 -dr -fi nvs.txt`), runs _REG, _STA and _INI, and evaluates `\\_PIC 1` first. The
first evaluates the _HID, _CID, _STA, _SEG and _BBN of each such Device and of each Device above one, the _STA of each
Device above those, the count of each one's entries, then the _ADR, _STA and _HID of each Device child of them; the
second the entries of the root bridges and of the bridges below them, then lists the namespace, whose node addresses
tell which link an entry names; the third the _HID, _CID, _STA, _PRS and _CRS of each link.

A root bridge is a Device whose _HID or _CID names PNP0A03, as an EisaId or a String; its _PRT is read when its _STA,
0x0F without one, and that of each Device above it say present or functioning, one that fails keeping it out, and when
its _SEG and _BBN, 0 without one, do not fail. Below it, a bridge is the Device Linux takes as the ACPI companion of a
PCI-to-PCI bridge (acpi_pci_find_companion, acpi_find_child_device): a Device child of the root bridge or of the bridge
above it whose _ADR, an Integer, names a device below 32 and a function below 8; of the children whose _ADR names the
same one, the first alone, or the first scored best, each scored once a second is met, the walk stopping at the first
scored high: without children, refused; without _STA, or with one that fails with AE_NOT_FOUND, middle for a video
device (a _DOD, _DOS or _ROM, or a _VPO, _GPD and _SPD), low otherwise; refused when its _STA fails otherwise or lacks
bit 1; low with a _HID that gives an Integer or a String, high without; never, right below the root bridge, one whose
_ADR is 0 and that has such a _HID. A bridge is kept when it or a Device below it holds a _PRT, below the first 16 root
bridges read, up to 8 below the root bridge and 64 in all, its children past the first 256 left out; its entries come
after those of every root bridge, depth first in declaration order, and each one's line names the bridges from the root
bridge down, as `/<device>.<function>`. An entry is a Package of four: an Integer Address whose bits 16 to 31 name a
device below 32, an Integer Pin below 4, a Source and an Integer SourceIndex, the two read swapped when the SourceIndex
is no Integer, as ACPICA repairs a _PRT, which `-dr` and the helper's names keep acpiexec from doing. A Source that is
an Integer wires the pin to the SourceIndex, level-triggered and active-low. A Source that references a Device names it,
and the pin goes to the interrupt it gives as a link, read when the SourceIndex is 0: present or functioning by its
_STA, whose _HID or _CID names PNP0C0F; its current interrupt the first one of the first IRQ or Extended Interrupt
descriptor of its _CRS that names one; kept when the first such descriptor of its _PRS lists it, 0 never listed, with
that descriptor's trigger and polarity; each template read only when ACPICA would convert it: each descriptor within its
bytes, each interrupt descriptor whole, an End Tag of two bytes closing it. Such an entry whose Device gives no
interrupt is printed `none`, and one whose Source is neither an Integer nor a reference to a Device `none malformed`, as
the kernel publishes it without an interrupt so that a lookup stops on it. Any other entry is left out."""
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
ROOT = (0x030AD041, "PNP0A03")
LINK = (0x0F0CD041, "PNP0C0F")
PRESENT_OR_FUNCTIONING = 0x09
NO_STATUS = 0x0F
ENABLED = 0x02
DEVICES, PINS, FUNCTIONS = 32, 4, 8
REFUSED, LOW, MIDDLE, HIGH = 1, 2, 3, 4
MAX_DEPTH, MAX_BRIDGES, MAX_CHILDREN, MAX_ROOTS = 8, 64, 256, 16
STATUS = re.compile(r"failed with status (\S+)")
OWNING = ("Device", "Processor", "Thermal", "Power")
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
            status = STATUS.search(line)
            if status:
                found[(command, "status")] = status.group(1)
        if acpiexec_idle.RETURNED.match(line) and command is not None:
            try:
                found[command], position = read_object(lines, position + 1)
                continue
            except (acpiexec_idle.Unreadable, IndexError):
                found[command] = None
        position += 1
    return found, addresses


def whole(name, data):
    """Whether an IRQ (0x04) or Extended Interrupt (0x89) descriptor holds the bytes its form takes, as ACPICA
    validates it: an IRQ two or three, an Extended Interrupt at least six and its table of at least one interrupt."""
    if name == 0x04:
        return len(data) in (2, 3)
    if name == 0x89:
        return len(data) >= 6 and data[1] >= 1 and 2 + 4 * data[1] <= len(data)
    return True


def descriptors(template):
    """Each descriptor of a resource template before its End Tag, as (name, data); None when one runs past the bytes,
    an interrupt descriptor is not whole, or no End Tag of two bytes closes the template, as ACPICA refuses such a
    template (AcpiUtWalkAmlResources)."""
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
                return found if offset + 2 <= len(template) else None
        if start + length > len(template) or not whole(name, template[start:start + length]):
            return None
        found.append((name, template[start:start + length]))
        offset = start + length
    return None


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


def ancestors(nodes, path):
    """The Devices above @p path, nearest first."""
    parts = path.split(".")
    above = [".".join(parts[:end]) for end in range(len(parts) - 1, 0, -1)]
    return [node for node in above if nodes.get(node, ("",))[0] == "Device"]


def number(found, nodes, path, name):
    """The Integer @p path's @p name gives, 0 without one, None when it fails."""
    if path + "." + name not in nodes:
        return 0
    return acpiexec_idle.integer_of(found.get(path + "." + name))


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


def children_of(nodes):
    """Each node's children, in declaration order, keyed by the parent's path."""
    found = {}
    for path in nodes:
        found.setdefault(path.rsplit(".", 1)[0] if "." in path else "\\", []).append(path)
    return found


def function_address(found, nodes, path):
    """The PCI function the _ADR of @p path names, device << 16 | function, or None: no _ADR, one that fails, or one
    past device 31 or function 7."""
    if path + "._ADR" not in nodes:
        return None
    value = acpiexec_idle.integer_of(found.get(path + "._ADR"))
    if value is None or value >> 16 >= DEVICES or value & 0xFFFF >= FUNCTIONS:
        return None
    return value


def has_identifier(found, nodes, path):
    """Whether @p path has a _HID that gives an Integer or a String."""
    result = found.get(path + "._HID") if path + "._HID" in nodes else None
    return result is not None and result[0] in ("integer", "string")


def score(found, nodes, kids, path):
    """How Linux scores @p path among Devices that name the same function, a bridge's children checked."""
    if not any(nodes[child][0] in OWNING for child in kids.get(path, [])):
        return REFUSED
    if path + "._STA" not in nodes or found.get((path + "._STA", "status")) == "AE_NOT_FOUND":
        video = any(path + name in nodes for name in ("._DOD", "._DOS", "._ROM")) or \
            all(path + name in nodes for name in ("._VPO", "._GPD", "._SPD"))
        return MIDDLE if video else LOW
    status = acpiexec_idle.integer_of(found.get(path + "._STA"))
    if status is None or not status & ENABLED:
        return REFUSED
    return LOW if has_identifier(found, nodes, path) else HIGH


def choose(found, nodes, kids, group):
    """The Device Linux takes among @p group, Devices that name the same function, in declaration order."""
    chosen, best = group[0], None
    for candidate in group[1:]:
        if best is None:
            best = score(found, nodes, kids, chosen)
            if best == HIGH:
                break
        value = score(found, nodes, kids, candidate)
        if value > best:
            chosen, best = candidate, value
        if best == HIGH:
            break
    return chosen


def holds_table(nodes, path):
    """Whether @p path, or a node below it, has a _PRT child whose parent is a Device."""
    return any(node.startswith(path + ".") and node.endswith("._PRT") and nodes.get(node[:-5], ("",))[0] == "Device"
               for node in nodes)


def bridges_below(found, nodes, kids, root, published):
    """The bridges below root bridge @p root, as (path, label, its bridges) trees in declaration order, taken in the
    kernel's order, all the bridges right below one before those below them; @p published, a one-element list, counts
    the bridges taken below every root bridge."""
    def below(scope, label, depth):
        functions = [(child, function_address(found, nodes, child)) for child in kids.get(scope, [])
                     if nodes[child][0] == "Device"]
        functions = [(child, address) for child, address in functions if address is not None][:MAX_CHILDREN]
        seen, mine = set(), []
        for _, address in functions:
            if address in seen:
                continue
            seen.add(address)
            chosen = choose(found, nodes, kids, [child for child, other in functions if other == address])
            if (depth == 1 and address == 0 and has_identifier(found, nodes, chosen)) or \
                    not holds_table(nodes, chosen):
                continue
            if depth > MAX_DEPTH or published[0] == MAX_BRIDGES:
                continue
            published[0] += 1
            mine.append((chosen, "%s/%02X.%X" % (label, address >> 16, address & 0xFFFF)))
        return [(path, name, below(path, name, depth + 1)) for path, name in mine]

    return below(root, "", 1)


def routes(bridges, entries, links):
    """The `aml route` lines: @p bridges, each (path, segment, bus, bridges below), the root bridges in declaration
    order, each bridge below one a (path, label, bridges below) tree; @p entries, each bridge's list of entry results;
    @p links, each link path's (interrupt, level, active_low) or None."""
    lines = []

    def walk(path, segment, bus, label, below):
        lines.extend(table_lines(entries.get(path, []), "%04X:%02X%s" % (segment & 0xFFFF, bus & 0xFF, label), links))
        for child, name, deeper in below:
            walk(child, segment, bus, name, deeper)

    for path, segment, bus, _ in bridges:
        lines.extend(table_lines(entries.get(path, []), "%04X:%02X" % (segment & 0xFFFF, bus & 0xFF), links))
    for _, segment, bus, below in bridges:
        for child, name, deeper in below:
            walk(child, segment, bus, name, deeper)
    return lines


def table_lines(entries, where, links):
    """The `aml route` lines of one _PRT's @p entries, each prefixed with @p where, its root bus and bridges."""
    lines = []
    for entry in entries:
        if entry is None or entry[0] != "package" or len(entry[1]) != 4:
            continue
        address, pin, source, index = entry[1]
        if index[0] != "integer":
            source, index = index, source
        if address[0] != "integer" or pin[0] != "integer" or index[0] != "integer":
            continue
        device, pin = address[1] >> 16 & 0xFFFF, pin[1]
        if device >= DEVICES or pin >= PINS:
            continue
        if source[0] == "integer":
            route = (index[1], True, True, "wired")
        elif source[0] == "reference" and source[2] == "Device":
            if source[1] not in links:
                continue
            read = links[source[1]][1] if index[1] == 0 else None
            if read is None:
                lines.append("aml route %s:%02X.%c none %s" % (where, device, "ABCD"[pin], links[source[1]][0]))
                continue
            route = read + (links[source[1]][0],)
        else:
            lines.append("aml route %s:%02X.%c none malformed" % (where, device, "ABCD"[pin]))
            continue
        lines.append("aml route %s:%02X.%c interrupt=%u %s %s %s" % (
            where, device, "ABCD"[pin], route[0], "level" if route[1] else "edge", "low" if route[2] else "high",
            route[3]))
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
        ("IRQ 11 before an Extended Interrupt that runs past its bytes in _CRS", irq(1 << 11, 0x18) +
         bytes([0x89, 0x10, 0x00]), irq(1 << 11, 0x18), None, False),
        ("IRQ 11 in _CRS without an End Tag", irq(1 << 11, 0x18), irq(1 << 11, 0x18), None, False),
        ("IRQ 11 in _CRS closed by one byte of an End Tag", irq(1 << 11, 0x18) + bytes([0x79]), irq(1 << 11, 0x18),
         None, False),
        ("an IRQ of one byte before IRQ 11 in _CRS", bytes([0x21, 0x00]) + irq(1 << 11, 0x18), irq(1 << 11, 0x18),
         None),
        ("an Extended Interrupt that names none before IRQ 11 in _CRS",
         bytes([0x89, 0x06, 0x00, 0x09, 0x00, 0, 0, 0, 0]) + irq(1 << 11, 0x18), irq(1 << 11, 0x18), None),
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
    for what, current, possible, expected, *closed in cases:
        closing = end if closed != [False] else b""
        found = {"\\LNKA._HID": ("integer", LINK[0]), "\\LNKA._CRS": ("buffer", current + closing),
                 "\\LNKA._PRS": ("buffer", possible + end)}
        got = read_link(found, {"\\LNKA": ("Device", "")}, "\\LNKA")
        if got != expected:
            print("self-test: %s reads %s, expected %s" % (what, got, expected))
            failures += 1
    first, second = ("reference", "0x1", "Device"), ("reference", "0x2", "Device")
    entries = {"\\PCI0": [
        ("package", [("integer", 0x0001FFFF), ("integer", 0), ("integer", 0), first]),
        ("package", [("integer", 0x0002FFFF), ("integer", 0), second, ("integer", 0)]),
        ("package", [("integer", 0x0003FFFF), ("integer", 1), ("integer", 0), ("integer", 20)]),
        ("package", [("integer", 0x0004FFFF), ("integer", 2), ("string", "LNKA"), ("integer", 0)]),
        ("package", [("integer", 0x0005FFFF), ("integer", 3), ("reference", "0x3", "Integer"), ("integer", 0)]),
    ]}
    links = {"0x1": ("\\LNKA", (11, True, True)), "0x2": ("\\LNKB", None)}
    expected = ["aml route 0000:00:01.A interrupt=11 level low \\LNKA", "aml route 0000:00:02.A none \\LNKB",
                "aml route 0000:00:03.B interrupt=20 level low wired", "aml route 0000:00:04.C none malformed",
                "aml route 0000:00:05.D none malformed"]
    got = routes([("\\PCI0", 0, 0, [])], entries, links)
    if got != expected:
        print("self-test: a swapped entry, a link that gives none, a wired pin and two unusable Sources read %s, "
              "expected %s"
              % (got, expected))
        failures += 1
    failures += self_test_bridges()
    print("self-test: pass" if failures == 0 else "self-test: %d failure(s)" % failures)
    return failures


def self_test_bridges():
    """Checks the bridges taken below a made-up root bridge, the one scored best of the Devices that name one function,
    and the lines their entries print; returns the count of cases that differ."""
    nodes, found, failures = {}, {}, 0

    def device(path, address=None, kind="Device", **objects):
        nodes[path] = (kind, "")
        if address is not None:
            nodes[path + "._ADR"] = ("Integer", "")
            found[path + "._ADR"] = ("integer", address)
        for name, value in objects.items():
            nodes[path + "." + name] = ("Integer", "")
            if value is not None:
                found[path + "." + name] = value

    enabled, string, eisa = ("integer", 0x0F), ("string", "LPLT0001"), ("integer", 0x020CD041)
    device("\\PCI0", _PRT=None)
    device("\\PCI0.RP01", 0x001C0000, _PRT=None)
    device("\\PCI0.RP01.PXSX", 0)
    device("\\PCI0.RP01.PXSX.DSP0", 0x00010000, _PRT=None)
    device("\\PCI0.RP02", 0x001C0001, _HID=string, _PRT=None)
    device("\\PCI0.RP02.HIDB", 0, _HID=string, _PRT=None)
    groups = [
        (0x001C0002, [("DUPA", None, {"_STA": enabled}), ("DUPB", "Device", {"_STA": enabled})], 1),
        (0x00020000, [("VIDB", "Device", {"_STA": enabled, "_HID": string}), ("VIDA", "Power", {"_DOS": None}),
                      ("VIDC", "Device", {"_STA": ("integer", 0x0D)})], 1),
        (0x00060000, [("TZA0", "Thermal", {"_STA": enabled}), ("TZB0", "Device", {"_STA": enabled, "_HID": string})],
         0),
        (0x00070000, [("PRA0", "Processor", {"_STA": enabled}), ("PRB0", "Device", {"_STA": enabled,
                                                                                    "_HID": string})], 0),
        (0x00080000, [("L080", "Device", {"_STA": enabled, "_HID": eisa}), ("R080", "Device", {"_ROM": None})], 1),
        (0x00090000, [("L090", "Device", {"_STA": enabled, "_HID": string}),
                      ("P090", "Device", {"_VPO": None, "_GPD": None, "_SPD": None})], 1),
        (0x000A0000, [("L0A0", "Device", {"_STA": enabled, "_HID": string}),
                      ("NA10", "Device", {"_VPO": None, "_GPD": None}), ("NA20", "Device", {"_VPO": None, "_SPD": None}),
                      ("NA30", "Device", {"_GPD": None, "_SPD": None})], 0),
        (0x000B0000, [("L0B0", "Device", {"_STA": enabled, "_HID": string}), ("D0B0", "Device", {"_DOD": None})], 1),
        (0x000E0000, [("NFA0", "Device", {"_STA": None}), ("NFB0", "Device", {"_STA": None})], 1),
    ]
    for address, members, _ in groups:
        for name, kind, objects in members:
            device("\\PCI0." + name, address, _PRT=None, **objects)
            if kind is not None:
                device("\\PCI0.%s.CHLD" % name, kind=kind)
    found[("\\PCI0.NFA0._STA", "status")] = "AE_AML_PACKAGE_LIMIT"
    found[("\\PCI0.NFB0._STA", "status")] = "AE_NOT_FOUND"
    device("\\PCI0.HIDD", 0, _HID=string, _PRT=None)
    device("\\PCI0.ALLF", 0x0003FFFF, _PRT=None)
    device("\\PCI0.BIGD", 0x00200000, _PRT=None)
    device("\\PCI0.EMPT", 0x00040000)
    device("\\PCI0.TZHD", 0x000D0000)
    device("\\PCI0.TZHD.TZ01", kind="Thermal", _PRT=None)
    path = "\\PCI0.DEEP"
    device(path, 0x00050000)
    for level in range(2, 10):
        path += ".DP%02u" % level
        device(path, 0, **({"_PRT": None} if level == 9 else {}))
    kids = children_of(nodes)
    tree = bridges_below(found, nodes, kids, "\\PCI0", [0])
    chain = [("\\PCI0.DEEP", "/05.0")]
    for level in range(2, 9):
        chain.append((chain[-1][0] + ".DP%02u" % level, chain[-1][1] + "/00.0"))
    deep = []
    for child, label in reversed(chain):
        deep = [(child, label, deep)]
    expected = [("\\PCI0.RP01", "/1C.0", [("\\PCI0.RP01.PXSX", "/1C.0/00.0",
                                             [("\\PCI0.RP01.PXSX.DSP0", "/1C.0/00.0/01.0", [])])]),
                ("\\PCI0.RP02", "/1C.1", [("\\PCI0.RP02.HIDB", "/1C.1/00.0", [])])]
    expected += [("\\PCI0." + members[chosen][0], "/%02X.%X" % (address >> 16, address & 0xFFFF), [])
                 for address, members, chosen in groups]
    expected += deep
    if tree != expected:
        print("self-test: the bridges below a made-up root bridge read %s, expected %s" % (tree, expected))
        failures += 1
    wired = [("package", [("integer", 0x0000FFFF), ("integer", pin), ("integer", 0), ("integer", 16 + pin)])
             for pin in range(3)]
    entries = {"\\PCI0": [wired[0]], "\\PCI0.RP01": [wired[1]], "\\PCI0.RP01.PXSX.DSP0": [wired[2]]}
    expected = ["aml route 0000:00:00.A interrupt=16 level low wired",
                "aml route 0000:00/1C.0:00.B interrupt=17 level low wired",
                "aml route 0000:00/1C.0/00.0/01.0:00.C interrupt=18 level low wired"]
    got = routes([("\\PCI0", 0, 0, tree)], entries, {})
    if got != expected:
        print("self-test: the entries of the bridges below a root bridge read %s, expected %s" % (got, expected))
        failures += 1
    entries["\\PCI1"] = [wired[0]]
    expected.insert(1, "aml route 0000:01:00.A interrupt=16 level low wired")
    got = routes([("\\PCI0", 0, 0, tree), ("\\PCI1", 0, 1, [])], entries, {})
    if got != expected:
        print("self-test: a second root bridge's entries read %s, expected %s before the bridges'" % (got, expected))
        failures += 1
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
        kids, holding = children_of(nodes), set(candidates)
        holding |= {above for candidate in candidates for above in ancestors(nodes, candidate)}
        scopes = [path for path in nodes if path in holding]
        identity = ["evaluate %s.%s" % (path, name) for path in scopes
                    for name in ("_HID", "_CID", "_STA", "_SEG", "_BBN") if path + "." + name in nodes]
        identity += ["evaluate %s._STA" % path for path in sorted({above for scope in scopes
                                                                    for above in ancestors(nodes, scope)} - holding)
                     if path + "._STA" in nodes]
        counts = ["evaluate \\%s%03X" % (letters[0], index) for index in range(len(candidates))]
        addresses_of = list(dict.fromkeys("evaluate %s.%s" % (child, name) for scope in scopes
                                          for child in kids.get(scope, []) if nodes[child][0] == "Device"
                                          for name in ("_ADR", "_STA", "_HID") if child + "." + name in nodes))
        found, _ = results(run(bin_directory, work, tables + [compiled],
                               ["evaluate \\_PIC 1"] + identity + counts + addresses_of))
        count_of = {path: acpiexec_idle.integer_of(found.get("\\%s%03X" % (letters[0], index)))
                    for index, path in enumerate(candidates)}
        bridges, published = [], [0]
        for path in scopes:
            if not (names(found.get(path + "._HID"), ROOT) or names(found.get(path + "._CID"), ROOT)) or \
                    not all(present(found, nodes, device) for device in [path] + ancestors(nodes, path)):
                continue
            segment, bus = number(found, nodes, path, "_SEG"), number(found, nodes, path, "_BBN")
            if segment is None or bus is None:
                continue
            bridges.append((path, segment, bus, bridges_below(found, nodes, kids, path, published)
                            if len(bridges) < MAX_ROOTS else []))
        read, stack = [], [(path, below) for path, _, _, below in reversed(bridges)]
        while stack:
            path, below = stack.pop()
            read.append(path)
            stack.extend((child, deeper) for child, _, deeper in reversed(below))
        commands = ["evaluate \\%s%03X %u" % (letters[1], candidates.index(path), entry) for path in read
                    if count_of.get(path) is not None for entry in range(count_of[path])]
        entries_found, addresses = results(run(bin_directory, work, tables + [compiled],
                                               ["evaluate \\_PIC 1"] + commands + ["namespace"]))
        entries, wanted = {}, {}
        for path in read:
            if count_of.get(path) is None:
                continue
            index = candidates.index(path)
            entries[path] = [entries_found.get("\\%s%03X %u" % (letters[1], index, entry))
                             for entry in range(count_of[path])]
            for entry in entries[path]:
                if entry is not None and entry[0] == "package" and len(entry[1]) == 4 and \
                        entry[1][2][0] == "reference" and entry[1][2][1] in addresses:
                    wanted[entry[1][2][1]] = addresses[entry[1][2][1]]
        link_commands = ["evaluate %s.%s" % (path, name) for path in sorted(set(wanted.values()))
                         for name in ("_HID", "_CID", "_STA", "_PRS", "_CRS") if path + "." + name in nodes]
        link_found, _ = results(run(bin_directory, work, tables, ["evaluate \\_PIC 1"] + link_commands))
        links = {address: (path, read_link(link_found, nodes, path)) for address, path in wanted.items()}
        print("\n".join(routes(bridges, entries, links)))
    finally:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
