"""Whether a machine relies on ACPICA's slack mode: its needs path evaluated by acpiexec, then by `acpiexec -es`, and
the two runs compared result by result (specification §1.4, §8.6).

Usage: python3 -I tools/acpi/acpiexec_slack.py <acpica bin directory> <MADT lines> <presets> <table>...
       python3 -I tools/acpi/acpiexec_slack.py --self-test

The MADT lines, the presets and the tables are those of tools/acpi/acpiexec_idle.py, which stages the machine the same
way: the tables `test-acpi --loaded` writes, the DSDT first, loaded statically, the NVS values of the presets, and the
fill of the simulated regions. acpiexec's own initialisation of devices, their _STA and _INI and its _REG walk, does
not run (`-di`); what acpiexec still runs of its own, once it initialises its events, comes before the first
evaluation in both runs: the _REG of the spaces whose handlers it installs then (aeinstall.c, through
AcpiInstallAddressSpaceHandler) and the _STA of every device its tests read (aetests.c, through AcpiGetDevices). The
evaluations given on its standard input are those of the needs path, §1.3, in the order of §3.8: \\_INI and \\_SB._INI
(step 2); the _REG of each scope that declares a PCI_Config region, for that space (step 3); the _ADR, _SEG, _BBN,
_HID and _CID of the scope that holds such a region and of each one above it, which its attach reads (§3.6); the walk
of step 4, its _STA and _INI; the _HID and _UID of the processor Devices; the handshake of steps 6 and 7; the _STA of
each processor object with a _CST and of what lies above it; then each _CST. The walk is approximate: a first run
decides it by reading every _STA it may meet before any _INI runs, so a _STA whose value an _INI sets may be read
otherwise than the kernel reads it. The same evaluations run twice, without and with `-es`, which returns a method's
last value where it ends without Return, reads an uninitialised local or argument as Integer 0, and accepts what ACPI
forbids: a store of any object, a field past its region's length rounded up to its access width.

The two runs agree when their loads end with the same failures, every `ACPI Error`, `ACPI Exception` and `ACPI Warning`
line printed while the tables load, each pointer into acpiexec's memory masked, but not what acpiexec prints once it
initialises its events; and when each evaluation ends with the same status and the same result. An _INI, a _REG and a
_PDC, whose results no caller reads, compare on status only, and so does a method that reads Timer, in its body or
through a method it calls, as tools/acpi/aml_census.py finds it in the same tables: two runs read two clocks.

Prints `aml slack agree evaluations=<n> status_only=<n> load_lines=<n>` when the two runs agree, else one
`aml slack differ <what> plain=<result> slack=<result>` line per difference, a run that stops where the other goes on
included, its result `stopped`. When both runs stop before the same evaluation, it prints only
`aml slack unevaluated <path>`, the first path neither reached.

--self-test checks the comparison on made-up acpiexec output, and the evaluations on a made-up namespace, each verdict
and each list written by hand, and exits 1 when one differs.
"""
import collections
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import acpiexec_idle  # noqa: E402
import aml_census  # noqa: E402

SLACK = ["-es"]
NO_INITIALISATION = ["-di"]
ROOT = "\\"
PCI_CONFIG = 2
REGISTERED = 1
PRESENT = 0x01
FUNCTIONING = 0x08
STATUS_ONLY_NAMES = ("_INI", "_REG", "_PDC")
PCI_IDENTITY = ("_ADR", "_SEG", "_BBN", "_HID", "_CID")
PROMPT = re.compile(r"^- evaluate (\S+)")
EVALUATE = re.compile(r"^evaluate (\S+)")
NO_OBJECT = re.compile(r"^No object was returned from evaluation of (\S+)")
REPORTED = re.compile(r"ACPI (?:Error|Exception|Warning): .*")
POINTER = re.compile(r"\b0x[0-9a-f]{12}\b")
EVENTS = "Initializing General Purpose Events"
RENDERED_WIDTH = 160


class Run:
    """What one acpiexec run printed: the failures its load reported, then each evaluation's result, in order."""

    def __init__(self, load, results):
        self.load, self.results = load, results


