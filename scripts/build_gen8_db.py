#!/usr/bin/env python3
"""
CoinTrace gen-8 database builder
==================================
Inputs:  data/sd_seed/CoinTrace/database/index.json          (gen-7, 6D centroids, base)
         docs/external/С-14/2026-04-11.C14_unified.ndjson    (C-14 session, p4_ protocol, 7D)
Output:  data/sd_seed/CoinTrace/database/index.json          (gen-8, 7D centroids)

Strategy vs gen-7
-----------------
gen-7 was built from C-8/C-10/C-11/C-12 sessions (protocol p3_, no mass_g).
gen-8 is built FRESH from C-14 (protocol p4_MIKROE3240_b06_mass):
  - All 13 classes measured with STEP_WEIGHT → STEP_FULL workflow
  - Each record has mass_g at top level + production_vector.mass_n
  - 7D centroid = [dRp1_n, k1, k2, df_n, dL1_n, df1_n, mass_n]
  - 7D radius_95pct computed from per-record distances in 7D space

C-14 coin_name → gen-8 group key mapping is defined in C14_GROUP_KEYS below.
coin_name strings MUST EXACTLY match what the firmware writes to NDJSON
(set via session tool before each measurement group — see SESSION_PROTOCOL.md).

Changes vs gen-7
----------------
- version: 7 → 8
- generation: 7 → 8
- session_id: "c10_c11_c12" → "c14"
- protocols: ["p3_..."] → ["p4_..."]
- centroid: 6D → 7D (adds mass_n key)
- radius_95pct: 6D Euclidean → 7D Euclidean
- id format: "class/c14_hw_YYYY-MM-DD"
- records_count reflects C-14 measurements only

TODO before running:
  1. Complete C-14 hardware session
  2. Copy all SD NDJSON files to docs/external/С-14/
  3. Merge into unified NDJSON: docs/external/С-14/2026-04-11.C14_unified.ndjson
     (Run this script to see which coin_names are found vs expected.)
  4. Remove the sys.exit(1) guard at the bottom of this file.
"""

from __future__ import annotations

import json
import math
import statistics
import sys
from datetime import datetime, timezone
from pathlib import Path

# Fix Windows console encoding for Unicode output
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

# ── Paths ─────────────────────────────────────────────────────────────────────
REPO_ROOT   = Path(__file__).resolve().parent.parent
C14_NDJSON  = REPO_ROOT / "docs" / "external" / "С-14" / "2026-04-11.C14_unified.ndjson"
OUTPUT      = REPO_ROOT / "data" / "sd_seed" / "CoinTrace" / "database" / "index.json"

PROTOCOL_ID     = "p4_MIKROE3240_b06_mass"
DRIFT_THRESHOLD = 0.05   # 5%
MASS_REF_G      = 33.3   # NAU7802Plugin::MASS_REF_G — XUSSR10 as reference coin

