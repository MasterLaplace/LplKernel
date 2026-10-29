"""Compares the namespace `test-acpi --namespace` prints with acpiexec's, normalised by acpiexec_namespace.py.

Usage: python3 -I tools/acpi/compare_namespace.py [--paths] <kernel lines> <acpiexec lines>

Both files hold `aml node <path> <type> <value>` lines. A node the kernel prints `Untyped`, or a region it prints
`[Address/Length not yet evaluated]`, waits for the interpreter: an object built from an expression. Such a node
agrees with whatever acpiexec built, and is counted apart. With --paths, only the sets of paths are compared: a
namespace the initialisation ran over holds the values it stored. Prints `agree <nodes> <waiting>`, or one `differ`
line per node, then exits 1.
"""
import sys

WAITING = "[Address/Length not yet evaluated]"


def nodes(path):
    """Each node of a file, by path: its type and value."""
    found = {}
    with open(path, encoding="latin-1") as lines:
        for line in lines:
            if line.startswith("aml node "):
                fields = line.rstrip("\n").split(" ", 3)
                found[fields[2]] = fields[3]
    return found


def main():
    paths_only = sys.argv[1] == "--paths"
    ours, theirs = nodes(sys.argv[1 + paths_only]), nodes(sys.argv[2 + paths_only])
    waiting, differences = 0, []
    for path in sorted(set(ours) | set(theirs)):
        mine, other = ours.get(path), theirs.get(path)
        if paths_only and mine is not None and other is not None:
            continue
        if mine is not None and other is not None and (mine == "Untyped" or
                                                        (mine.endswith(WAITING) and other.startswith("Region "))):
            waiting += 1
        elif mine != other:
            differences.append("differ %s kernel=%s acpiexec=%s" % (path, mine, other))
    if differences:
        print("\n".join(differences))
        sys.exit(1)
    print("agree %u %u" % (len(ours), waiting))


if __name__ == "__main__":
    main()