def load_lines(lines):
    """The failures a load reports, which the runs compare, each pointer into acpiexec's memory as `0x?`: twelve
    lowercase hexadecimal digits, as a 64-bit user-space pointer prints, where ACPICA prints an address of the
    machine's in uppercase."""
    return collections.Counter(POINTER.sub("0x?", reported.group(0)) for reported in map(REPORTED.search, lines)
                               if reported)


def evaluation(lines):
    """The status and the result of one evaluation's lines, or None when they hold none."""
    for position, line in enumerate(lines):
        if acpiexec_idle.FAILED.match(line):
            return line.split(" failed with status ", 1)[1].split()[0], None
        if NO_OBJECT.match(line):
            return "AE_OK", ("none", None)
        if acpiexec_idle.RETURNED.match(line):
            try:
                found, _ = acpiexec_idle.read_object(lines, position + 1)
            except (acpiexec_idle.Unreadable, IndexError):
                found = ("unreadable", None)
            return "AE_OK", found
    return None


def parse(output):
    """A Run from what acpiexec printed: what precedes the initialisation of its events is its load, what follows,
    until the first prompt, its own probing, left out, and what follows each `- evaluate` prompt, up to the next
    prompt, that evaluation."""
    load, results, current, loaded = [], [], None, False
    for line in output.splitlines():
        if line.startswith(EVENTS):
            loaded = True
        elif line.startswith("- "):
            if current is not None:
                results.append((current[0], evaluation(current[1])))
            prompt = PROMPT.match(line)
            current, loaded = ((prompt.group(1), []) if prompt else None), True
        elif current is not None:
            current[1].append(line)
        elif not loaded:
            load.append(line)
    if current is not None:
        results.append((current[0], evaluation(current[1])))
    return Run(load_lines(load), results)


def rendered_object(found):
    """An object as acpiexec_idle.read_object reads it, on one line."""
    kind, value = found
    if kind == "integer":
        return "Integer 0x%X" % value
    if kind == "string":
        return 'String "%s"' % value
    if kind == "buffer":
        return "Buffer %s" % (value.hex(" ").upper() or "empty")
    if kind == "package":
        return "Package {%s}" % ", ".join(rendered_object(element) for element in value)
    return kind


def render(result):
    """A result as one line of at most RENDERED_WIDTH characters: its status, then its object."""
    status, found = result
    text = status if found is None else "%s %s" % (status, rendered_object(found))
    return text if len(text) <= RENDERED_WIDTH else text[:RENDERED_WIDTH - 3] + "..."


def padded(path):
    """An absolute path with each segment padded to four characters, as ACPI stores it."""
    return "\\" + ".".join(segment.ljust(4, "_") for segment in path.lstrip("\\").split("."))


def compares_status_only(path, timer_readers):
    """Whether the evaluation of @p path compares on status only: an _INI, a _REG, a _PDC, or a method that reads
    Timer."""
    return padded(path) in timer_readers or path.rsplit(".", 1)[-1] in STATUS_ONLY_NAMES


def reached(run, paths):
    """How many of @p paths a Run evaluated, in order, each result of its own path."""
    count = 0
    for (path, result), wanted in zip(run.results, paths):
        if path != wanted or result is None:
            break
        count += 1
    return count


