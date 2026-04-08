#!/usr/bin/env python3
"""
CoinTrace gen-7 database builder
==================================
Inputs:  docs/external/С-10/2026-04-04.C10_unified.ndjson   (C-10/C-11 unified, unchanged)
         docs/external/2026-04-03.8.session_cf1edfcd.ndjson  (C-8, Silver classes, unchanged)
         docs/external/С-12/2026-04-07.C12_unified.ndjson    (C-12 session — to be recorded)
Output:  data/sd_seed/CoinTrace/database/index.json  (gen-7)

Changes vs gen-6
----------------
Awaiting C-12 session results. Planned measurements:

  XUSSR10 moneta-1  (USSR 10 Rubles Ag900, coin already in DB — same as C-10)
    coin_name: "USSR 10 Rubles Ag900 moneta-1 (Side A/B)"
    metal_code: XUSSR10
    target: 5+5 records
    purpose: session-to-session drift check; merge into xussr10_c12_a/b if clean

  XUSSR10 moneta-2  (USSR 10 Rubles Ag900, different year/mintmark)
    coin_name: "USSR 10 Rubles Ag900 moneta-2 (Side A/B)"
    metal_code: XUSSR10    [if indistinguishable from moneta-1 by physics]
             OR XUSSR10B   [if distinguishable, or suspected fake — decide after seeing data]
    target: 5+5 records
    purpose: fake detection / design variant check

  XFE verification  (Germany 1.5 Euro CuNi — already in DB)
    coin_name: "Germany 1.5 Euro CuNi (Side A/B)"   ← same as C-10, reuse group key
    metal_code: XFE
    target: 5+5 records
    purpose: verify Side B dRp1_n=-15.88 is stable (close to Ag zone)

  XFENIP reseed  (Slovakia 2 Koruny Steel-Ni — needs_reseed=True in DB)
    coin_name: "Slovakia 2 Koruny Steel-Ni (Side A/B)"   ← same as C-11, reuse group key
    metal_code: XFENIP
    target: 10+10 records  (must run >=20 min after power-on for sensor warmup)
    purpose: reduce r95 from 0.97/1.35 to <0.3

Algorithm changes vs gen-6
---------------------------
- Adds C-12 NDJSON as third input source.
- C-12 XFE records are merged with C-10 XFE (reuse group key xfe_c10_a/b → rename xfe_a/b).
- C-12 XFENIP replaces C-11 XFENIP entries (needs_reseed cleared if r95 < 0.30).
- XUSSR10 handling: decided after C-12 data review.
  See XUSSR10_C12_MERGE flag below.

TODO before running:
  1. Record C-12 session → save as docs/external/C-12/2026-04-07.C12_unified.ndjson
  2. Set coin_name labels exactly as listed above (firmware uses coin_name for grouping).
  3. Review XUSSR10 moneta-2 data — set XUSSR10_C12_MERGE accordingly.
  4. Remove the sys.exit(1) guard at the bottom of this file.
"""

from __future__ import annotations

import json
import math
import statistics
import sys
from datetime import datetime, timezone
from pathlib import Path

# ── Paths ─────────────────────────────────────────────────────────────────────
REPO_ROOT    = Path(__file__).resolve().parent.parent
NDJSON_DIR   = REPO_ROOT / "docs" / "external"
C10_UNIFIED  = NDJSON_DIR / "С-10" / "2026-04-04.C10_unified.ndjson"
C8_SESSION   = NDJSON_DIR / "2026-04-03.8.session_cf1edfcd.ndjson"
C12_UNIFIED  = NDJSON_DIR / "С-12" / "2026-04-07.C12_unified.ndjson"
OUTPUT       = REPO_ROOT / "data" / "sd_seed" / "CoinTrace" / "database" / "index.json"

PROTOCOL_ID      = "p3_MIKROE3240_b06_012mm"
DRIFT_THRESHOLD  = 0.05   # 5%

