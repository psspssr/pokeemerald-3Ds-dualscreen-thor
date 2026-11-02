#!/usr/bin/env python3
"""How much of Hoenn is voxelized, read off docs/VOXEL_STATUS.md.

    python 3ds_port/scripts/voxel_status.py            # print the progress
    python 3ds_port/scripts/voxel_status.py --places   # and every place's
    python 3ds_port/scripts/voxel_status.py --write    # refresh the doc's Progress block
    python 3ds_port/scripts/voxel_status.py --check    # fail if that block is out of date

Every Buildings and Terrain cell of the tables is one item (a `—` cell is
not counted: nothing to do there). Each item scores by its mark:

    ⬜ 0    🟦 0.5    🟨 0.6    🟧 0.75    ✅ 1

(🟨 is modelled but not reviewed, so below 🟧, whose faults are known.)

"Progress" is the mean score; "complete" is the share of items at ✅. Rows
count as they are listed, so a layout shared by N maps counts N times, as
the player meets it N times; --layouts counts each layout once instead.

The block --write fills sits between the markers

    <!-- voxel-progress:start -->
    <!-- voxel-progress:end -->

and is the only part of the doc this script ever changes. Run it after
editing the tables, in the same commit.
"""

from __future__ import annotations

import argparse
import sys
from collections import OrderedDict
from pathlib import Path

SCORES = OrderedDict([("⬜", 0.0), ("🟦", 0.5), ("🟨", 0.6), ("🟧", 0.75), ("✅", 1.0)])
NONE = "—"
START, END = "<!-- voxel-progress:start -->", "<!-- voxel-progress:end -->"


def find_doc() -> Path:
    root = Path(__file__).resolve().parents[2]
    for rel in ("docs/VOXEL_STATUS.md", "public/docs/VOXEL_STATUS.md"):
        if (root / rel).exists():
            return root / rel
    sys.exit("voxel_status: docs/VOXEL_STATUS.md not found; pass its path")


def parse(text: str, path: Path):
    """[(section, place, area, layout, column, mark, indoor)], one per counted cell;
    indoor: a row without terrain (Terrain `—`), a room."""
    items, section, place, header = [], None, None, None
    for n, line in enumerate(text.splitlines(), 1):
        if line.startswith("## "):
            section, place, header = line[3:].strip(), None, None
        elif line.startswith("### "):
            place, header = line[4:].strip(), None
        elif line.startswith("|"):
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if cells[0] == "Area" and {"Layout", "Buildings", "Terrain"} <= set(cells):
                header = cells
                continue
            if cells[0] in ("Area", "Column", "Mark", ""):
                header = None       # the legends' and the Progress block's tables
                continue
            if header is None or set(cells[0]) <= set("-"):
                continue
            if len(cells) != len(header):
                sys.exit("%s:%d: %d cells, the header has %d" % (path, n, len(cells), len(header)))
            row = dict(zip(header, cells))
            layout = row["Layout"].split("`")[1] if "`" in row["Layout"] else row["Layout"]
            for column in ("Buildings", "Terrain"):
                mark = row[column]
                if mark == NONE:
                    continue
                if mark not in SCORES:
                    sys.exit("%s:%d: %s is %r, not one of %s or %s"
                             % (path, n, column, mark, " ".join(SCORES), NONE))
                items.append((section, place or section, row["Area"], layout, column, mark,
                              row["Terrain"] == NONE))
    if not items:
        sys.exit("%s: no table rows found" % path)
    return items


def unique_layouts(items):
    seen, out = set(), []
    for it in items:
        key = (it[3], it[4])
        if key not in seen:
            seen.add(key)
            out.append(it)
    return out


def summary(items):
    counts = OrderedDict((m, 0) for m in SCORES)
    for it in items:
        counts[it[5]] += 1
    total = len(items)
    progress = sum(SCORES[m] * c for m, c in counts.items()) / total if total else 0.0
    return total, progress, counts["✅"] / total if total else 0.0, counts


def pct(x: float) -> str:
    return "%.1f %%" % (100 * x)


def table(groups) -> list[str]:
    head = "| | Items | Progress | Complete | " + " | ".join(SCORES) + " |"
    lines = [head, "|---|---:|---:|---:|" + "---:|" * len(SCORES)]
    for name, items in groups:
        total, progress, complete, counts = summary(items)
        lines.append("| %s | %d | %s | %s | %s |" % (name, total, pct(progress), pct(complete),
                                                    " | ".join(str(c) for c in counts.values())))
    return lines


def report(items, places: bool) -> list[str]:
    bold = lambda name: "**%s**" % name
    groups = [(bold("All of Hoenn"), items)]
    groups.append(("Outdoors and caves", [i for i in items if not i[6]]))
    groups.append(("Interiors", [i for i in items if i[6]]))
    groups.append(("Buildings and rooms", [i for i in items if i[4] == "Buildings"]))
    groups.append(("Terrain", [i for i in items if i[4] == "Terrain"]))
    for section in OrderedDict.fromkeys(i[0] for i in items):
        groups.append((section, [i for i in items if i[0] == section]))
    lines = table(groups)
    if places:
        lines += [""] + table([(place, [i for i in items if i[1] == place])
                               for place in OrderedDict.fromkeys(i[1] for i in items)])
    return lines


def block(lines) -> str:
    weights = ", ".join("%s %g" % (m, s) for m, s in SCORES.items())
    return "\n".join([START, "",
                      "Generated by `python 3ds_port/scripts/voxel_status.py --write` from the "
                      "tables below; do not edit by hand. Each Buildings or Terrain cell is one "
                      "item, scored %s; Complete = items at ✅." % weights,
                      ""] + lines + ["", END])


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("doc", nargs="?", type=Path, help="the status doc (default: found from the repo)")
    ap.add_argument("--places", action="store_true", help="also one line per town, route or area")
    ap.add_argument("--layouts", action="store_true", help="count each layout once")
    ap.add_argument("--write", action="store_true", help="refresh the Progress block in the doc")
    ap.add_argument("--check", action="store_true", help="exit 1 if the Progress block is out of date")
    args = ap.parse_args()
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")      # the marks, on a Windows console
        sys.stderr.reconfigure(encoding="utf-8")

    path = args.doc or find_doc()
    text = path.read_text(encoding="utf-8")
    items = parse(text, path)
    if args.layouts:
        items = unique_layouts(items)

    if args.write or args.check:
        if START not in text or END not in text:
            sys.exit("%s: no %s ... %s block" % (path, START, END))
        before, rest = text.split(START, 1)
        after = rest.split(END, 1)[1]
        new = before + block(report(parse(text, path), False)) + after
        if args.check:
            if new != text:
                print("%s: the Progress block is out of date; run voxel_status.py --write" % path)
                return 1
            print("%s: Progress block up to date" % path)
            return 0
        if new != text:
            path.write_text(new, encoding="utf-8", newline="\n")
            print("%s: Progress block updated" % path)
        else:
            print("%s: Progress block already up to date" % path)

    print("\n".join(report(items, args.places)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
