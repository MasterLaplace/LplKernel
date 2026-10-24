"""The namespace ACPICA's acpiexec loads from a set of tables, as the `aml node` lines `test-acpi --namespace` prints.

Usage: python3 -I tools/acpi/acpiexec_namespace.py <acpica bin directory> <acpidump text file>
       python3 -I tools/acpi/acpiexec_namespace.py <acpica bin directory> --tables <DSDT> [<SSDT>...]

An acpidump file is split with acpixtract; its DSDT, then its SSDTs in the dump's order, go to `acpiexec -l -di`, which
loads them without running _INI, and its `namespace` command prints every node. Each line becomes
`aml node <path> <type> <value>`: the addresses ACPICA prints (Aml, Object, Notify Object, an alias's target node) are
dropped, and so are the nodes ACPICA creates for an External with no object, and its own `\\_TI_` test scope.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

LINE = re.compile(r"^\s*(\d+)\s+(\S{4})\s+(\S+)\s+(?:0x[0-9a-f]+|\(nil\))\s+[0-9A-F]{3}\s?(.*)$")


def tables_of_dump(bin_directory, dump, work):
    """The DSDT, then the SSDTs in the dump's order, that acpixtract writes from an acpidump text file."""
    subprocess.run([os.path.join(bin_directory, "acpixtract"), "-a", os.path.abspath(dump)], cwd=work,
                   capture_output=True, check=False)
    ssdts = sorted((name for name in os.listdir(work) if re.fullmatch(r"ssdt\d*\.dat", name)),
                   key=lambda name: int(name[4:-4] or 0))
    return (["dsdt.dat"] if os.path.exists(os.path.join(work, "dsdt.dat")) else []) + ssdts


def namespace(bin_directory, tables, work):
    """acpiexec's namespace dump of tables, as normalised lines."""
    output = subprocess.run([os.path.join(bin_directory, "acpiexec"), "-l", "-di", "-b", "namespace"] + tables,
                            cwd=work, capture_output=True, text=True, errors="replace", timeout=600).stdout
    lines, path, started, skipped_depth = [], [], False, None
    for line in output.splitlines():
        if line.startswith("ACPI Namespace (from Namespace Root)"):
            started = True
            continue
        match = LINE.match(line) if started else None
        if match is None:
            continue
        depth, name, kind, value = int(match.group(1)), match.group(2), match.group(3), match.group(4).strip()
        del path[depth:]
        path.append(name)
        if skipped_depth is not None and depth > skipped_depth:
            continue
        skipped_depth = depth if (depth == 0 and name == "_TI_") or value == "<No attached object>" else None
        if skipped_depth is not None:
            continue
        value = re.sub(r"\s*Aml (0x[0-9a-f]+|\(nil\))$", "", value)
        value = re.sub(r"^(Notify Object: |Object )(0x[0-9a-f]+|\(nil\))$", "", value)
        value = re.sub(r"\s*\((0x[0-9a-f]+|\(nil\))\)$", "", value)
        lines.append(("aml node \\%s %s %s" % (".".join(path), kind, value)).rstrip())
    return lines


def main():
    bin_directory = os.path.abspath(sys.argv[1])
    work = tempfile.mkdtemp()
    try:
        if sys.argv[2] == "--tables":
            tables = []
            for index, table in enumerate(sys.argv[3:]):
                tables.append("table%u.dat" % index)
                shutil.copyfile(table, os.path.join(work, tables[-1]))
        else:
            tables = tables_of_dump(bin_directory, sys.argv[2], work)
        print("\n".join(namespace(bin_directory, tables, work)))
    finally:
        shutil.rmtree(work)


if __name__ == "__main__":
    main()
