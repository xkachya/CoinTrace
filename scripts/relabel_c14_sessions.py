#!/usr/bin/env python3
"""
relabel_c14_sessions.py
=======================
Uses 2026-04-11.Discovery.Session.C-14.txt as authoritative ground truth to
relabel coin_name / metal_code in C-14 NDJSON session files, then produces a
single clean unified output file.

Ground truth mapping is derived by cross-referencing:
  - "coin_name Side X : ..." annotations in the session log
  - "Dump #N → /CoinTrace/discovery/session_XXXX.ndjson" lines
  - mass_g values for sanity-check

Skipped records (not included in output):
  - session_f947a73a index 1  : aborted measurement (Unclassified, mid-session)
  - session_f947a73a index 6  : aborted measurement (Unclassified, mid-session)
  - session_f947a73a index 32 : Germany 3 Marks Side A @ 6.29g (coin unstable)

Known label errors in raw NDJSON (corrected by this script):
  - session_f01e404c index 19 : "American Silver Eagle 1oz (Side B)" → Kennedy Kennedy Side A
  - session_f01e404c index 22 : "USSR 10 Rubles Ag900 (Side A)" → Kennedy Side B
  - session_f01e404c index 24 : "USSR 10 Rubles Ag900 (Side A)" → Kennedy Side B
  - session_f01e404c index 45–49 : "Unclassified" → Germany 1 Pfennig Side A
  - session_f01e404c index 50–54 : "Unclassified" → Germany 1 Pfennig Side B
  - session_f947a73a many     : "Unclassified" → correct class per log context

Inputs:   docs/external/С-14/session_*.ndjson  (5 files, 142 raw records)
Output:   docs/external/С-14/2026-04-11.C14_unified.ndjson

Usage:
    cd D:\\GitHub\\CoinTrace
    python scripts/relabel_c14_sessions.py [--dry-run]
"""

import json
import sys
from collections import defaultdict
from pathlib import Path

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

REPO_ROOT = Path(__file__).resolve().parent.parent
C14_DIR   = REPO_ROOT / "docs" / "external" / "С-14"
OUTPUT    = C14_DIR / "2026-04-11.C14_unified.ndjson"

# Session files in chronological measurement order
SESSION_ORDER = [
    "session_06f50226.ndjson",
    "session_db906276.ndjson",
    "session_f01e404c.ndjson",
    "session_f947a73a.ndjson",
    "session_31a22af3.ndjson",
]

# ---------------------------------------------------------------------------
# Ground-truth label map
# Key  : (session_stem, record_index_within_file)
# Value: (coin_name, metal_code)
# Built from 2026-04-11.Discovery.Session.C-14.txt
# ---------------------------------------------------------------------------

