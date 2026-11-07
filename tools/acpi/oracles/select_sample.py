"""Deterministic sample of linuxhw/ACPI reports for the AML census.

Usage: python3 -I select_sample.py <tree.txt> > sample.txt

Rule, reproducible from the tree listing alone:
  * every report of the types Server, Mini Pc, All In One and Tablet that has a .bin;
  * for Desktop, Notebook and Convertible, a quota per vendor, picked at evenly
    spaced positions of that vendor's reports sorted by path;
  * plus the reports whose model names a platform shipped in 2022 or later
    (NEWEST below), so that the newest firmware is in the sample.
"""
import sys

ALL_TYPES = ("Server", "Mini Pc", "All In One", "Tablet")

QUOTAS = {
    "Desktop": {
        "ASUSTek Computer": 2, "Gigabyte Technology": 2, "MSI": 2, "ASRock": 2,
        "Dell": 1, "Hewlett-Packard": 1, "Lenovo": 1, "Intel": 1,
        "Supermicro": 1, "Google": 1,
    },
    "Notebook": {
        "Lenovo": 2, "Dell": 2, "Hewlett-Packard": 2, "ASUSTek Computer": 2,
        "Acer": 2, "MSI": 1, "Apple": 1, "Toshiba": 1, "HUAWEI": 1,
        "Framework": 2, "Valve": 1, "Samsung Electronics": 1, "Razer": 1,
        "System76": 1, "Google": 1,
    },
    "Convertible": {
        "Lenovo": 1, "Hewlett-Packard": 1, "Dell": 1,
    },
}

NEWEST = (
    "Desktop/ASUSTek Computer/ROG/ROG Maximus Z790 APEX ENCORE/",
    "Desktop/ASRock/X600/X600M-STX/",
    "Desktop/MSI/MS-7/MS-7E16/",
    "Desktop/Gigabyte Technology/Z790/Z790 AORUS PRO X/",
    "Notebook/ASUSTek Computer/ASUS/ASUS Zenbook S 16 UM5606WA_UM5606WA/",
    "Notebook/MSI/Prestige/Prestige 13 AI+ Evo A2VMG/",
    "Notebook/Dell/Precision/Precision 5680/",
    "Notebook/Lenovo/ThinkPad/ThinkPad X1 Carbon Gen 11 21HNSAD001/",
    "Convertible/ASUSTek Computer/ROG/ROG Flow X16 GV601VV_GV601VV/",
    "Convertible/Lenovo/ThinkPad/ThinkPad X1 Yoga Gen 8 21HRS2KF00/",
)


def evenly_spaced(items, count):
    if count >= len(items):
        return list(items)
    return [items[int((index + 0.5) * len(items) / count)] for index in range(count)]


def main():
    with open(sys.argv[1], encoding="utf-8") as listing:
        bins = sorted(line.rstrip("\n") for line in listing if line.rstrip("\n").endswith(".bin"))
    chosen = []
    for path in bins:
        if path.split("/")[0] in ALL_TYPES:
            chosen.append(path)
    for kind, quotas in QUOTAS.items():
        for vendor, count in quotas.items():
            pool = [path for path in bins if path.startswith(f"{kind}/{vendor}/")]
            chosen.extend(evenly_spaced(pool, count))
    for prefix in NEWEST:
        matches = [path for path in bins if path.startswith(prefix)]
        chosen.extend(matches[:1])
    seen = set()
    for path in chosen:
        if path not in seen:
            seen.add(path)
            print(path)


if __name__ == "__main__":
    main()