# ── C-12 behavioral flags (set after reviewing C-12 data) ────────────────────
# True  → merge XUSSR10 moneta-2 into same XUSSR10 class (same coin, same metal)
# False → keep as separate XUSSR10B class (distinguishable, or suspected fake)
XUSSR10_C12_MERGE: bool = True    # C-12 analysis: moneta-2 confirmed same Ag900 metal

# ── Point exclusions (carried forward from gen-6) ────────────────────────────
C10_POINT_EXCL: dict[str, set[int]] = {
    "Kennedy Half Dollar Ag400 (Side B)": {25},
    # XZNNIP_a: idx 0,2 — rp0 outliers (anomalous dRp1_n), identified post-C10
    # r95_a: 0.661 → ~0.05 after exclusion (n=5-2=3 remaining)
    "Ukraine 10 UAH 2022 ZnNi (Side A)": {0, 2},
}

# ── C-8 label remap (Silver classes only; Kennedy excluded — gen-6 decision) ─
C8_REMAP: dict[int, tuple[str, str, str] | None] = {
    0:  None,
    1:  ("XAG999", "American Silver Eagle 1oz (Side A)", "xag999_eagle_a"),
    2:  ("XAG999", "American Silver Eagle 1oz (Side A)", "xag999_eagle_a"),
    3:  ("XAG999", "American Silver Eagle 1oz (Side A)", "xag999_eagle_a"),
    4:  ("XAG999", "American Silver Eagle 1oz (Side A)", "xag999_eagle_a"),
    5:  ("XAG999", "American Silver Eagle 1oz (Side B)", "xag999_eagle_b"),
    6:  ("XAG999", "American Silver Eagle 1oz (Side B)", "xag999_eagle_b"),
    7:  ("XAG999", "American Silver Eagle 1oz (Side B)", "xag999_eagle_b"),
    8:  ("XAG999", "American Silver Eagle 1oz (Side B)", "xag999_eagle_b"),
    9:  ("XAG999", "American Silver Eagle 1oz (Side B)", "xag999_eagle_b"),
    20: ("XAG999", "Australian Kangaroo 1oz (Side A)",   "xag999_kang_a"),
    21: ("XAG999", "Australian Kangaroo 1oz (Side A)",   "xag999_kang_a"),
    22: ("XAG999", "Australian Kangaroo 1oz (Side A)",   "xag999_kang_a"),
    23: ("XAG999", "Australian Kangaroo 1oz (Side A)",   "xag999_kang_a"),
    24: ("XAG999", "Australian Kangaroo 1oz (Side A)",   "xag999_kang_a"),
    25: ("XAG999", "Australian Kangaroo 1oz (Side B)",   "xag999_kang_b"),
    26: ("XAG999", "Australian Kangaroo 1oz (Side B)",   "xag999_kang_b"),
    27: ("XAG999", "Australian Kangaroo 1oz (Side B)",   "xag999_kang_b"),
    28: ("XAG999", "Australian Kangaroo 1oz (Side B)",   "xag999_kang_b"),
    29: ("XAG999", "Australian Kangaroo 1oz (Side B)",   "xag999_kang_b"),
    30: ("XAG800", "Bahamas $1 Ag800 (Side A)",          "xag800_bhs_a"),
    31: ("XAG800", "Bahamas $1 Ag800 (Side A)",          "xag800_bhs_a"),
    32: ("XAG800", "Bahamas $1 Ag800 (Side A)",          "xag800_bhs_a"),
    33: ("XAG800", "Bahamas $1 Ag800 (Side A)",          "xag800_bhs_a"),
    34: ("XAG800", "Bahamas $1 Ag800 (Side A)",          "xag800_bhs_a"),
    35: ("XAG800", "Bahamas $1 Ag800 (Side B)",          "xag800_bhs_b"),
    36: ("XAG800", "Bahamas $1 Ag800 (Side B)",          "xag800_bhs_b"),
    37: ("XAG800", "Bahamas $1 Ag800 (Side B)",          "xag800_bhs_b"),
    38: ("XAG800", "Bahamas $1 Ag800 (Side B)",          "xag800_bhs_b"),
    39: ("XAG800", "Bahamas $1 Ag800 (Side B)",          "xag800_bhs_b"),
}
C8_SILVER_GROUPS = {"xag999_eagle_a", "xag999_eagle_b",
                    "xag999_kang_a",  "xag999_kang_b",
                    "xag800_bhs_a",   "xag800_bhs_b"}