# ── Coin name → (entry_id, metal_code, coin_name_canonical) ──────────────────
# Keys  = exact coin_name strings written by firmware during C-14 session.
# Values = (group_key, metal_code)
# See SESSION_PROTOCOL.md for the full operator guide.
C14_GROUP_KEYS: dict[str, tuple[str, str]] = {
    # XAG800
    "Bahamas $1 Ag800 (Side A)":                ("xag800_bhs_a",    "XAG800"),
    "Bahamas $1 Ag800 (Side B)":                ("xag800_bhs_b",    "XAG800"),
    # XAG999 — Eagle
    "American Silver Eagle 1oz (Side A)":       ("xag999_eagle_a",  "XAG999"),
    "American Silver Eagle 1oz (Side B)":       ("xag999_eagle_b",  "XAG999"),
    # XAG999 — Kangaroo (optional; if not measured, Eagle covers mass_n for XAG999)
    "Australian Kangaroo 1oz (Side A)":         ("xag999_kang_a",   "XAG999"),
    "Australian Kangaroo 1oz (Side B)":         ("xag999_kang_b",   "XAG999"),
    # XAG900
    "Olympic 1984 Ag900 (Side A)":              ("xag900_a",        "XAG900"),
    "Olympic 1984 Ag900 (Side B)":              ("xag900_b",        "XAG900"),
    # XUSSR10 (reference coin)
    "USSR 10 Rubles Ag900 (Side A)":            ("xussr10_a",       "XUSSR10"),
    "USSR 10 Rubles Ag900 (Side B)":            ("xussr10_b",       "XUSSR10"),
    # XKENNED
    "Kennedy Half Dollar Ag400 (Side A)":       ("xkenned_a",       "XKENNED"),
    "Kennedy Half Dollar Ag400 (Side B)":       ("xkenned_b",       "XKENNED"),
    # XCUZN
    "Germany GDR 10 Marks 1990 (Side A)":       ("xcuzn_a",         "XCUZN"),
    "Germany GDR 10 Marks 1990 (Side B)":       ("xcuzn_b",         "XCUZN"),
    # XFE
    "Germany 1.5 Euro CuNi (Side A)":           ("xfe_a",           "XFE"),
    "Germany 1.5 Euro CuNi (Side B)":           ("xfe_b",           "XFE"),
    # XFECUP
    "Germany 1 Pfennig Steel-Cu (Side A)":      ("xfecup_a",        "XFECUP"),
    "Germany 1 Pfennig Steel-Cu (Side B)":      ("xfecup_b",        "XFECUP"),
    # XFENIP
    "Slovakia 2 Koruny Steel-Ni (Side A)":      ("xfenip_a",        "XFENIP"),
    "Slovakia 2 Koruny Steel-Ni (Side B)":      ("xfenip_b",        "XFENIP"),
    # XNICKEL
    "South Africa 10 Cents Ni (Side A)":        ("xnickel_a",       "XNICKEL"),
    "South Africa 10 Cents Ni (Side B)":        ("xnickel_b",       "XNICKEL"),
    # XCU
    "Russian Empire 5 Kopecks Cu (Side A)":     ("xcu_a",           "XCU"),
    "Russian Empire 5 Kopecks Cu (Side B)":     ("xcu_b",           "XCU"),
    # XAL
    "Germany 3 Marks 1922 Al (Side A)":         ("xal_a",           "XAL"),
    "Germany 3 Marks 1922 Al (Side B)":         ("xal_b",           "XAL"),
    # XZNNIP
    "Ukraine 10 UAH 2022 ZnNi (Side A)":        ("xznnip_a",        "XZNNIP"),
    "Ukraine 10 UAH 2022 ZnNi (Side B)":        ("xznnip_b",        "XZNNIP"),
}

KEYS_7D = ("dRp1_n", "k1", "k2", "df_n", "dL1_n", "df1_n", "mass_n")
KEYS_6D = ("dRp1_n", "k1", "k2", "df_n", "dL1_n", "df1_n")


# ── Helpers ───────────────────────────────────────────────────────────────────

def load_ndjson(path: Path) -> list[dict]:
    records = []
    with path.open(encoding="utf-8") as fh:
        for i, line in enumerate(fh):
            line = line.strip()
            if line:
                try:
                    records.append(json.loads(line))
                except Exception as e:
                    print(f"  [WARN] Parse error {path.name}:{i}: {e}", file=sys.stderr)
    return records


def drift_ratio(rec: dict) -> float:
    steps = rec.get("steps", [])
    if len(steps) < 4:
        return 0.0
    rp0 = steps[0].get("rp_median", 0)
    rp3 = steps[3].get("rp_median", 0)
    if rp0 == 0:
        return 0.0
    return abs(rp3 - rp0) / rp0