def _build_label_map() -> dict[tuple[str, int], tuple[str, str]]:
    m: dict[tuple[str, int], tuple[str, str]] = {}

    def add(stem: str, indices, coin_name: str, metal_code: str):
        for i in indices:
            m[(stem, i)] = (coin_name, metal_code)

    # ---- session_06f50226  (30 records, indices 0-29) ----------------------
    add("session_06f50226", range(0,  5), "Bahamas $1 Ag800 (Side A)",          "XAG800")
    add("session_06f50226", range(5,  10),"Bahamas $1 Ag800 (Side B)",          "XAG800")
    add("session_06f50226", range(10, 15),"American Silver Eagle 1oz (Side A)", "XAG999")
    add("session_06f50226", range(15, 20),"American Silver Eagle 1oz (Side B)", "XAG999")
    add("session_06f50226", range(20, 25),"Australian Kangaroo 1oz (Side A)",   "XAG999")
    add("session_06f50226", range(25, 30),"Australian Kangaroo 1oz (Side B)",   "XAG999")

    # ---- session_db906276  (5 records, indices 0-4) ------------------------
    add("session_db906276", range(0,  5), "Olympic 1984 Ag900 (Side A)",        "XAG900")

    # ---- session_f01e404c  (55 records, indices 0-54) ----------------------
    add("session_f01e404c", range(0,  5), "Olympic 1984 Ag900 (Side B)",        "XAG900")
    add("session_f01e404c", range(5,  10),"USSR 10 Rubles Ag900 (Side A)",      "XUSSR10")
    add("session_f01e404c", range(10, 15),"USSR 10 Rubles Ag900 (Side B)",      "XUSSR10")
    add("session_f01e404c", range(15, 20),"Kennedy Half Dollar Ag400 (Side A)", "XKENNED")
    add("session_f01e404c", range(20, 25),"Kennedy Half Dollar Ag400 (Side B)", "XKENNED")
    add("session_f01e404c", range(25, 30),"Germany GDR 10 Marks 1990 (Side A)", "XCUZN")
    add("session_f01e404c", range(30, 35),"Germany GDR 10 Marks 1990 (Side B)", "XCUZN")
    add("session_f01e404c", range(35, 40),"Germany 1.5 Euro CuNi (Side A)",     "XFE")
    add("session_f01e404c", range(40, 45),"Germany 1.5 Euro CuNi (Side B)",     "XFE")
    add("session_f01e404c", range(45, 50),"Germany 1 Pfennig Steel-Cu (Side A)","XFECUP")
    add("session_f01e404c", range(50, 55),"Germany 1 Pfennig Steel-Cu (Side B)","XFECUP")

    # ---- session_f947a73a  (38 records, 0-37) ------------------------------
    # index 1  → aborted (Unclassified) → NOT added → will be skipped
    # index 6  → aborted (Unclassified) → NOT added → will be skipped
    # index 32 → Germany 3 Marks Side A, LDC1101 data valid (conf=0.84),
    #            NAU7802 mass bad (6.29g — coin moved during weighing).
    #            Corrected to median of other 4 Side A records = 2.08g.
    #            See MASS_CORRECTIONS map below.
    add("session_f947a73a", [0],         "Slovakia 2 Koruny Steel-Ni (Side A)", "XFENIP")
    add("session_f947a73a", range(2, 6), "Slovakia 2 Koruny Steel-Ni (Side A)", "XFENIP")
    add("session_f947a73a", range(7, 12),"Slovakia 2 Koruny Steel-Ni (Side B)", "XFENIP")
    add("session_f947a73a", range(12,17),"South Africa 10 Cents Ni (Side A)",   "XNICKEL")
    add("session_f947a73a", range(17,22),"South Africa 10 Cents Ni (Side B)",   "XNICKEL")
    add("session_f947a73a", range(22,27),"Russian Empire 5 Kopecks Cu (Side A)","XCU")
    add("session_f947a73a", range(27,32),"Russian Empire 5 Kopecks Cu (Side B)","XCU")
    add("session_f947a73a", [32],        "Germany 3 Marks 1922 Al (Side A)",    "XAL")
    add("session_f947a73a", range(33,37),"Germany 3 Marks 1922 Al (Side A)",    "XAL")
    add("session_f947a73a", [37],        "Germany 3 Marks 1922 Al (Side B)",    "XAL")

    # ---- session_31a22af3  (14 records, 0-13) ------------------------------
    add("session_31a22af3", range(0, 4), "Germany 3 Marks 1922 Al (Side B)",    "XAL")
    add("session_31a22af3", range(4, 9), "Ukraine 10 UAH 2022 ZnNi (Side A)",   "XZNNIP")
    add("session_31a22af3", range(9, 14),"Ukraine 10 UAH 2022 ZnNi (Side B)",   "XZNNIP")

    return m


LABEL_MAP = _build_label_map()

# ---------------------------------------------------------------------------
# Mass corrections for records where NAU7802 reading was bad but LDC1101
# data is valid. Corrected value = median of other records in the same class.
# Key: (session_stem, record_index)
# Value: corrected (mass_g, mass_n)
# ---------------------------------------------------------------------------