# ── C-10/C-11 group key mapping (unchanged from gen-6) ───────────────────────
C10_GROUP_KEYS: dict[str, str] = {
    "USSR 10 Rubles Ag900 (Side A)":           "xussr10_c10_a",
    "USSR 10 Rubles Ag900 (Side B)":           "xussr10_c10_b",
    "Kennedy Half Dollar Ag400 (Side A)":      "xkenned_c10_a",
    "Kennedy Half Dollar Ag400 (Side B)":      "xkenned_c10_b",
    "Olympic 1984 Ag900 (Side A)":             "xag900_c10_a",
    "Olympic 1984 Ag900 (Side B)":             "xag900_c10_b",
    "Germany GDR 10 Marks CuZn (Side A)":      "xcuzn_c10_a",
    "Germany GDR 10 Marks CuZn (Side B)":      "xcuzn_c10_b",
    "Germany 1.5 Euro CuNi (Side A)":          "xfe_c10_a",
    "Germany 1.5 Euro CuNi (Side B)":          "xfe_c10_b",
    "Germany 3 Marks 1922 Aluminum (Side A)":  "xal_c10_a",
    "Germany 3 Marks 1922 Aluminum (Side B)":  "xal_c10_b",
    "Russian 5 Kopecks Copper (Side A)":       "xcu_c10_a",
    "Russian 5 Kopecks Copper (Side B)":       "xcu_c10_b",
    "Ukraine 10 UAH 2022 ZnNi (Side A)":       "xznnip_c10_a",
    "Ukraine 10 UAH 2022 ZnNi (Side B)":       "xznnip_c10_b",
    "Slovakia 2 Koruny Steel-Ni (Side A)":     "xfenip_c11_a",
    "Slovakia 2 Koruny Steel-Ni (Side B)":     "xfenip_c11_b",
    "Germany 1 Pfennig Steel-Cu (Side A)":     "xfecup_c11_a",
    "Germany 1 Pfennig Steel-Cu (Side B)":     "xfecup_c11_b",
    "South Africa 10 Cents Ni (Side A)":       "xnickel_c11_a",
    "South Africa 10 Cents Ni (Side B)":       "xnickel_c11_b",
}

# ── C-12 group key mapping ────────────────────────────────────────────────────
# coin_name strings MUST match exactly what the firmware writes to NDJSON.
# Set these labels in the session tool before recording.
C12_GROUP_KEYS: dict[str, str] = {
    # XUSSR10 moneta-1 — same coin as C-10, new session for drift verification
    "USSR 10 Rubles Ag900 moneta-1 (Side A)":  "xussr10_c12m1_a",
    "USSR 10 Rubles Ag900 moneta-1 (Side B)":  "xussr10_c12m1_b",
    # XUSSR10 moneta-2 — different year/mintmark; group key determined by XUSSR10_C12_MERGE
    "USSR 10 Rubles Ag900 moneta-2 (Side A)":  "xussr10_c12m2_a",  # metal_code set dynamically
    "USSR 10 Rubles Ag900 moneta-2 (Side B)":  "xussr10_c12m2_b",
    # XFE verification — merged with C-10 records, same group key
    "Germany 1.5 Euro CuNi (Side A)":          "xfe_c10_a",        # reuse C-10 key → will merge
    "Germany 1.5 Euro CuNi (Side B)":          "xfe_c10_b",
    # XFENIP reseed — replaces C-11 entries
    "Slovakia 2 Koruny Steel-Ni (Side A)":     "xfenip_c12_a",
    "Slovakia 2 Koruny Steel-Ni (Side B)":     "xfenip_c12_b",
}

