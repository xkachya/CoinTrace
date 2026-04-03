#!/usr/bin/env python3
"""
CoinTrace gen-4 database builder (P1)
======================================
Inputs:  NDJSON sessions in docs/external/
Output:  data/sd_seed/CoinTrace/database/index.json  (gen-4)

Algorithm
---------
1. Load all 5 NDJSON session files.
2. Apply C-8 label remap (per docs/external/2026-04-03.C8_DISCOVERY_ANALYSIS_REPORT.md).
3. Compute calibrated baseFS (empty-coil frequency) from sessions with known df_n.
4. Compute retroactive df_n for slope-era sessions (C-7.1/7.2/7.3) and
   df1_n for ALL sessions using per-record or calibrated baseFS.
5. Filter records: drift > DRIFT_THRESHOLD or metal_code == "UNKN" are excluded.
6. Group records into DB entries (coin × side).
7. Compute 6D centroid + radius_95pct per entry.
8. Carry forward gen-3 entries not covered by current session data (XCUZN, XAG900).
9. Write index.json gen-4.

References
----------
- Analysis:   docs/external/2026-04-03.C8_DISCOVERY_ANALYSIS_REPORT.md
- Distance:   lib/StorageManager/src/FingerprintCache.cpp  query()
- Features:   lib/StorageManager/src/VectorCompute.h
- ADR-VEC-002: df1_n = (fSensor_step1 - baseFS) / baseFS
"""

from __future__ import annotations

import json
import math
import statistics
import sys
from datetime import datetime, timezone
from pathlib import Path

# ── Paths ─────────────────────────────────────────────────────────────────────
REPO_ROOT  = Path(__file__).resolve().parent.parent
NDJSON_DIR = REPO_ROOT / "docs" / "external"
OUTPUT     = REPO_ROOT / "data" / "sd_seed" / "CoinTrace" / "database" / "index.json"

SESSIONS = [
    # C-7 sessions excluded — two reasons:
    #   1. C-7.1/7.2/7.3: slope-era firmware, no df_n stored → retroactive baseFS uncertain
    #   2. C-7.4: df_n present but baseFS=780,981 Hz vs C-8 baseFS=794,358 Hz (+1.7% drift)
    #      → mixing C-7.4 with C-8 introduces systematic bias in df_n/df1_n features
    # XFE/XAL/XCU/XZNNIP carried forward from gen-3 (see GEN3_CARRY_FORWARD).
    # Re-seed planned in C-11+ with stable hardware state.
    "2026-04-03.8.session_cf1edfcd.ndjson",   # C-8  50 rec  baseFS=794,358 Hz  WRONG LABELS → remapped
]

PROTOCOL_ID    = "p3_MIKROE3240_b06_012mm"
DRIFT_THRESHOLD = 0.05   # 5 % — matches VectorCompute::DRIFT_THRESHOLD

