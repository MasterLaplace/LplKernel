"""Whether a machine relies on ACPICA's slack mode: its needs path evaluated by acpiexec, then by `acpiexec -es`, and
the two runs compared result by result (specification §1.4, §8.6).

Usage: python3 -I tools/acpi/acpiexec_slack.py <acpica bin directory> <MADT lines> <presets> <table>...
       python3 -I tools/acpi/acpiexec_slack.py --self-test

The MADT lines, the presets and the tables are those of tools/acpi/acpiexec_idle.py, which stages the machine the same
way: the tables `test-acpi --loaded` writes, the DSDT first, loaded statically, the NVS values of the presets, and the
fill of the simulated regions. acpiexec runs _REG, _STA and _INI itself, then evaluates, given on its standard input,
the needs path of §1.3 a host can ask for: the _ADR, _SEG, _BBN, _HID and _CID of every Device that holds a PCI_Config
region or lies above one; the _HID and _UID of the processor Devices; the _STA of every Device, Processor and
ThermalZone; the handshake of §3.8 steps 6 and 7; the _STA of each processor object with a _CST and of what lies above
it; then each _CST. The same evaluations run twice, without and with `-es`, which returns a method's last value where
it ends without Return, reads an uninitialised local or argument as Integer 0, and accepts what ACPI forbids: a store
of any object, a field past its region's length rounded up to its access width.

The two runs agree when their initialisations end with the same failures, every `ACPI Error`, `ACPI Exception` and
`ACPI Warning` line printed before the first evaluation, and with the same count of _INI and _STA ACPICA ran; and when
each evaluation ends with the same status and the same result. A _PDC, whose result no caller reads, and a method that
reads Timer, in its body or through a method it calls, as tools/acpi/aml_census.py finds it in the same tables, compare
on status only: two runs read two clocks.

Prints `aml slack agree evaluations=<n> status_only=<n> initialisation_lines=<n>` when the two runs agree, else one
`aml slack differ <what> plain=<result> slack=<result>` line per difference. When acpiexec stops before an evaluation
in either run, it prints only `aml slack unevaluated <path>`, the first path that run did not reach.

--self-test checks the comparison on made-up acpiexec output, each verdict written by hand, and exits 1 when one
differs.
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
STATUS_ONLY_NAMES = ("_PDC",)
PCI_IDENTITY = ("_ADR", "_SEG", "_BBN", "_HID", "_CID")
PROMPT = re.compile(r"^- evaluate (\S+)")
EVALUATE = re.compile(r"^evaluate (\S+)")
NO_OBJECT = re.compile(r"^No object was returned from evaluation of (\S+)")
REPORTED = re.compile(r"ACPI (?:Error|Exception|Warning): .*")
INITIALISED = re.compile(r"^\s*(Executed \d+ _INI methods requiring \d+ _STA executions)")
RENDERED_WIDTH = 160


class Run:
    """What one acpiexec run printed: its initialisation's lines, then each evaluation's result, in order."""

    def __init__(self, initialisation, results):
        self.initialisation, self.results = initialisation, results


def initialisation_lines(lines):
    """The lines of an initialisation the runs compare: its failures and its count of _INI."""
    found = collections.Counter()
    for line in lines:
        reported, initialised = REPORTED.search(line), INITIALISED.match(line)
        if reported:
            found[reported.group(0)] += 1
        elif initialised:
            found[initialised.group(1)] += 1
    return found


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
    """A Run from what acpiexec printed: what precedes the first prompt is its initialisation, and what follows each
    `- evaluate` prompt, up to the next prompt, that evaluation."""
    initialisation, results, current, prompted = [], [], None, False
    for line in output.splitlines():
        if line.startswith("- "):
            if current is not None:
                results.append((current[0], evaluation(current[1])))
            prompt = PROMPT.match(line)
            current, prompted = ((prompt.group(1), []) if prompt else None), True
        elif current is not None:
            current[1].append(line)
        elif not prompted:
            initialisation.append(line)
    if current is not None:
        results.append((current[0], evaluation(current[1])))
    return Run(initialisation_lines(initialisation), results)


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
    """Whether the evaluation of @p path compares on status only: a _PDC, or a method that reads Timer."""
    return padded(path) in timer_readers or path.rsplit(".", 1)[-1] in STATUS_ONLY_NAMES


def compare(commands, plain, slack, timer_readers):
    """The lines that say how two Runs of @p commands compare: a result compared on status only when its command's path
    is a _PDC or one of @p timer_readers; a single `unevaluated` line when a run did not reach every command."""
    paths = [EVALUATE.match(command).group(1) for command in commands]
    for run in (plain, slack):
        for index, path in enumerate(paths):
            if index >= len(run.results) or run.results[index][1] is None:
                return ["aml slack unevaluated %s" % path]
    lines = ["aml slack differ initialisation plain=%s slack=none" % line
             for line in sorted((plain.initialisation - slack.initialisation).elements())]
    lines += ["aml slack differ initialisation plain=none slack=%s" % line
              for line in sorted((slack.initialisation - plain.initialisation).elements())]
    status_only = 0
    for path, (_, mine), (_, theirs) in zip(paths, plain.results, slack.results):
        on_status = compares_status_only(path, timer_readers)
        status_only += 1 if on_status else 0
        if (mine[0] != theirs[0]) if on_status else (mine != theirs):
            lines.append("aml slack differ %s plain=%s slack=%s" % (path, render(mine), render(theirs)))
    return lines or ["aml slack agree evaluations=%u status_only=%u initialisation_lines=%u"
                     % (len(paths), status_only, sum(plain.initialisation.values()))]


def made_up_output(initialisation, evaluations, outstanding):
    """acpiexec's output for made-up initialisation lines and (path, result lines) evaluations, and the count of
    allocations it reports still out when it quits."""
    lines = ["Initializing Device/Processor/Thermal objects and executing _INI/_STA methods:"] + initialisation
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


def self_test():
    """Checks compare() on made-up outputs of the two runs; the lines each case gives are written by hand. Returns the
    count of cases that differ."""
    status, cst, pdc, osc = "\\_SB_.PCI0._STA", "\\_PR_.CPU0._CST", "\\_PR_.CPU0._PDC", "\\_SB._OSC"
    executed = "    Executed 4 _INI methods requiring 1 _STA executions (examined 133 objects)"
    aborted = ("ACPI Error: Aborting method \\_SB.PCI0._INI due to previous error (AE_AML_NO_RETURN_VALUE) "
               "(20260930/psparse-529)")
    commands = ["evaluate %s" % status, "evaluate %s" % cst, "evaluate %s (01 00 00 00 01 00 00 00 18 03 00 00)" % pdc,
                "evaluate %s (6E B0 11 08) 1 2 (00 00 00 00 00 00 00 00)" % osc]
    agree = [(status, integer(status, 0x0F)), (cst, integer(cst, 3)),
             (pdc, ["No object was returned from evaluation of %s" % pdc]),
             (osc, buffer(osc, [0, 0, 0, 0, 1, 0, 0, 0]))]
    cases = [
        ("the same output", [executed], agree, [executed], agree, set(),
         ["aml slack agree evaluations=4 status_only=1 initialisation_lines=1"]),
        ("the same results at other addresses", [executed], agree, [executed],
         [(status, integer(status, 0x0F, "0x57b19a81ee00"))] + agree[1:], set(),
         ["aml slack agree evaluations=4 status_only=1 initialisation_lines=1"]),
        ("a _STA of another value", [], agree, [], [(status, integer(status, 0x0B))] + agree[1:], set(),
         ["aml slack differ \\_SB_.PCI0._STA plain=AE_OK Integer 0xF slack=AE_OK Integer 0xB"]),
        ("a _STA that returns nothing, then the last value", [],
         [(status, ["No object was returned from evaluation of %s" % status])] + agree[1:], [], agree, set(),
         ["aml slack differ \\_SB_.PCI0._STA plain=AE_OK none slack=AE_OK Integer 0xF"]),
        ("a _STA that reads an uninitialised local, then 0", [],
         [(status, ["Evaluation of %s failed with status AE_AML_UNINITIALIZED_LOCAL" % status])] + agree[1:], [],
         agree, set(), ["aml slack differ \\_SB_.PCI0._STA plain=AE_AML_UNINITIALIZED_LOCAL slack=AE_OK Integer 0xF"]),
        ("a _CST that reads Timer, of another value", [], agree, [],
         agree[:1] + [(cst, integer(cst, 4))] + agree[2:], {cst},
         ["aml slack agree evaluations=4 status_only=2 initialisation_lines=0"]),
        ("a _CST that reads Timer, of another status", [], agree, [],
         agree[:1] + [(cst, ["Evaluation of %s failed with status AE_AML_TIMEOUT" % cst])] + agree[2:], {cst},
         ["aml slack differ \\_PR_.CPU0._CST plain=AE_OK Integer 0x3 slack=AE_AML_TIMEOUT"]),
        ("a _PDC that returns nothing, then the last value", [], agree, [],
         agree[:2] + [(pdc, integer(pdc, 1))] + agree[3:], set(),
         ["aml slack agree evaluations=4 status_only=1 initialisation_lines=0"]),
        ("an _OSC of another Buffer", [], agree, [], agree[:3] + [(osc, buffer(osc, [0, 0, 0, 0, 0, 0, 0, 0]))], set(),
         ["aml slack differ \\_SB._OSC plain=AE_OK Buffer 00 00 00 00 01 00 00 00 "
          "slack=AE_OK Buffer 00 00 00 00 00 00 00 00"]),
        ("an _OSC that reads Timer, its path unpadded, of another Buffer", [], agree, [],
         agree[:3] + [(osc, buffer(osc, [0, 0, 0, 0, 0, 0, 0, 0]))], {"\\_SB_._OSC"},
         ["aml slack agree evaluations=4 status_only=2 initialisation_lines=0"]),
        ("an _INI that fails in the initialisation of one run only", [aborted, executed], agree, [executed], agree,
         set(), ["aml slack differ initialisation plain=%s slack=none" % aborted]),
        ("a run that stops before an evaluation", [], agree, [], agree[:1], set(),
         ["aml slack unevaluated \\_PR_.CPU0._CST"]),
    ]
    failures = 0
    for what, plain_init, plain_evaluations, slack_init, slack_evaluations, timer, expected in cases:
        plain = parse(made_up_output(plain_init, plain_evaluations, 4))
        slack = parse(made_up_output(slack_init, slack_evaluations, 22))
        found = compare(commands, plain, slack, timer)
        if found != expected:
            print("self-test: %s gives %s, expected %s" % (what, found, expected))
            failures += 1
    print("self-test: pass" if failures == 0 else "self-test: %d failure(s)" % failures)
    return failures


def needs_path_commands(machine):
    """The evaluations of the needs path a host can ask for, in the order of §3.8: the identity of the Devices that hold
    a PCI_Config region or lie above one, and of the processor Devices; every _STA; the handshake; the _STA of the
    processor objects with a _CST and of what lies above them; their _CST."""
    nodes, holders = machine.nodes, {}
    for path, (kind, value) in nodes.items():
        if kind == "Region" and value.startswith("[PCI_Config]"):
            holders.update((device, None) for device in acpiexec_idle.above(path)
                           if nodes.get(device, ("",))[0] == "Device")
    identity = ["evaluate %s.%s" % (device, name) for device in holders for name in PCI_IDENTITY
                if device + "." + name in nodes]
    statuses = ["evaluate %s._STA" % path for path, (kind, _) in nodes.items()
                if kind in acpiexec_idle.DEVICES and path + "._STA" in nodes]
    return (identity + machine.identity_commands() + statuses + machine.handshake_commands()
            + machine.presence_commands() + machine.idle_commands())


def main():
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(1 if self_test() else 0)
    if len(sys.argv) < 5:
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        sys.exit(2)
    with tempfile.TemporaryDirectory() as work:
        machine = acpiexec_idle.Machine(os.path.abspath(sys.argv[1]), acpiexec_idle.read_slots(sys.argv[2]),
                                        acpiexec_idle.read_presets(sys.argv[3]), sys.argv[4:], work)
        try:
            machine.identify()
        except acpiexec_idle.Stopped as stopped:
            print("aml slack unevaluated %s" % stopped)
            return
        commands = needs_path_commands(machine)
        plain, slack = (parse(acpiexec_idle.run(machine.bin_directory, work, machine.tables, commands, options))
                        for options in (acpiexec_idle.OPTIONS, acpiexec_idle.OPTIONS + SLACK))
        blocks = []
        for table in machine.tables:
            with open(os.path.join(work, table), "rb") as data:
                blocks.append((table, data.read()))
        print("\n".join(compare(commands, plain, slack, aml_census.methods_reading_timer(blocks))))


if __name__ == "__main__":
    main()