# metal_code for XUSSR10 moneta-2 (depends on XUSSR10_C12_MERGE flag)
_XUSSR10M2_CODE = "XUSSR10" if XUSSR10_C12_MERGE else "XUSSR10B"
C12_METAL_CODE_OVERRIDE: dict[str, str] = {
    "USSR 10 Rubles Ag900 moneta-2 (Side A)": _XUSSR10M2_CODE,
    "USSR 10 Rubles Ag900 moneta-2 (Side B)": _XUSSR10M2_CODE,
}


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
    rp0 = steps[0]["rp_median"]
    rp3 = steps[3]["rp_median"]
    return abs(rp3 - rp0) / rp0


def compute_basefs(rec: dict) -> float:
    fs0  = rec["steps"][0]["fSensor_hz"]
    df_n = rec["production_vector"]["df_n"]
    return fs0 / (1.0 + df_n)


def compute_features_c8(rec: dict, bfs: float) -> dict:
    pv    = rec["production_vector"]
    steps = rec["steps"]
    rp0   = steps[0]["rp_median"]
    rp1   = steps[1]["rp_median"]
    rp2   = steps[2]["rp_median"]
    l0    = steps[0]["l_median"]
    l1    = steps[1]["l_median"]
    fs0   = steps[0]["fSensor_hz"]
    fs1   = steps[1]["fSensor_hz"]

    dRp1_n = pv.get("dRp1_n", (rp1 - rp0) / 800.0)
    k1     = pv.get("k1",     rp1 / rp0)
    k2     = pv.get("k2",     rp2 / rp0)
    dL1_n  = pv.get("dL1_n", (l0 - l1) / 2000.0)

    if "df_n" in pv:
        df_n = pv["df_n"]
        bfs  = fs0 / (1.0 + df_n)
    else:
        df_n = (fs0 - bfs) / bfs

    df1_n = (fs1 - bfs) / bfs

    return {
        "dRp1_n": round(dRp1_n, 5),
        "k1":     round(k1,     5),
        "k2":     round(k2,     5),
        "dL1_n":  round(dL1_n,  5),
        "df_n":   round(df_n,   5),
        "df1_n":  round(df1_n,  5),
    }


def features_from_pv(rec: dict) -> dict:
    pv = rec["production_vector"]
    return {
        "dRp1_n": round(pv["dRp1_n"], 5),
        "k1":     round(pv["k1"],     5),
        "k2":     round(pv["k2"],     5),
        "dL1_n":  round(pv["dL1_n"],  5),
        "df_n":   round(pv["df_n"],   5),
        "df1_n":  round(pv["df1_n"],  5),
    }


def centroid_6d(vectors: list[dict]) -> dict:
    keys = ("dRp1_n", "k1", "k2", "dL1_n", "df_n", "df1_n")
    return {k: round(statistics.mean(v[k] for v in vectors), 5) for k in keys}


def radius_95pct(vectors: list[dict], c: dict) -> float:
    keys = ("dRp1_n", "k1", "k2", "dL1_n", "df_n", "df1_n")
    dists = sorted(
        math.sqrt(sum((v[k] - c[k]) ** 2 for k in keys))
        for v in vectors
    )
    idx = min(int(len(dists) * 0.95), len(dists) - 1)
    return round(dists[idx], 4)