# ── C-8 label remap ───────────────────────────────────────────────────────────
# Real coin assignments for session_cf1edfcd.ndjson (2026-04-03, C-8).
# Entire session was recorded with wrong metal_codes; correct mapping per
# per-record video+weight analysis in C8_DISCOVERY_ANALYSIS_REPORT.md.
#
# Tuple: (metal_code, coin_name, group_key)
# None  → exclude record (drift outlier idx=0: drift 9.34 %)
C8_REMAP: dict[int, tuple[str, str, str] | None] = {
    0:  None,
    1:  ("XAG999",    "American Silver Eagle 1oz (Side A)",  "xag999_eagle_a"),
    2:  ("XAG999",    "American Silver Eagle 1oz (Side A)",  "xag999_eagle_a"),
    3:  ("XAG999",    "American Silver Eagle 1oz (Side A)",  "xag999_eagle_a"),
    4:  ("XAG999",    "American Silver Eagle 1oz (Side A)",  "xag999_eagle_a"),
    5:  ("XAG999",    "American Silver Eagle 1oz (Side B)",  "xag999_eagle_b"),
    6:  ("XAG999",    "American Silver Eagle 1oz (Side B)",  "xag999_eagle_b"),
    7:  ("XAG999",    "American Silver Eagle 1oz (Side B)",  "xag999_eagle_b"),
    8:  ("XAG999",    "American Silver Eagle 1oz (Side B)",  "xag999_eagle_b"),
    9:  ("XAG999",    "American Silver Eagle 1oz (Side B)",  "xag999_eagle_b"),
    10: ("XKENNEDY",  "Kennedy Half Dollar 1967 (Side A)",   "xkennedy_a"),
    11: ("XKENNEDY",  "Kennedy Half Dollar 1967 (Side A)",   "xkennedy_a"),
    12: ("XKENNEDY",  "Kennedy Half Dollar 1967 (Side A)",   "xkennedy_a"),
    13: ("XKENNEDY",  "Kennedy Half Dollar 1967 (Side A)",   "xkennedy_a"),
    14: ("XKENNEDY",  "Kennedy Half Dollar 1967 (Side A)",   "xkennedy_a"),
    15: ("XKENNEDY",  "Kennedy Half Dollar 1967 (Side B)",   "xkennedy_b"),
    16: ("XKENNEDY",  "Kennedy Half Dollar 1967 (Side B)",   "xkennedy_b"),
    17: ("XKENNEDY",  "Kennedy Half Dollar 1967 (Side B)",   "xkennedy_b"),
    18: ("XKENNEDY",  "Kennedy Half Dollar 1967 (Side B)",   "xkennedy_b"),
    19: ("XKENNEDY",  "Kennedy Half Dollar 1967 (Side B)",   "xkennedy_b"),
    20: ("XAG999",    "Australian Kangaroo 1oz (Side A)",    "xag999_kang_a"),
    21: ("XAG999",    "Australian Kangaroo 1oz (Side A)",    "xag999_kang_a"),
    22: ("XAG999",    "Australian Kangaroo 1oz (Side A)",    "xag999_kang_a"),
    23: ("XAG999",    "Australian Kangaroo 1oz (Side A)",    "xag999_kang_a"),
    24: ("XAG999",    "Australian Kangaroo 1oz (Side A)",    "xag999_kang_a"),
    25: ("XAG999",    "Australian Kangaroo 1oz (Side B)",    "xag999_kang_b"),
    26: ("XAG999",    "Australian Kangaroo 1oz (Side B)",    "xag999_kang_b"),
    27: ("XAG999",    "Australian Kangaroo 1oz (Side B)",    "xag999_kang_b"),
    28: ("XAG999",    "Australian Kangaroo 1oz (Side B)",    "xag999_kang_b"),
    29: ("XAG999",    "Australian Kangaroo 1oz (Side B)",    "xag999_kang_b"),
    30: ("XAG800",    "Bahamas $1 Ag800 (Side A)",           "xag800_bhs_a"),
    31: ("XAG800",    "Bahamas $1 Ag800 (Side A)",           "xag800_bhs_a"),
    32: ("XAG800",    "Bahamas $1 Ag800 (Side A)",           "xag800_bhs_a"),
    33: ("XAG800",    "Bahamas $1 Ag800 (Side A)",           "xag800_bhs_a"),
    34: ("XAG800",    "Bahamas $1 Ag800 (Side A)",           "xag800_bhs_a"),
    35: ("XAG800",    "Bahamas $1 Ag800 (Side B)",           "xag800_bhs_b"),
    36: ("XAG800",    "Bahamas $1 Ag800 (Side B)",           "xag800_bhs_b"),
    37: ("XAG800",    "Bahamas $1 Ag800 (Side B)",           "xag800_bhs_b"),
    38: ("XAG800",    "Bahamas $1 Ag800 (Side B)",           "xag800_bhs_b"),
    39: ("XAG800",    "Bahamas $1 Ag800 (Side B)",           "xag800_bhs_b"),
    40: ("XUSSR10R",  "USSR 10 Rubles Ag900 (Side A)",       "xussr10r_a"),
    41: ("XUSSR10R",  "USSR 10 Rubles Ag900 (Side A)",       "xussr10r_a"),
    42: ("XUSSR10R",  "USSR 10 Rubles Ag900 (Side A)",       "xussr10r_a"),
    43: ("XUSSR10R",  "USSR 10 Rubles Ag900 (Side A)",       "xussr10r_a"),
    44: ("XUSSR10R",  "USSR 10 Rubles Ag900 (Side A)",       "xussr10r_a"),
    45: ("XUSSR10R",  "USSR 10 Rubles Ag900 (Side B)",       "xussr10r_b"),
    46: ("XUSSR10R",  "USSR 10 Rubles Ag900 (Side B)",       "xussr10r_b"),
    47: ("XUSSR10R",  "USSR 10 Rubles Ag900 (Side B)",       "xussr10r_b"),
    48: ("XUSSR10R",  "USSR 10 Rubles Ag900 (Side B)",       "xussr10r_b"),
    49: ("XUSSR10R",  "USSR 10 Rubles Ag900 (Side B)",       "xussr10r_b"),
}