def compare(commands, plain, slack, timer_readers):
    """The lines that say how two Runs of @p commands compare: a result compared on status only when its command's path
    is an _INI, a _REG, a _PDC or one of @p timer_readers; a run that stops where the other goes on differs there; a
    single `unevaluated` line when both stop before the same command."""
    paths = [EVALUATE.match(command).group(1) for command in commands]
    plain_reached, slack_reached = reached(plain, paths), reached(slack, paths)
    if plain_reached == slack_reached < len(paths):
        return ["aml slack unevaluated %s" % paths[plain_reached]]
    lines = ["aml slack differ load plain=%s slack=none" % line
             for line in sorted((plain.load - slack.load).elements())]
    lines += ["aml slack differ load plain=none slack=%s" % line
              for line in sorted((slack.load - plain.load).elements())]
    status_only, both = 0, min(plain_reached, slack_reached)
    for path, (_, mine), (_, theirs) in zip(paths[:both], plain.results, slack.results):
        on_status = compares_status_only(path, timer_readers)
        status_only += 1 if on_status else 0
        if (mine[0] != theirs[0]) if on_status else (mine != theirs):
            lines.append("aml slack differ %s plain=%s slack=%s" % (path, render(mine), render(theirs)))
    if plain_reached != slack_reached:
        mine = render(plain.results[both][1]) if plain_reached > both else "stopped"
        theirs = render(slack.results[both][1]) if slack_reached > both else "stopped"
        lines.append("aml slack differ %s plain=%s slack=%s" % (paths[both], mine, theirs))
    return lines or ["aml slack agree evaluations=%u status_only=%u load_lines=%u"
                     % (len(paths), status_only, sum(plain.load.values()))]


def made_up_output(load, probing, evaluations, outstanding):
    """acpiexec's output for made-up lines of its load, of its own probing after it, and (path, result lines)
    evaluations, and the count of allocations it reports still out when it quits."""
    lines = ["ACPI table initialization:"] + load + ["Initializing General Purpose Events (GPEs):"] + probing
    for path, result in evaluations:
        lines += ["- evaluate %s" % path, "Evaluating %s" % path] + result
    return "\n".join(lines + ["- quit", "ACPI Error: %u (0x%X) Outstanding cache allocations (20260930/uttrack-764)"
                               % (outstanding, outstanding)])


def integer(path, value, address="0x5e0193ba3e00"):
    """The lines of an evaluation of @p path that returned an Integer."""
    return ["Evaluation of %s returned object %s, external buffer length 18" % (path, address),
            "  [Integer] = %016X" % value, ""]


def buffer(path, data):
    """The lines of an evaluation of @p path that returned a Buffer of @p data, at most 16 bytes."""
    return ["Evaluation of %s returned object 0x5e0193ba3e00, external buffer length 28" % path,
            "  [Buffer] Length %02X = " % len(data), "    0000: %s" % " ".join("%02X" % byte for byte in data), ""]


def replaced_path(evaluations, path, other):
    """Made-up evaluations, the evaluation of @p path made of @p other instead."""
    return [((other, [line.replace(path, other) for line in lines]) if evaluated == path else (evaluated, lines))
            for evaluated, lines in evaluations]


def replaced(evaluations, path, result):
    """Made-up evaluations, the result of @p path replaced by @p result."""
    return [(evaluated, result if evaluated == path else lines) for evaluated, lines in evaluations]


