"""How far the residencies a rule derives from a state's exit latency fall from those Linux's intel_idle tunes by hand,
state by state: the measure that chose the rule the layer applies to _CST
(`advanced_configuration_and_power_interface_processor_idle_costs`).

Usage: python3 -I tools/acpi/oracles/against_intel_idle_tables.py <intel_idle.c>
       python3 -I tools/acpi/oracles/against_intel_idle_tables.py --self-test

<intel_idle.c> is drivers/idle/intel_idle.c of a Linux tree, for example v6.12's:

    gh api -H 'Accept: application/vnd.github.raw' \\
        '/repos/torvalds/linux/contents/drivers/idle/intel_idle.c?ref=v6.12' > intel_idle.c

Each state of its `struct cpuidle_state` tables gives an MWAIT hint, an exit latency and a target residency; a state
whose hint's C-state (bits 7:4) is 0 is of type C1, as the layer reads a _CST state's type. For each rule, the median of
|log2(rule / tuned)| over the states of non-zero latency, and the states the rule puts under half or over twice the
tuned residency:

  - `intel_idle on _CST`: the latency for C1, three times it otherwise (`intel_idle_init_cstates_acpi`);
  - `acpi_idle`: twice the latency (`latency_factor`, drivers/acpi/processor_idle.c).

Exits 2 on a usage error or when fewer than 50 states are read, which says the tables' form changed.

--self-test checks the reading and the measure on a made-up table, and exits 1 when one differs.
"""
import math
import re
import statistics
import sys

STATE = re.compile(r'\{\s*\.name\s*=\s*"([^"]+)".*?\.flags\s*=\s*MWAIT2flg\((0x[0-9a-fA-F]+)\)[^,]*,'
                   r'\s*\.exit_latency\s*=\s*(\d+),\s*\.target_residency\s*=\s*(\d+)', re.S)
TABLE = re.compile(r'static struct cpuidle_state (\w+)\[\] __initdata = \{(.*?)\n\};', re.S)
FLOOR = 50
RULES = (
    ("intel_idle on _CST", lambda c1, latency: latency if c1 else 3 * latency),
    ("acpi_idle", lambda c1, latency: 2 * latency),
)


def states(text):
    """(table, name, hint, exit latency, target residency) of each state of the tables in `text`."""
    for table, body in TABLE.findall(text):
        for name, hint, latency, residency in STATE.findall(body):
            yield table, name, int(hint, 16), int(latency), int(residency)


def measure(rows, rule):
    """The median |log2 error| of `rule` over `rows`, and the counts under half and over twice the tuned value."""
    errors = [math.log2(rule((hint >> 4) == 0, latency) / residency) for _, _, hint, latency, residency in rows]
    return statistics.median(abs(e) for e in errors), sum(e < -1 for e in errors), sum(e > 1 for e in errors)


def report(rows):
    """The lines the measure prints for `rows`."""
    lines = ["%u states of non-zero latency, %u tables" % (len(rows), len({row[0] for row in rows}))]
    for label, rule in RULES:
        median, under, over = measure(rows, rule)
        lines.append("%s: median |log2 error| %.2f, under half %u, over twice %u, of %u"
                     % (label, median, under, over, len(rows)))
    return lines


def self_test():
    """Checks states() and report() on a made-up table; returns the count of cases that differ."""
    text = ("static struct cpuidle_state made_up_cstates[] __initdata = {\n"
            "\t{\n\t\t.name = \"C1\",\n\t\t.desc = \"MWAIT 0x00\",\n\t\t.flags = MWAIT2flg(0x00),\n"
            "\t\t.exit_latency = 2,\n\t\t.target_residency = 1,\n\t\t.enter = &intel_idle, },\n"
            "\t{\n\t\t.name = \"C1E\",\n\t\t.desc = \"MWAIT 0x01\",\n\t\t.flags = MWAIT2flg(0x01),\n"
            "\t\t.exit_latency = 10,\n\t\t.target_residency = 20,\n\t\t.enter = &intel_idle, },\n"
            "\t{\n\t\t.name = \"C6\",\n\t\t.desc = \"MWAIT 0x20\",\n"
            "\t\t.flags = MWAIT2flg(0x20) | CPUIDLE_FLAG_TLB_FLUSHED,\n"
            "\t\t.exit_latency = 100,\n\t\t.target_residency = 401,\n\t\t.enter = &intel_idle, },\n"
            "\t{\n\t\t.enter = NULL }\n};\n")
    expected = ["3 states of non-zero latency, 1 tables",
                "intel_idle on _CST: median |log2 error| 1.00, under half 0, over twice 0, of 3",
                "acpi_idle: median |log2 error| 1.00, under half 1, over twice 1, of 3"]
    got = report([row for row in states(text) if row[3] > 0])
    failures = 0 if got == expected else 1
    if failures:
        print("self-test: the made-up table gives %s, expected %s" % (got, expected))
    print("self-test: pass" if failures == 0 else "self-test: 1 failure")
    return failures


def main():
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(1 if self_test() else 0)
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    with open(sys.argv[1]) as source:
        rows = [row for row in states(source.read()) if row[3] > 0]
    if len(rows) < FLOOR:
        print("only %u states read from %s: the tables' form changed" % (len(rows), sys.argv[1]), file=sys.stderr)
        sys.exit(2)
    for line in report(rows):
        print(line)


if __name__ == "__main__":
    main()