# Coins covered by C-8 A/B remap — all other metal_codes from C-8 are UNKN/noise
C8_VALID_GROUPS = {v[2] for v in C8_REMAP.values() if v is not None}
# These coins were seeded from C-7 sessions that are excluded due to
# inter-session baseFS drift (+1.7% C-7.4 vs C-8).
# df1_n is set to 0.0 (placeholder; weight=0.0 in current matcher.json).
# Re-seed plan:
#   C-10: Kennedy (A/B) + USSR (A/B)             — 2026-04-xx
#   C-11: XFE_STEEL (Ukraine kopek, DE Pfennig, BR centavo, SK krona)
#          XNICKEL (SA 10c), XBIMETAL (FR 10Fr)  — planned
#   C-12: XAL, XCU, XZNNIP, XCUZN re-seed       — planned
GEN3_CARRY_FORWARD = [
    {
        "id":           "xfe/c7_hw_2026-04-02",
        "protocol_id":  PROTOCOL_ID,
        "metal_code":   "XFE",
        "coin_name":    "Germany 1.5 Euro 1997 European Week Berlin",
        "year":         None,
        "centroid": {
            "dRp1_n": -15.5818,
            "k1":      1.30578,
            "k2":      1.40526,
            "dL1_n":  -2.4221,
            "df_n":    0.8078,
            "df1_n":   0.0,      # placeholder — C-7 excluded (baseFS drift)
        },
        "radius_95pct":  1.985,
        "records_count": 5,
    },
    {
        "id":           "xal/c7_hw_2026-04-02",
        "protocol_id":  PROTOCOL_ID,
        "metal_code":   "XAL",
        "coin_name":    "Germany 50 Pfennig 1919-1922 Weimar Republic",
        "year":         None,
        "centroid": {
            "dRp1_n": -17.0326,
            "k1":      1.34718,
            "k2":      1.45558,
            "dL1_n":  -2.3197,
            "df_n":    0.7369,
            "df1_n":   0.0,      # placeholder — C-7 excluded (baseFS drift)
        },
        "radius_95pct":  1.6015,
        "records_count": 5,
    },
    {
        "id":           "xcu/c7_hw_2026-04-02",
        "protocol_id":  PROTOCOL_ID,
        "metal_code":   "XCU",
        "coin_name":    "Russian Empire 5 Kopecks 1867-1917",
        "year":         None,
        "centroid": {
            "dRp1_n": -19.7046,
            "k1":      1.43022,
            "k2":      1.55412,
            "dL1_n":  -2.6502,
            "df_n":    0.8805,
            "df1_n":   0.0,      # placeholder — C-7 excluded (baseFS drift)
        },
        "radius_95pct":  2.017,
        "records_count": 5,
    },
    {
        "id":           "xznnip/c7_hw_2026-04-02",
        "protocol_id":  PROTOCOL_ID,
        "metal_code":   "XZNNIP",
        "coin_name":    "Ukraine 10 UAH 2022 Territorial Defence Forces",
        "year":         None,
        "centroid": {
            "dRp1_n": -19.8208,
            "k1":      1.44078,
            "k2":      1.57634,
            "dL1_n":  -2.3553,
            "df_n":    0.7476,
            "df1_n":   0.0,      # placeholder — C-7 excluded (baseFS drift)
        },
        "radius_95pct":  3.58,
        "records_count": 5,
    },
    {
        "id":           "xag900/c7_hw_2026-04-02",
        "protocol_id":  PROTOCOL_ID,
        "metal_code":   "XAG900",
        "coin_name":    "Olympic 1984 Ag900",
        "year":         None,
        "centroid": {
            "dRp1_n": -12.8278,
            "k1":      1.23064,
            "k2":      1.30598,
            "dL1_n":  -2.2345,
            "df_n":    0.7394,
            "df1_n":   0.0,      # placeholder — re-seed needed
        },
        "radius_95pct":  3.7416,
        "records_count": 5,
    },
    {
        "id":           "xcuzn/c7_hw_2026-04-02",
        "protocol_id":  PROTOCOL_ID,
        "metal_code":   "XCUZN",
        "coin_name":    "Germany GDR 10 Marks 1990",
        "year":         None,
        "centroid": {
            "dRp1_n": -26.518,
            "k1":      1.80208,
            "k2":      2.0513,
            "dL1_n":  -2.33,
            "df_n":    0.7738,
            "df1_n":   0.0,      # placeholder — re-seed needed
        },
        "radius_95pct":  0.8256,
        "records_count": 5,
    },
]


# ── Helpers ───────────────────────────────────────────────────────────────────