def self_test():
    """Checks compare() on made-up outputs of the two runs; the lines each case gives are written by hand. Returns the
    count of cases that differ."""
    status, cst, pdc, osc = "\\_SB_.PCI0._STA", "\\_PR_.CPU0._CST", "\\_PR_.CPU0._PDC", "\\_SB._OSC"
    ini, reg = "\\_SB_.PCI0._INI", "\\_SB_.PCI0._REG"
    loaded = "ACPI Error: AE_NOT_FOUND, During name lookup/catalog (20260930/psobject-234)"
    aborted = "ACPI Error: AE_AML_NO_RETURN_VALUE, Missing or null operand (20260930/dsutils-666)"
    unattached = ("Final data object initialization: ACPI Error: No object attached to node [LTRE] %s "
                  "(20260930/exresnte-105)")
    probed = ("ACPI Error: No object was returned from \\DEV0._STA due to previous error (AE_NOT_EXIST) "
              "(20260930/uteval-93)")
    table = "ACPI Error: Region SystemMemory (ID=0) at 0x00000000%s is out of range (20260930/exfldio-147)"
    commands = ["evaluate %s" % status, "evaluate %s" % cst, "evaluate %s (01 00 00 00 01 00 00 00 18 03 00 00)" % pdc,
                "evaluate %s (6E B0 11 08) 1 2 (00 00 00 00 00 00 00 00)" % osc, "evaluate %s" % ini,
                "evaluate %s 2 1" % reg]
    agree = [(status, integer(status, 0x0F)), (cst, integer(cst, 3)),
             (pdc, ["No object was returned from evaluation of %s" % pdc]),
             (osc, buffer(osc, [0, 0, 0, 0, 1, 0, 0, 0])),
             (ini, ["No object was returned from evaluation of %s" % ini]),
             (reg, ["No object was returned from evaluation of %s" % reg])]
    other_buffer = buffer(osc, [0, 0, 0, 0, 0, 0, 0, 0])
    cases = [
        ("the same output", [loaded], agree, [loaded], agree, set(),
         ["aml slack agree evaluations=6 status_only=3 load_lines=1"]),
        ("the same results at other addresses", [loaded, unattached % "0x651103505d70"], agree,
         [loaded, unattached % "0x5affb6506ee0"], replaced(agree, status, integer(status, 0x0F, "0x57b19a81ee00")),
         set(), ["aml slack agree evaluations=6 status_only=3 load_lines=2"]),
        ("a _STA of another value", [], agree, [], replaced(agree, status, integer(status, 0x0B)), set(),
         ["aml slack differ \\_SB_.PCI0._STA plain=AE_OK Integer 0xF slack=AE_OK Integer 0xB"]),
        ("a _STA that returns nothing, then the last value", [],
         replaced(agree, status, ["No object was returned from evaluation of %s" % status]), [], agree, set(),
         ["aml slack differ \\_SB_.PCI0._STA plain=AE_OK none slack=AE_OK Integer 0xF"]),
        ("a _STA that reads an uninitialised local, then 0", [],
         replaced(agree, status, ["Evaluation of %s failed with status AE_AML_UNINITIALIZED_LOCAL" % status]), [],
         agree, set(), ["aml slack differ \\_SB_.PCI0._STA plain=AE_AML_UNINITIALIZED_LOCAL slack=AE_OK Integer 0xF"]),
        ("a _CST that reads Timer, of another value", [], agree, [], replaced(agree, cst, integer(cst, 4)), {cst},
         ["aml slack agree evaluations=6 status_only=4 load_lines=0"]),
        ("a _CST that reads Timer, of another status", [], agree, [],
         replaced(agree, cst, ["Evaluation of %s failed with status AE_AML_TIMEOUT" % cst]), {cst},
         ["aml slack differ \\_PR_.CPU0._CST plain=AE_OK Integer 0x3 slack=AE_AML_TIMEOUT"]),
        ("a _PDC that returns nothing, then the last value", [], agree, [], replaced(agree, pdc, integer(pdc, 1)),
         set(), ["aml slack agree evaluations=6 status_only=3 load_lines=0"]),
        ("an _INI that returns nothing, then Integer 0", [], agree, [], replaced(agree, ini, integer(ini, 0)), set(),
         ["aml slack agree evaluations=6 status_only=3 load_lines=0"]),
        ("a _REG that returns nothing, then the last value", [], agree, [], replaced(agree, reg, integer(reg, 1)),
         set(), ["aml slack agree evaluations=6 status_only=3 load_lines=0"]),
        ("an _OSC of another Buffer", [], agree, [], replaced(agree, osc, other_buffer), set(),
         ["aml slack differ \\_SB._OSC plain=AE_OK Buffer 00 00 00 00 01 00 00 00 "
          "slack=AE_OK Buffer 00 00 00 00 00 00 00 00"]),
        ("an _OSC that reads Timer, its path unpadded, of another Buffer", [], agree, [],
         replaced(agree, osc, other_buffer), {"\\_SB_._OSC"},
         ["aml slack agree evaluations=6 status_only=4 load_lines=0"]),
        ("table-level code that fails in one run only", [loaded, aborted], agree, [loaded], agree, set(),
         ["aml slack differ load plain=%s slack=none" % aborted]),
        ("table-level code that fails in the slack run only", [loaded], agree, [loaded, aborted], agree, set(),
         ["aml slack differ load plain=none slack=%s" % aborted]),
        ("a region address that differs, printed as a table address is", [table % "FED40000"], agree,
         [table % "FED41000"], agree, set(),
         ["aml slack differ load plain=%s slack=none" % (table % "FED40000"),
          "aml slack differ load plain=none slack=%s" % (table % "FED41000")]),
        ("the slack run only stops before an evaluation", [], agree, [], agree[:1], set(),
         ["aml slack differ \\_PR_.CPU0._CST plain=AE_OK Integer 0x3 slack=stopped"]),
        ("the plain run only stops before an evaluation", [], agree[:2], [], agree, set(),
         ["aml slack differ \\_PR_.CPU0._PDC plain=stopped slack=AE_OK none"]),
        ("both runs stop before the same evaluation", [], agree[:1], [], agree[:1], set(),
         ["aml slack unevaluated \\_PR_.CPU0._CST"]),
        ("a run whose evaluation is of another path", [], agree, [], replaced_path(agree, cst, "\\_PR_.CPU1._CST"),
         set(), ["aml slack differ \\_PR_.CPU0._CST plain=AE_OK Integer 0x3 slack=stopped"]),
    ]
    failures = 0
    for what, plain_load, plain_evaluations, slack_load, slack_evaluations, timer, expected in cases:
        plain = parse(made_up_output(plain_load, [probed], plain_evaluations, 4))
        slack = parse(made_up_output(slack_load, [], slack_evaluations, 22))
        found = compare(commands, plain, slack, timer)
        if found != expected:
            print("self-test: %s gives %s, expected %s" % (what, found, expected))
            failures += 1
    failures += walk_self_test()
    print("self-test: pass" if failures == 0 else "self-test: %d failure(s)" % failures)
    return failures


