"""Runs uACPI's test cases (specification §8.5) through the host harness and compares each result with its `// Expect:` line.

Usage: python3 -I tools/acpi/uacpi_cases.py <iasl> <test-acpi> <test-cases directory> <divergences list>

Each case is compiled as uACPI compiles it: `iasl -oa`, with the warnings its runner ignores. `test-acpi --evaluate \\MAIN`
loads it and evaluates `\\MAIN`. A case is
  ok          when the status is ok and the result has the expected type and value;
  divergence  when it is listed in the divergences list with the section of the specification that differs, and fails;
  skip        when its expectation, `check-*`, tests uACPI's own API;
  waits       when it stops on what this kernel does not run yet, named on the line: an opcode it decodes but does not
              execute, or a field access, that the evaluation or a statement of the table's code met, or the issue the
              divergences list names for it, and fails;
  not ok      otherwise, a listed divergence that passes included, so that the list stays true.
Prints one line per case, then the counts; exits 1 when a case is not ok.
"""
import os
import re
import subprocess
import sys
import tempfile

IGNORED_WARNINGS = [3144, 2098, 6058, 6161, 6005, 6080, 6084, 6086, 2089, 2173, 6117, 3124, 3175, 3141, 6141, 6031,
                    6074, 6085, 3168, 2067]
EXPECT = re.compile(r"^// Expect: (int|str) => (.*)$")
WAITING = ("unimplemented_operator", "field_access_deferred")


def divergences(path):
    """Each listed case, by name, with the reason it diverges."""
    listed = {}
    with open(path, encoding="utf-8") as lines:
        for line in lines:
            line = line.strip()
            if line and not line.startswith("#"):
                name, _, reason = line.partition(" ")
                listed[name] = reason.strip()
    return listed


def expectation(path):
    """The type and value a case's `// Expect:` line gives."""
    with open(path, encoding="utf-8", errors="replace") as lines:
        for line in lines:
            match = EXPECT.match(line.rstrip("\n"))
            if match:
                return match.group(1), match.group(2)
    return None, None


def evaluate(iasl, harness, case, work):
    """Compiles a case and evaluates its \\MAIN: the harness's status, result type and value, and what waits."""
    aml = os.path.join(work, "case")
    flags = [argument for warning in IGNORED_WARNINGS for argument in ("-vw", str(warning))]
    compiled = subprocess.run([iasl, *flags, "-oa", "-p", aml, case], capture_output=True, text=True, timeout=60)
    if compiled.returncode != 0 or not os.path.exists(aml + ".aml"):
        return None
    output = subprocess.run([harness, "--evaluate", "\\MAIN", aml + ".aml"], capture_output=True, text=True,
                            errors="replace", timeout=120).stdout
    result = {"status": None, "type": None, "value": None, "unimplemented": [], "failed": []}
    for line in output.splitlines():
        if line.startswith("aml evaluate status="):
            result["status"] = line.split("=", 1)[1]
        elif line.startswith("aml evaluate type="):
            fields = line[len("aml evaluate "):].split(" ", 1)
            result["type"] = fields[0].split("=", 1)[1]
            result["value"] = fields[1].split("=", 1)[1] if len(fields) > 1 else ""
        elif line.startswith("aml evaluate unimplemented "):
            result["unimplemented"].append(line.rsplit(" ", 1)[1])
        elif line.startswith("aml evaluate failed "):
            result["failed"].append(line.split(" ")[3])
    os.remove(aml + ".aml")
    return result


def waiting(result):
    """What the case met that the kernel does not run yet: waiting statuses, then unimplemented opcodes."""
    statuses = [status for status in [result["status"], *result["failed"]] if status in WAITING]
    return sorted(set(statuses)) + result["unimplemented"]


def matches(kind, expected, result):
    """Whether a result has the expected type and value."""
    if result["status"] != "ok" or result["type"] != kind:
        return False
    if kind == "str":
        return result["value"] == expected
    return int(result["value"]) == int(expected, 0)


def main():
    iasl, harness, cases, listed_path = sys.argv[1:5]
    listed = divergences(listed_path)
    counts = {"ok": 0, "divergence": 0, "skip": 0, "waits": 0, "not ok": 0}
    names = sorted(name for name in os.listdir(cases) if name.endswith(".asl"))
    work = tempfile.mkdtemp()
    for name in names:
        case = name[:-4]
        kind, expected = expectation(os.path.join(cases, name))
        if kind is None or expected.startswith("check-"):
            print("skip %s: %s tests uACPI's own API" % (case, expected))
            counts["skip"] += 1
            continue
        result = evaluate(iasl, harness, os.path.join(cases, name), work)
        if result is None:
            print("not ok %s: iasl refuses it" % case)
            counts["not ok"] += 1
        elif matches(kind, expected, result):
            if case in listed:
                print("not ok %s: listed as a divergence, and passes" % case)
                counts["not ok"] += 1
            else:
                print("ok %s" % case)
                counts["ok"] += 1
        elif case in listed and listed[case].startswith("#"):
            print("waits %s: %s" % (case, listed[case]))
            counts["waits"] += 1
        elif case in listed:
            print("divergence %s: %s" % (case, listed[case]))
            counts["divergence"] += 1
        elif waiting(result):
            print("waits %s: %s" % (case, " ".join(waiting(result))))
            counts["waits"] += 1
        else:
            print("not ok %s: status %s, %s %s instead of %s %s" % (case, result["status"], result["type"],
                                                                   result["value"], kind, expected))
            counts["not ok"] += 1
    print("# uacpi: %u ok, %u divergences, %u skipped, %u wait for what the kernel does not run yet, %u not ok, of %u"
          % (counts["ok"], counts["divergence"], counts["skip"], counts["waits"], counts["not ok"], len(names)))
    sys.exit(1 if counts["not ok"] or not names else 0)


if __name__ == "__main__":
    main()