def features_7d(rec: dict) -> dict | None:
    """Extract 7D feature vector from a C-14 NDJSON record."""
    pv = rec.get("production_vector", {})
    # All 6D LDC1101 dims must be present
    for k in KEYS_6D:
        if k not in pv:
            return None
    # mass_n must be present and valid (not sentinel)
    mass_n = pv.get("mass_n", -1.0)
    if mass_n < 0.0:
        return None
    return {
        "dRp1_n": round(pv["dRp1_n"], 5),
        "k1":     round(pv["k1"],     5),
        "k2":     round(pv["k2"],     5),
        "df_n":   round(pv["df_n"],   5),
        "dL1_n":  round(pv["dL1_n"],  5),
        "df1_n":  round(pv["df1_n"],  5),
        "mass_n": round(mass_n,        5),
    }


def centroid_7d(vectors: list[dict]) -> dict:
    return {k: round(statistics.mean(v[k] for v in vectors), 5) for k in KEYS_7D}


def radius_95pct_7d(vectors: list[dict], c: dict) -> float:
    dists = sorted(
        math.sqrt(sum((v[k] - c[k]) ** 2 for k in KEYS_7D))
        for v in vectors
    )
    idx = min(int(len(dists) * 0.95), len(dists) - 1)
    return round(dists[idx], 4)