def child(scope, name):
    """The path of @p name in @p scope, the root included."""
    return scope + name if scope == ROOT else scope + "." + name


def parent(path):
    """The scope @p path is declared in."""
    return path.rsplit(".", 1)[0] if "." in path else ROOT


def kind_of(nodes, path):
    """The type acpiexec gives the node at @p path, or None when there is none."""
    return nodes[path][0] if path in nodes else None


def holding(nodes, name):
    """The paths whose subtree holds a node called @p name."""
    return {scope for path in nodes if path.rsplit(".", 1)[-1].lstrip(ROOT) == name
            for scope in acpiexec_idle.above(path)}


def pci_config_regions(nodes):
    """The paths of the PCI_Config regions, in declaration order."""
    return [path for path, (kind, value) in nodes.items() if kind == "Region" and value.startswith("[PCI_Config]")]


def boot_commands(nodes):
    """The evaluations of §3.8 steps 2 and 3: \\_INI, \\_SB._INI, then _REG(PCI_Config, 1) in each scope that declares
    a PCI_Config region."""
    commands = ["evaluate %s" % path for path in (child(ROOT, "_INI"), child(ROOT + "_SB_", "_INI"))
                if kind_of(nodes, path) == "Method"]
    scopes = dict.fromkeys(parent(path) for path in pci_config_regions(nodes))
    return commands + ["evaluate %s %X %X" % (child(scope, "_REG"), PCI_CONFIG, REGISTERED) for scope in scopes
                       if kind_of(nodes, child(scope, "_REG")) == "Method"]


def pci_identity_commands(nodes):
    """The evaluations a PCI_Config region's attach makes (§3.6): the _ADR, _SEG, _BBN, _HID and _CID of the scope that
    holds such a region and of each one above it."""
    holders = dict.fromkeys(scope for path in pci_config_regions(nodes) for scope in acpiexec_idle.above(path))
    return ["evaluate %s" % child(scope, name) for scope in holders for name in PCI_IDENTITY
            if child(scope, name) in nodes]