MASS_CORRECTIONS: dict[tuple[str, int], tuple[float, float]] = {
    # Germany 3 Marks 1922 Al Side A: coin moved during weighing → 6.29g wrong.
    # Median of other 4 Side A records: 2.08g; mass_n = 2.08 / 33.3 = 0.0625
    ("session_f947a73a", 32): (2.08, 0.0625),
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    dry_run = "--dry-run" in sys.argv

    counts: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    relabeled = 0
    skipped   = 0
    out_records: list[dict] = []

    for fname in SESSION_ORDER:
        fpath = C14_DIR / fname
        if not fpath.exists():
            print(f"  [WARN] Missing: {fpath}")
            continue

        stem = fpath.stem
        raw_lines = fpath.read_text(encoding="utf-8").splitlines()

        for line in raw_lines:
            line = line.strip()
            if not line:
                continue

            try:
                rec = json.loads(line)
            except json.JSONDecodeError as e:
                print(f"  [WARN] JSON parse error in {fname}: {e}")
                continue

            idx = rec.get("index")
            if idx is None:
                print(f"  [WARN] Record without 'index' in {fname} — skipped")
                skipped += 1
                continue

            key = (stem, idx)
            if key not in LABEL_MAP:
                # Aborted measurement, bad quality, or not part of C-14 protocol
                old_name = rec.get("coin_name", "?")
                old_mass = rec.get("mass_g", 0)
                skipped += 1
                print(f"  [SKIP] {fname}[{idx}]  was={old_name!r}  mass={old_mass:.2f}g")
                continue

            new_coin_name, new_metal_code = LABEL_MAP[key]
            old_coin_name  = rec.get("coin_name", "")
            old_metal_code = rec.get("metal_code", "")

            # Report corrections
            label_changed = (
                old_coin_name  != new_coin_name or
                old_metal_code != new_metal_code
            )
            if label_changed:
                relabeled += 1
                print(
                    f"  [FIX]  {fname}[{idx}]  "
                    f"\"{old_coin_name}\"/{old_metal_code}"
                    f" → \"{new_coin_name}\"/{new_metal_code}"
                    f"  mass={rec.get('mass_g', 0):.2f}g"
                )

            rec["coin_name"]  = new_coin_name
            rec["metal_code"] = new_metal_code
            rec["source_session"] = stem
            rec["source_index"]   = idx

            # Apply mass correction if needed
            if key in MASS_CORRECTIONS:
                corr_mass_g, corr_mass_n = MASS_CORRECTIONS[key]
                print(
                    f"  [CORR] {fname}[{idx}]  "
                    f"mass_g {rec.get('mass_g', 0):.2f}g → {corr_mass_g:.2f}g  "
                    f"(NAU7802 bad read, LDC1101 data valid)"
                )
                rec["mass_g"]   = corr_mass_g
                if "production_vector" in rec and "mass_n" in rec["production_vector"]:
                    rec["production_vector"]["mass_n"] = corr_mass_n

            counts[new_coin_name]["n"] += 1
            out_records.append(rec)

    # Re-index output records sequentially
    for i, rec in enumerate(out_records):
        rec["index"] = i

    # Write output
    if not dry_run:
        OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        with OUTPUT.open("w", encoding="utf-8") as f:
            for rec in out_records:
                f.write(json.dumps(rec, ensure_ascii=False) + "\n")

    # Summary
    total_out = len(out_records)
    print()
    print("=" * 68)
    print(f"  Records written : {total_out}  (skipped {skipped}, relabeled {relabeled})")
    print(f"  Output          : {OUTPUT.relative_to(REPO_ROOT)}")
    if dry_run:
        print("  *** DRY-RUN — file NOT written ***")
    print()
    print("  Class counts (coin_name → n):")
    for label in sorted(counts):
        n = counts[label]["n"]
        flag = "  ✓" if n >= 5 else f"  ⚠ need {5 - n} more"
        print(f"    {n:2d}  {label}{flag}")
    print("=" * 68)


if __name__ == "__main__":
    main()