def sigma_mass(vectors: list[dict]) -> float:
    """Standard deviation of mass_g values (for Wave 10 exit criterion #2)."""
    masses = [v["mass_n"] * MASS_REF_G for v in vectors]
    if len(masses) < 2:
        return 0.0
    return round(statistics.stdev(masses), 3)


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    # ── Guard: C-14 unified file must exist ───────────────────────────────────
    if not C14_NDJSON.exists():
        print("ERROR: C-14 unified session file not found:")
        print(f"       {C14_NDJSON}")
        print()
        print("Steps to prepare:")
        print("  1. Complete C-14 hardware session (see SESSION_PROTOCOL.md)")
        print("  2. Copy session NDJSON files from SD card to:")
        print(f"       {C14_NDJSON.parent}/")
        print("  3. Merge all session files into the unified NDJSON:")
        print(f"       {C14_NDJSON.name}")
        print("     (one NDJSON object per line — NDJSON format)")
        print()
        print("Expected coin_name labels in C-14 session:")
        for coin_name, (gk, mc) in C14_GROUP_KEYS.items():
            print(f"  '{coin_name}'  →  {mc} / {gk}")
        sys.exit(1)

    print("CoinTrace gen-8 DB builder")
    print(f"  C-14 NDJSON : {C14_NDJSON.name}")
    print(f"  Output      : {OUTPUT}")
    print(f"  Protocol    : {PROTOCOL_ID}")
    print(f"  MASS_REF_G  : {MASS_REF_G} g")
    print()

    # ── Load and filter C-14 records ──────────────────────────────────────────
    raw = load_ndjson(C14_NDJSON)
    print(f"  C-14 raw records: {len(raw)}")

    excl_protocol = excl_drift = excl_test = excl_mass = excl_unknown = 0
    groups: dict[str, list[dict]] = {}
    group_meta: dict[str, tuple[str, str]] = {}  # gk → (metal_code, coin_name)

    for rec in raw:
        if rec.get("_test_only"):
            excl_test += 1
            continue

        # Accept both p3_ (legacy fallback measurements) and p4_ (target)
        pid = rec.get("production_vector", {}).get("protocol_id") or ""
        # Note: protocol_id is a top-level NDJSON field in firmware, not in production_vector
        # The firmware writes it as a field at measurement level; check both common locations.
        if pid == "" and "protocol_id" not in rec:
            # Accept if record has mass_g (C-14 minimal requirement); skip if clearly p3_ only
            pass
        if rec.get("protocol_id", PROTOCOL_ID) not in (PROTOCOL_ID, "p3_MIKROE3240_b06_012mm"):
            # Unknown protocol
            excl_protocol += 1
            continue

        if drift_ratio(rec) > DRIFT_THRESHOLD:
            excl_drift += 1
            continue

        coin_name = rec.get("coin_name", "")
        mapping = C14_GROUP_KEYS.get(coin_name)
        if mapping is None:
            excl_unknown += 1
            if coin_name:
                print(f"  [WARN] Unknown coin_name (not in C14_GROUP_KEYS): '{coin_name}'")
            continue

        gk, metal_code = mapping

        feats = features_7d(rec)
        if feats is None:
            excl_mass += 1
            continue

        if gk not in groups:
            groups[gk] = []
            group_meta[gk] = (metal_code, coin_name)
        groups[gk].append(feats)

    print(f"  Protocol excluded  : {excl_protocol}")
    print(f"  Drift excluded     : {excl_drift}")
    print(f"  Test-only excluded : {excl_test}")
    print(f"  No mass_n excluded : {excl_mass}")
    print(f"  Unknown coin_name  : {excl_unknown}")
    print()

    # ── Check coverage ────────────────────────────────────────────────────────
    all_expected = set(C14_GROUP_KEYS.values())
    covered = {(gk, mc) for gk, mc in groups.keys()
               for _ in [group_meta[gk]]}
    covered = set(groups.keys())

    print(f"  Groups found: {len(groups)}/{len(set(gk for gk, _ in C14_GROUP_KEYS.values()))}")
    missing = []
    for coin_name, (gk, mc) in C14_GROUP_KEYS.items():
        if gk not in groups:
            missing.append(f"    MISSING: {mc} / {gk}  ('{coin_name}')")
    if missing:
        print(f"  [WARN] {len(missing)} groups missing from C-14 NDJSON:")
        for m in missing:
            print(m)
    print()

    # ── Per-group statistics ───────────────────────────────────────────────────
    print(f"  {'Group':<22} {'MC':<10} {'n':>3}  {'mass_g_mean':>11}  "
          f"{'σ_mass_g':>9}  {'r95_7d':>7}  Note")
    print("  " + "-" * 87)

    entries = []
    session_date = "2026-04-11"

    for gk in sorted(groups.keys()):
        vecs = groups[gk]
        mc, coin_name = group_meta[gk]
        n = len(vecs)

        c = centroid_7d(vecs)
        r95 = radius_95pct_7d(vecs, c)
        sig_mass = sigma_mass(vecs)
        mass_g_mean = round(c["mass_n"] * MASS_REF_G, 2)

        note = ""
        if n < 5:
            note = f"⚠️  n={n} < 5 (insufficient)"
        elif n < 10:
            note = f"n={n} (ok, recommend 10+)"
        if sig_mass > 0.2:
            note += f" ⚠️  σ_mass={sig_mass}g > 0.2g"

        print(f"  {gk:<22} {mc:<10} {n:>3}  {mass_g_mean:>11.2f}g "
              f"  {sig_mass:>7.3f}g  {r95:>7.4f}  {note}")

        entries.append({
            "id":            f"{gk}/c14_hw_{session_date}",
            "protocol_id":   PROTOCOL_ID,
            "metal_code":    mc,
            "coin_name":     coin_name,
            "year":          None,
            "centroid":      c,
            "radius_95pct":  r95,
            "records_count": n,
        })

    print()

    if not entries:
        print("ERROR: No entries built — C-14 NDJSON has no valid records.")
        print("       Check coin_name labels match C14_GROUP_KEYS exactly.")
        sys.exit(1)

    # ── Build gen-8 output ────────────────────────────────────────────────────
    gen8 = {
        "version":      8,
        "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "generation":   8,
        "session_id":   "c14",
        "protocols":    [PROTOCOL_ID],
        "entries":      entries,
    }

    OUTPUT.write_text(json.dumps(gen8, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print(f"  gen-8 DB written: {len(entries)} entries, {len(set(e['metal_code'] for e in entries))} classes")
    print(f"  Output: {OUTPUT}")
    print()
    print("  Next step: python scripts/a8_pairwise_7d.py")
    print("  Wave 10 exit criterion: all pairs > 2.0σ in 7D")

    # ── Guard: remove this line after reviewing C-14 data and confirming gen-8 output ──
    sys.exit(1)  # TODO(C-14): remove before committing gen-8 DB


if __name__ == "__main__":
    main()