def presence_probe_commands(nodes):
    """The evaluations of the _STA of every Device, Processor and ThermalZone whose subtree holds an _INI: what the walk
    of §3.8 step 4 may read."""
    holds = holding(nodes, "_INI")
    return ["evaluate %s" % child(path, "_STA") for path, (kind, _) in nodes.items()
            if kind in acpiexec_idle.DEVICES and path in holds and child(path, "_STA") in nodes]


def walk_commands(nodes, presence):
    """The evaluations of the walk of §3.8 step 4, given each _STA's result in @p presence: from the root, in
    declaration order, through the subtrees that hold an _INI; a device without _STA is present; a present one runs its
    _INI, but \\_SB, which step 2 ran, and is descended into; one functioning but not present is descended into; one
    whose _STA fails runs no _INI and is descended into; any other is left with its subtree."""
    children, holds, commands = collections.defaultdict(list), holding(nodes, "_INI"), []
    for path in nodes:
        children[parent(path)].append(path)

    def visit(scope):
        for path in (path for path in children[scope] if path in holds):
            if kind_of(nodes, path) not in acpiexec_idle.DEVICES:
                visit(path)
                continue
            status = acpiexec_idle.NO_STATUS
            if child(path, "_STA") in nodes:
                commands.append("evaluate %s" % child(path, "_STA"))
                status = acpiexec_idle.integer_of(presence.get(child(path, "_STA")))
            if status is not None and status & PRESENT and path != ROOT + "_SB_" and child(path, "_INI") in nodes:
                commands.append("evaluate %s" % child(path, "_INI"))
            if status is None or status & (PRESENT | FUNCTIONING):
                visit(path)

    visit(ROOT)
    return commands


def probe(machine, commands, options):
    """Each path @p commands evaluate in one run of acpiexec with @p options, and its object as acpiexec_idle reads
    results: None when the evaluation failed or returned no object; acpiexec_idle.Stopped when the run did not reach
    one."""
    run = parse(acpiexec_idle.run(machine.bin_directory, machine.work, machine.tables, commands, options))
    found = {}
    for index, command in enumerate(commands):
        if index >= len(run.results) or run.results[index][1] is None:
            raise acpiexec_idle.Stopped(EVALUATE.match(command).group(1))
        path, (status, result) = run.results[index]
        found[path] = result if status == "AE_OK" and result[0] != "none" else None
    return found