def make_entry(entry_id: str, metal_code: str, coin_name: str,
               vectors: list[dict]) -> dict:
    c = centroid_6d(vectors)
    return {
        "id":            entry_id,
        "protocol_id":   PROTOCOL_ID,
        "metal_code":    metal_code,
        "coin_name":     coin_name,
        "year":          None,
        "centroid":      c,
        "radius_95pct":  radius_95pct(vectors, c),
        "records_count": len(vectors),
    }


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    # ── Guard: C-12 file must exist ───────────────────────────────────────────
    if not C12_UNIFIED.exists():
        print("ERROR: C-12 session file not found:")
        print(f"       {C12_UNIFIED}")
        print()
        print("Record C-12 session, then save NDJSON to the path above."
              "  (directory: docs/external/С-12/)")
        print("Required coin_name labels for the session:")
        for name, gk in C12_GROUP_KEYS.items():
            print(f"  '{name}'  -> {gk}")
        print()
        print(f"XUSSR10_C12_MERGE = {XUSSR10_C12_MERGE}")
        print(f"XUSSR10 moneta-2 metal_code = '{_XUSSR10M2_CODE}'")
        sys.exit(1)

    print("CoinTrace gen-7 DB builder")
    print(f"  C-8 session : {C8_SESSION.name}")
    print(f"  C-10 unified: {C10_UNIFIED.name}")
    print(f"  C-12 unified: {C12_UNIFIED.name}")
    print(f"  Output      : {OUTPUT}")
    print(f"  XUSSR10_C12_MERGE = {XUSSR10_C12_MERGE}"
          f"  (moneta-2 -> '{_XUSSR10M2_CODE}')")
    print()

    # ══════════════════════════════════════════════════════════════════════════
    # PART A — C-8 Silver classes (XAG999 + XAG800 only — same as gen-6)
    # ══════════════════════════════════════════════════════════════════════════
    c8_recs = load_ndjson(C8_SESSION)
    bfs_samples = [compute_basefs(r) for r in c8_recs
                   if "df_n" in r.get("production_vector", {})]
    calibrated_bfs = statistics.median(bfs_samples)
    print(f"  C-8 baseFS: {calibrated_bfs:.1f} Hz  (n={len(bfs_samples)})")

    c8_groups: dict[str, list[dict]] = {}
    c8_meta:   dict[str, tuple[str, str]] = {}
    c8_excl_drift = 0

    for rec in c8_recs:
        idx = rec["index"]
        mapping = C8_REMAP.get(idx)
        if mapping is None:
            continue
        metal_code, coin_name, gk = mapping
        if gk not in C8_SILVER_GROUPS:
            continue

        d = drift_ratio(rec)
        if d > DRIFT_THRESHOLD:
            c8_excl_drift += 1
            continue

        feats = compute_features_c8(rec, calibrated_bfs)
        if gk not in c8_groups:
            c8_groups[gk] = []
            c8_meta[gk] = (metal_code, coin_name)
        c8_groups[gk].append(feats)

    print(f"  C-8 drift excluded: {c8_excl_drift}")

    # ══════════════════════════════════════════════════════════════════════════
    # PART B — C-10/C-11 unified (same as gen-6; XFENIP c11 dropped below)
    # ══════════════════════════════════════════════════════════════════════════
    c10_recs = load_ndjson(C10_UNIFIED)
    print(f"  C-10 unified loaded: {len(c10_recs)} records")

    c10_groups: dict[str, list[dict]] = {}
    c10_meta:   dict[str, tuple[str, str]] = {}
    c10_excl_test  = 0
    c10_excl_drift = 0
    c10_excl_point = 0

    # XFENIP C-11 is replaced by C-12 reseed — skip it here
    SKIP_GROUPS_C10 = {"xfenip_c11_a", "xfenip_c11_b"}

    for rec in c10_recs:
        if rec.get("_test_only"):
            c10_excl_test += 1
            continue

        d = drift_ratio(rec)
        if d > DRIFT_THRESHOLD:
            c10_excl_drift += 1
            continue

        coin_name  = rec["coin_name"]
        rec_index  = rec.get("index")

        excl_indices = C10_POINT_EXCL.get(coin_name, set())
        if rec_index in excl_indices:
            c10_excl_point += 1
            continue

        metal_code = rec["metal_code"]
        gk = C10_GROUP_KEYS.get(coin_name)
        if gk is None:
            side = "a" if "Side A" in coin_name else "b"
            gk = f"{metal_code.lower()}_c10_{side}"

        if gk in SKIP_GROUPS_C10:
            continue

        feats = features_from_pv(rec)
        if gk not in c10_groups:
            c10_groups[gk] = []
            c10_meta[gk] = (metal_code, coin_name)
        c10_groups[gk].append(feats)

    print(f"  C-10 test excluded : {c10_excl_test}")
    print(f"  C-10 drift excluded: {c10_excl_drift}")
    print(f"  C-10 point excluded: {c10_excl_point}")
    print(f"  C-10 XFENIP skipped (replaced by C-12 reseed)")

    # ══════════════════════════════════════════════════════════════════════════
    # PART C — C-12 session
    # ══════════════════════════════════════════════════════════════════════════
    c12_recs = load_ndjson(C12_UNIFIED)
    print(f"  C-12 unified loaded: {len(c12_recs)} records")

    c12_groups: dict[str, list[dict]] = {}
    c12_meta:   dict[str, tuple[str, str]] = {}
    c12_excl_drift = 0

    for rec in c12_recs:
        if rec.get("_test_only"):
            continue

        d = drift_ratio(rec)
        if d > DRIFT_THRESHOLD:
            c12_excl_drift += 1
            print(f"  [DRIFT C12] {rec.get('metal_code')} "
                  f"idx={rec.get('index')} drift={d*100:.1f}% -> excluded")
            continue

        coin_name  = rec["coin_name"]
        metal_code = C12_METAL_CODE_OVERRIDE.get(coin_name, rec["metal_code"])
        gk = C12_GROUP_KEYS.get(coin_name)
        if gk is None:
            print(f"  [WARN C12] Unknown coin_name: '{coin_name}' — skipped")
            continue

        feats = features_from_pv(rec)
        if gk not in c12_groups:
            c12_groups[gk] = []
            c12_meta[gk] = (metal_code, coin_name)
        c12_groups[gk].append(feats)

    print(f"  C-12 drift excluded: {c12_excl_drift}")

    # XFE C-12 records are merged into the existing C-10 XFE groups
    for gk in ("xfe_c10_a", "xfe_c10_b"):
        c12_vecs = c12_groups.pop(gk, [])
        if c12_vecs:
            c10_vecs = c10_groups.get(gk, [])
            c10_groups[gk] = c10_vecs + c12_vecs
            print(f"  [MERGE XFE] {gk}: C-10 n={len(c10_vecs)} + C-12 n={len(c12_vecs)}"
                  f" -> n={len(c10_groups[gk])}")

    # XUSSR10 merge: 3 output entries from C-10 + C-12 groups
    #   xussr10_m1_a = C-10 side-A  + C-12 moneta-1 side-A            (n=5+5=10)
    #   xussr10_m2_a = C-12 moneta-2 side-A only                       (n=5)
    #   xussr10_b    = C-10 side-B  + C-12 m1 side-B + C-12 m2 side-B (n=5+5+5=15)
    _m1a_c10  = c10_groups.pop("xussr10_c10_a",   [])
    _m1a_c12  = c12_groups.pop("xussr10_c12m1_a", [])
    _b_c10    = c10_groups.pop("xussr10_c10_b",   [])
    _b_c12m1  = c12_groups.pop("xussr10_c12m1_b", [])
    _m2a_c12  = c12_groups.pop("xussr10_c12m2_a", [])
    _b_c12m2  = c12_groups.pop("xussr10_c12m2_b", [])

    c12_groups["xussr10_m1_a"] = _m1a_c10 + _m1a_c12
    c12_meta["xussr10_m1_a"]   = ("XUSSR10", "USSR 10 Rubles Ag900 moneta-1 (Side A)")
    c12_groups["xussr10_m2_a"] = _m2a_c12
    c12_meta["xussr10_m2_a"]   = ("XUSSR10", "USSR 10 Rubles Ag900 moneta-2 (Side A)")
    c12_groups["xussr10_b"]    = _b_c10 + _b_c12m1 + _b_c12m2
    c12_meta["xussr10_b"]      = ("XUSSR10", "USSR 10 Rubles Ag900 (Side B)")

    print(f"  [MERGE XUSSR10] xussr10_m1_a: C-10 n={len(_m1a_c10)} + C-12 n={len(_m1a_c12)}"
          f" -> n={len(c12_groups['xussr10_m1_a'])}")
    print(f"  [MERGE XUSSR10] xussr10_m2_a: C-12 n={len(_m2a_c12)} (only)")
    print(f"  [MERGE XUSSR10] xussr10_b: C-10 n={len(_b_c10)} + m1-B n={len(_b_c12m1)}"
          f" + m2-B n={len(_b_c12m2)} -> n={len(c12_groups['xussr10_b'])}")

    # ══════════════════════════════════════════════════════════════════════════
    # PART D — Build entries
    # ══════════════════════════════════════════════════════════════════════════
    entries: list[dict] = []

    print("\n─── C-8 Silver classes (carry-forward; XAG999+XAG800 only) ──────")
    for gk in sorted(c8_groups.keys()):
        metal, coin = c8_meta[gk]
        entry = make_entry(f"{gk}/c8_hw_2026-04-03", metal, coin, c8_groups[gk])
        entries.append(entry)
        c = entry["centroid"]
        print(f"  {gk:22s}  n={len(c8_groups[gk]):2d}  r95={entry['radius_95pct']:.4f}")

    print("\n─── C-10/C-11 re-seed + new classes (gen-6 base) ─────────────────")
    for gk in sorted(c10_groups.keys()):
        metal, coin = c10_meta[gk]
        entry = make_entry(f"{gk}/c10_hw_2026-04-04", metal, coin, c10_groups[gk])
        entries.append(entry)
        c = entry["centroid"]
        print(f"  {gk:22s}  n={len(c10_groups[gk]):2d}  r95={entry['radius_95pct']:.4f}")

    print("\n─── C-12 new / reseeded entries ───────────────────────────────────")
    for gk in sorted(c12_groups.keys()):
        metal, coin = c12_meta[gk]
        entry = make_entry(f"{gk}/c12_hw_2026-04-07", metal, coin, c12_groups[gk])
        # XFENIP: clear needs_reseed if r95 < 0.30
        if metal == "XFENIP":
            if entry["radius_95pct"] < 0.30:
                print(f"  [XFENIP] r95={entry['radius_95pct']:.4f} < 0.30 — needs_reseed cleared")
            else:
                entry["needs_reseed"] = True
                print(f"  [XFENIP] r95={entry['radius_95pct']:.4f} >= 0.30 — needs_reseed retained")
        entries.append(entry)
        print(f"  {gk:22s}  n={len(c12_groups[gk]):2d}  r95={entry['radius_95pct']:.4f}"
              f"  metal={metal}")

    # ── Write ─────────────────────────────────────────────────────────────────
    now_iso = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    db = {
        "version":      7,
        "generated_at": now_iso,
        "generation":   7,
        "session_id":   "c10_c11_c12",
        "protocols":    [PROTOCOL_ID],
        "entries":      entries,
    }

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with OUTPUT.open("w", encoding="utf-8") as fh:
        json.dump(db, fh, indent=2, ensure_ascii=False)
        fh.write("\n")

    # ── Summary ───────────────────────────────────────────────────────────────
    print(f"\n  Total entries: {len(entries)}")
    by_metal: dict[str, list] = {}
    for e in entries:
        by_metal.setdefault(e["metal_code"], []).append(e)
    print("\n  Classes in gen-7 DB:")
    for mc in sorted(by_metal.keys()):
        sides = len(by_metal[mc])
        n_total = sum(e["records_count"] for e in by_metal[mc])
        r95_max = max(e["radius_95pct"] for e in by_metal[mc])
        df1_mean = statistics.mean(e["centroid"]["df1_n"] for e in by_metal[mc])
        needs_flag = " [needs_reseed]" if any(
            e.get("needs_reseed") for e in by_metal[mc]) else ""
        print(f"  {mc:<12}  {sides} side(s)  n={n_total:3d}  "
              f"df1_n~{df1_mean:.4f}  r95_max={r95_max:.4f}{needs_flag}")

    print(f"\n  Written -> {OUTPUT}")
    print("  Done.")


if __name__ == "__main__":
    main()
