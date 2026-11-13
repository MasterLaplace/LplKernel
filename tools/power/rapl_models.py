"""Checks the Intel model table of kernel/kernel/power/energy_counter.c against Linux's, apart from the kernel's code.

Usage: python3 -I tools/power/rapl_models.py <intel_rapl_common.c> <intel-family.h> <energy_counter.c>

The first two files are Linux v6.12's drivers/powercap/intel_rapl_common.c and arch/x86/include/asm/intel-family.h, as
git.kernel.org serves them (`/plain/<path>?h=v6.12`). Every family-6 model of `rapl_ids` must be a row of the kernel's
table and no other, and a row must count in Atom units exactly where its `rapl_defaults_*` checks its unit with
`rapl_check_unit_atom`. Prints each difference, then `match` and exits 0, or `differ` and exits 1.
"""
import re
import sys


def linux_models(rapl, family):
    """The family-6 models of `rapl_ids`, each mapped to whether its units are an Atom's."""
    ids = {name: int(model, 16) for name, model in
           re.findall(r"#define\s+(INTEL_\w+)\s+IFM\(\s*6\s*,\s*(0x[0-9A-Fa-f]+)\s*\)", family)}
    atom = {name for name, body in re.findall(r"static const struct rapl_defaults (\w+) = \{(.*?)\};", rapl, re.S)
            if "rapl_check_unit_atom" in body}
    start = rapl.index("rapl_ids[] __initconst")
    table = rapl[start:rapl.index("};", start)]
    models = {}
    for name, defaults in re.findall(r"X86_MATCH_VFM\((INTEL_\w+),\s*&(\w+)\)", table):
        if name not in ids:
            sys.exit("rapl_models: %s is in rapl_ids but not in intel-family.h" % name)
        models[ids[name]] = defaults in atom
    return models


def kernel_models(source):
    """The rows of energy_counter.c's table, each mapped to its Atom flag."""
    return {int(model, 16): flag == "true"
            for model, flag in re.findall(r"\{(0x[0-9A-Fa-f]+)u,\s*(true|false)\s*\}", source)}


def main():
    if sys.argv[1:] in (["--help"], ["-h"]):
        print(__doc__)
        return
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    with open(sys.argv[1], encoding="utf-8") as rapl, open(sys.argv[2], encoding="utf-8") as family, \
            open(sys.argv[3], encoding="utf-8") as source:
        linux, kernel = linux_models(rapl.read(), family.read()), kernel_models(source.read())
    if not linux or not kernel:
        sys.exit("rapl_models: no model read from %s" % ("Linux" if not linux else "the kernel"))
    for model in sorted(set(linux) | set(kernel)):
        if model not in kernel:
            print("only in Linux: 0x%02X" % model)
        elif model not in linux:
            print("only in the kernel: 0x%02X" % model)
        elif linux[model] != kernel[model]:
            print("units differ: 0x%02X, Linux %s, kernel %s" % (model, linux[model], kernel[model]))
    same = linux == kernel
    print("%s: %d models in Linux, %d in the kernel" % ("match" if same else "differ", len(linux), len(kernel)))
    sys.exit(0 if same else 1)


if __name__ == "__main__":
    main()