def walk_self_test():
    """Checks the needs-path evaluations on a made-up namespace; the lists are written by hand. Returns the count of
    lists that differ."""
    nodes = collections.OrderedDict((path, (kind, value)) for path, kind, value in [
        ("\\_INI", "Method", ""), ("\\_SB_", "Device", ""), ("\\_SB_._INI", "Method", ""),
        ("\\_SB_.PCI0", "Device", ""), ("\\_SB_.PCI0._STA", "Method", ""), ("\\_SB_.PCI0._INI", "Method", ""),
        ("\\_SB_._REG", "Method", ""), ("\\_SB_.PCI0._REG", "Method", ""),
        ("\\_SB_.PCI0.HBUS", "Region", "[PCI_Config] Addr 0 Len 0100"),
        ("\\_SB_.PCI0._ADR", "Integer", "= 0"), ("\\_SB_.PCI0._HID", "String", 'Length 07 "PNP0A08"'),
        ("\\_SB_.PCI0.DEV1", "Device", ""), ("\\_SB_.PCI0.DEV1._STA", "Method", ""),
        ("\\_SB_.PCI0.DEV1._REG", "Method", ""), ("\\_SB_.PCI0.DEV1.MMIO", "Region", "[SystemMemory] Addr 0 Len 10"),
        ("\\_SB_.ABS0", "Device", ""), ("\\_SB_.ABS0._STA", "Method", ""), ("\\_SB_.ABS0.CHLD", "Device", ""),
        ("\\_SB_.ABS0.CHLD._INI", "Method", ""), ("\\_SB_.FUN0", "Device", ""), ("\\_SB_.FUN0._STA", "Method", ""),
        ("\\_SB_.FUN0._INI", "Method", ""), ("\\_SB_.FUN0.CHLD", "Device", ""), ("\\_SB_.FUN0.CHLD._INI", "Method", ""),
        ("\\_SB_.FAIL", "Device", ""), ("\\_SB_.FAIL._STA", "Method", ""), ("\\_SB_.FAIL._INI", "Method", ""),
        ("\\_SB_.FAIL.CHLD", "Device", ""), ("\\_SB_.FAIL.CHLD._INI", "Method", ""), ("\\_PR_", "Scope", ""),
        ("\\_PR_.CPU0", "Processor", "ID 00 Len 06 Addr 0000000000000410"), ("\\_PR_.CPU0._STA", "Method", ""),
        ("\\_PR_.CPU0._INI", "Method", ""), ("\\_GPE", "Scope", ""), ("\\_GPE._L01", "Method", "")])
    presence = {"\\_SB_.PCI0._STA": ("integer", 0x0F), "\\_SB_.ABS0._STA": ("integer", 0),
                "\\_SB_.FUN0._STA": ("integer", 0x08), "\\_SB_.FAIL._STA": None, "\\_PR_.CPU0._STA": ("integer", 0x0F)}
    cases = [
        ("steps 2 and 3", boot_commands(nodes),
         ["evaluate \\_INI", "evaluate \\_SB_._INI", "evaluate \\_SB_.PCI0._REG 2 1"]),
        ("the identity a PCI_Config region reads", pci_identity_commands(nodes),
         ["evaluate \\_SB_.PCI0._ADR", "evaluate \\_SB_.PCI0._HID"]),
        ("the _STA the walk may read", presence_probe_commands(nodes),
         ["evaluate \\_SB_.PCI0._STA", "evaluate \\_SB_.ABS0._STA", "evaluate \\_SB_.FUN0._STA",
          "evaluate \\_SB_.FAIL._STA", "evaluate \\_PR_.CPU0._STA"]),
        ("the walk", walk_commands(nodes, presence),
         ["evaluate \\_SB_.PCI0._STA", "evaluate \\_SB_.PCI0._INI", "evaluate \\_SB_.ABS0._STA",
          "evaluate \\_SB_.FUN0._STA", "evaluate \\_SB_.FUN0.CHLD._INI", "evaluate \\_SB_.FAIL._STA",
          "evaluate \\_SB_.FAIL.CHLD._INI", "evaluate \\_PR_.CPU0._STA", "evaluate \\_PR_.CPU0._INI"]),
    ]
    failures = 0
    for what, found, expected in cases:
        if found != expected:
            print("self-test: %s gives %s, expected %s" % (what, found, expected))
            failures += 1
    return failures


def main():
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(1 if self_test() else 0)
    if len(sys.argv) < 5:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        sys.exit(2)
    with tempfile.TemporaryDirectory() as work:
        machine = acpiexec_idle.Machine(os.path.abspath(sys.argv[1]), acpiexec_idle.read_slots(sys.argv[2]),
                                        acpiexec_idle.read_presets(sys.argv[3]), sys.argv[4:], work)
        options = acpiexec_idle.OPTIONS + NO_INITIALISATION
        probe_commands = (boot_commands(machine.nodes) + machine.identity_commands()
                          + presence_probe_commands(machine.nodes))
        try:
            machine.identity = probe(machine, probe_commands, options)
        except acpiexec_idle.Stopped as stopped:
            print("aml slack unevaluated %s" % stopped)
            return
        commands = (boot_commands(machine.nodes) + pci_identity_commands(machine.nodes)
                    + walk_commands(machine.nodes, machine.identity) + machine.identity_commands()
                    + machine.handshake_commands() + machine.presence_commands() + machine.idle_commands())
        plain, slack = (parse(acpiexec_idle.run(machine.bin_directory, work, machine.tables, commands, given))
                        for given in (options, options + SLACK))
        blocks = []
        for table in machine.tables:
            with open(os.path.join(work, table), "rb") as data:
                blocks.append((table, data.read()))
        print("\n".join(compare(commands, plain, slack, aml_census.methods_reading_timer(blocks))))


if __name__ == "__main__":
    main()