def load_ndjson(path: Path) -> list[dict]:
    records = []
    with path.open(encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


def drift_ratio(rec: dict) -> float:
    """Absolute drift: |rp_step3 - rp_step0| / rp_step0."""
    rp0 = rec["steps"][0]["rp_median"]
    rp3 = rec["steps"][3]["rp_median"]
    return abs(rp3 - rp0) / rp0


def compute_basefs(rec: dict) -> float:
    """Derive per-record empty-coil frequency from known df_n."""
    fs0  = rec["steps"][0]["fSensor_hz"]
    df_n = rec["production_vector"]["df_n"]
    return fs0 / (1.0 + df_n)


def compute_features(rec: dict, bfs: float) -> dict:
    """
    Compute all 6 normalised features for a single record.

    bfs  — empty-coil frequency (Hz); either per-record or calibrated.
    Returns dict with keys: dRp1_n, k1, k2, dL1_n, df_n, df1_n.
    """
    pv    = rec["production_vector"]
    steps = rec["steps"]

    rp0 = steps[0]["rp_median"]
    rp1 = steps[1]["rp_median"]
    rp2 = steps[2]["rp_median"]
    l0  = steps[0]["l_median"]
    l1  = steps[1]["l_median"]
    fs0 = steps[0]["fSensor_hz"]
    fs1 = steps[1]["fSensor_hz"]

    # prefer stored normalised values; fall back to manual calculation
    dRp1_n = pv.get("dRp1_n", (rp1 - rp0) / 800.0)
    k1     = pv.get("k1",     rp1 / rp0)
    k2     = pv.get("k2",     rp2 / rp0)
    dL1_n  = pv.get("dL1_n", (l0 - l1) / 2000.0)

    if "df_n" in pv:
        df_n = pv["df_n"]
        bfs  = fs0 / (1.0 + df_n)   # per-record baseFS overrides calibrated
    else:
        # slope-era session: derive df_n retroactively from calibrated baseFS
        df_n = (fs0 - bfs) / bfs

    df1_n = (fs1 - bfs) / bfs        # ADR-VEC-002

    return {
        "dRp1_n": round(dRp1_n, 5),
        "k1":     round(k1,     5),
        "k2":     round(k2,     5),
        "dL1_n":  round(dL1_n,  5),
        "df_n":   round(df_n,   5),
        "df1_n":  round(df1_n,  5),
    }


def centroid_6d(vectors: list[dict]) -> dict:
    keys = ("dRp1_n", "k1", "k2", "dL1_n", "df_n", "df1_n")
    return {k: round(statistics.mean(v[k] for v in vectors), 5) for k in keys}


def radius_95pct(vectors: list[dict], c: dict) -> float:
    """
    95th-percentile unweighted Euclidean distance from centroid.
    Features are already normalised (dRp1_n uses /800, dL1_n uses /2000,
    others are dimensionless ratios), so equal weights are appropriate.
    """
    keys = ("dRp1_n", "k1", "k2", "dL1_n", "df_n", "df1_n")
    dists = sorted(
        math.sqrt(sum((v[k] - c[k]) ** 2 for k in keys))
        for v in vectors
    )
    idx = min(int(len(dists) * 0.95), len(dists) - 1)
    return round(dists[idx], 4)


def make_entry(
    group_key: str,
    metal_code: str,
    coin_name: str,
    session_tag: str,
    vectors: list[dict],
) -> dict:
    c = centroid_6d(vectors)
    return {
        "id":            f"{group_key}/{session_tag}",
        "protocol_id":   PROTOCOL_ID,
        "metal_code":    metal_code,
        "coin_name":     coin_name,
        "year":          None,
        "centroid":      c,
        "radius_95pct":  radius_95pct(vectors, c),
        "records_count": len(vectors),
    }


# ── C-7 session group metadata — REMOVED (C-7 excluded, see SESSIONS comment) ──


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    print("CoinTrace gen-4 DB builder")
    print(f"  Input dir : {NDJSON_DIR}")
    print(f"  Output    : {OUTPUT}")
    print()

    # ── Step 1: Load all sessions ────────────────────────────────────────────
    all_records: list[dict] = []   # augmented with _session, _group_key fields

    for fname in SESSIONS:
        path = NDJSON_DIR / fname
        if not path.exists():
            print(f"  [WARN] Missing session file: {fname}", file=sys.stderr)
            continue
        recs = load_ndjson(path)
        is_c8 = "session_cf1edfcd" in fname

        for rec in recs:
            idx = rec["index"]

            if is_c8:
                # ── Step 2: C-8 remap ────────────────────────────────────────
                mapping = C8_REMAP.get(idx)
                if mapping is None:
                    continue   # excluded (drift outlier idx=0)
                metal_code, coin_name, group_key = mapping
                rec["_metal_code"]  = metal_code
                rec["_coin_name"]   = coin_name
                rec["_group_key"]   = group_key
                rec["_session_tag"] = "c8_hw_2026-04-03"
            else:
                # Should not happen — only C-8 in SESSIONS — skip defensively
                continue

            all_records.append(rec)

    print(f"  Loaded {len(all_records)} candidate records (before quality filters)")

    # ── Step 3: Calibrate baseFS from records with known df_n ────────────────
    # df_n = (fSensor_coin - fSensor_empty) / fSensor_empty
    # → fSensor_empty = fSensor_step0 / (1 + df_n)
    bfs_samples = [
        compute_basefs(r)
        for r in all_records
        if "df_n" in r.get("production_vector", {})
    ]
    if not bfs_samples:
        print("ERROR: No records with df_n — cannot calibrate baseFS.", file=sys.stderr)
        sys.exit(1)

    calibrated_bfs = statistics.median(bfs_samples)
    print(f"  Calibrated baseFS: {calibrated_bfs:.1f} Hz"
          f"  (n={len(bfs_samples)}, "
          f"min={min(bfs_samples):.1f}, max={max(bfs_samples):.1f})")

    # ── Steps 4+5: Compute features, apply quality filters ───────────────────
    groups: dict[str, list[dict]] = {}   # group_key → list of feature dicts
    group_meta: dict[str, tuple[str, str, str]] = {}   # group_key → (metal_code, coin_name, session_tag)

    excluded_drift  = 0
    excluded_unkn   = 0
    accepted        = 0

    for rec in all_records:
        if rec["_metal_code"] == "UNKN":
            excluded_unkn += 1
            continue

        d = drift_ratio(rec)
        if d > DRIFT_THRESHOLD:
            excluded_drift += 1
            print(f"  [DRIFT] {rec['_group_key']} idx={rec['index']} "
                  f"drift={d*100:.1f}% > {DRIFT_THRESHOLD*100:.0f}% → excluded")
            continue

        feats = compute_features(rec, calibrated_bfs)
        gk = rec["_group_key"]

        if gk not in groups:
            groups[gk] = []
            group_meta[gk] = (rec["_metal_code"], rec["_coin_name"], rec["_session_tag"])

        groups[gk].append(feats)
        accepted += 1

    print(f"\n  Records: accepted={accepted}  "
          f"excluded_drift={excluded_drift}  excluded_unkn={excluded_unkn}")

    # ── Step 6+7: Build DB entries ────────────────────────────────────────────
    entries: list[dict] = []

    print()
    for gk in sorted(groups.keys()):
        vecs  = groups[gk]
        metal, coin, tag = group_meta[gk]
        entry = make_entry(gk, metal, coin, tag, vecs)
        entries.append(entry)
        c = entry["centroid"]
        print(f"  {gk:20s}  n={len(vecs):2d}  "
              f"dRp1_n={c['dRp1_n']:+7.4f}  "
              f"df_n={c['df_n']:.4f}  "
              f"df1_n={c['df1_n']:.4f}  "
              f"r95={entry['radius_95pct']:.4f}")

    # ── Step 8: Carry-forward legacy gen-3 entries ────────────────────────────
    print()
    for entry in GEN3_CARRY_FORWARD:
        entries.append(entry)
        c = entry["centroid"]
        print(f"  {entry['metal_code']:20s}  n={entry['records_count']:2d}  "
              f"dRp1_n={c['dRp1_n']:+7.4f}  "
              f"df_n={c['df_n']:.4f}  "
              f"df1_n={c['df1_n']:.4f}  "
              f"r95={entry['radius_95pct']:.4f}  [GEN3 CARRY-FORWARD]")

    # ── Step 9: Write index.json ──────────────────────────────────────────────
    now_iso = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    db = {
        "version":      4,
        "generated_at": now_iso,
        "generation":   4,
        "session_id":   "cf1edfcd",
        "protocols":    [PROTOCOL_ID],
        "entries":      entries,
    }

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    with OUTPUT.open("w", encoding="utf-8") as fh:
        json.dump(db, fh, indent=2, ensure_ascii=False)
        fh.write("\n")

    print(f"\n  Written {len(entries)} entries → {OUTPUT}")
    print("  Done.")


if __name__ == "__main__":
    main()
