#!/usr/bin/env python3
"""
CoinTrace gen-5 database builder
==================================
Inputs:  docs/external/С-10/2026-04-04.C10_unified.ndjson   (C-10/C-11 unified)
         docs/external/2026-04-03.8.session_cf1edfcd.ndjson  (C-8, Silver classes)
Output:  data/sd_seed/CoinTrace/database/index.json  (gen-5)

Changes vs gen-4
----------------
- C-10/C-11 re-seeds: XUSSR10, XKENNED, XAG900, XCUZN, XFE, XAL, XCU, XZNNIP
  (replaces C-7 carry-forwards with real df1_n values)
- New classes: XFENIP (Slovakia 2 Koruny, steel+Ni-plated) [needs_reseed=True, r95 wide]
               XFECUP (Germany 1 Pfennig, steel+Cu-plated)
               XNICKEL (South Africa 10 Cents, pure Ni)
- XAG900 df1_n is now real (≈0.47)
- Kennedy merge: XKENNEDY C-8 (10 rec) + XKENNED C-10 (10 rec) → single XKENNED (20 rec)
  (centroids overlap in 6D wdist; same Ag400 coin, same metal_code)
- XTEST records excluded from DB (_test_only=True)
- C-8 Silver classes (XAG999, XAG800, XUSSR10R) carried forward from gen-4
  (XKENNEDY merged with C-10; XUSSR10R superseded by C-10 re-seed)

Algorithm
---------
1. Load C-8 session → compute features for Silver classes (carry-forward logic).
2. Load C-10 unified NDJSON → use metal_code/coin_name directly (already relabeled).
3. Filter: skip _test_only, skip drift > DRIFT_THRESHOLD.
4. Group by (metal_code, coin_name) into DB entries.
5. Compute 6D centroid + radius_95pct per entry.
6. Write index.json gen-5.
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
OUTPUT       = REPO_ROOT / "data" / "sd_seed" / "CoinTrace" / "database" / "index.json"

PROTOCOL_ID      = "p3_MIKROE3240_b06_012mm"
DRIFT_THRESHOLD  = 0.05   # 5% — matches VectorCompute::DRIFT_THRESHOLD

# ── C-8 label remap (Silver classes only — same as gen-4) ────────────────────
C8_REMAP: dict[int, tuple[str, str, str] | None] = {
    0:  None,
    1:  ("XAG999",   "American Silver Eagle 1oz (Side A)",  "xag999_eagle_a"),
    2:  ("XAG999",   "American Silver Eagle 1oz (Side A)",  "xag999_eagle_a"),
    3:  ("XAG999",   "American Silver Eagle 1oz (Side A)",  "xag999_eagle_a"),
    4:  ("XAG999",   "American Silver Eagle 1oz (Side A)",  "xag999_eagle_a"),
    5:  ("XAG999",   "American Silver Eagle 1oz (Side B)",  "xag999_eagle_b"),
    6:  ("XAG999",   "American Silver Eagle 1oz (Side B)",  "xag999_eagle_b"),
    7:  ("XAG999",   "American Silver Eagle 1oz (Side B)",  "xag999_eagle_b"),
    8:  ("XAG999",   "American Silver Eagle 1oz (Side B)",  "xag999_eagle_b"),
    9:  ("XAG999",   "American Silver Eagle 1oz (Side B)",  "xag999_eagle_b"),
    10: ("XKENNED",  "Kennedy Half Dollar Ag400 (Side A)",  "xkennedy_c8_a"),
    11: ("XKENNED",  "Kennedy Half Dollar Ag400 (Side A)",  "xkennedy_c8_a"),
    12: ("XKENNED",  "Kennedy Half Dollar Ag400 (Side A)",  "xkennedy_c8_a"),
    13: ("XKENNED",  "Kennedy Half Dollar Ag400 (Side A)",  "xkennedy_c8_a"),
    14: ("XKENNED",  "Kennedy Half Dollar Ag400 (Side A)",  "xkennedy_c8_a"),
    15: ("XKENNED",  "Kennedy Half Dollar Ag400 (Side B)",  "xkennedy_c8_b"),
    16: ("XKENNED",  "Kennedy Half Dollar Ag400 (Side B)",  "xkennedy_c8_b"),
    17: ("XKENNED",  "Kennedy Half Dollar Ag400 (Side B)",  "xkennedy_c8_b"),
    18: ("XKENNED",  "Kennedy Half Dollar Ag400 (Side B)",  "xkennedy_c8_b"),
    19: ("XKENNED",  "Kennedy Half Dollar Ag400 (Side B)",  "xkennedy_c8_b"),
    20: ("XAG999",   "Australian Kangaroo 1oz (Side A)",    "xag999_kang_a"),
    21: ("XAG999",   "Australian Kangaroo 1oz (Side A)",    "xag999_kang_a"),
    22: ("XAG999",   "Australian Kangaroo 1oz (Side A)",    "xag999_kang_a"),
    23: ("XAG999",   "Australian Kangaroo 1oz (Side A)",    "xag999_kang_a"),
    24: ("XAG999",   "Australian Kangaroo 1oz (Side A)",    "xag999_kang_a"),
    25: ("XAG999",   "Australian Kangaroo 1oz (Side B)",    "xag999_kang_b"),
    26: ("XAG999",   "Australian Kangaroo 1oz (Side B)",    "xag999_kang_b"),
    27: ("XAG999",   "Australian Kangaroo 1oz (Side B)",    "xag999_kang_b"),
    28: ("XAG999",   "Australian Kangaroo 1oz (Side B)",    "xag999_kang_b"),
    29: ("XAG999",   "Australian Kangaroo 1oz (Side B)",    "xag999_kang_b"),
    30: ("XAG800",   "Bahamas $1 Ag800 (Side A)",           "xag800_bhs_a"),
    31: ("XAG800",   "Bahamas $1 Ag800 (Side A)",           "xag800_bhs_a"),
    32: ("XAG800",   "Bahamas $1 Ag800 (Side A)",           "xag800_bhs_a"),
    33: ("XAG800",   "Bahamas $1 Ag800 (Side A)",           "xag800_bhs_a"),
    34: ("XAG800",   "Bahamas $1 Ag800 (Side A)",           "xag800_bhs_a"),
    35: ("XAG800",   "Bahamas $1 Ag800 (Side B)",           "xag800_bhs_b"),
    36: ("XAG800",   "Bahamas $1 Ag800 (Side B)",           "xag800_bhs_b"),
    37: ("XAG800",   "Bahamas $1 Ag800 (Side B)",           "xag800_bhs_b"),
    38: ("XAG800",   "Bahamas $1 Ag800 (Side B)",           "xag800_bhs_b"),
    39: ("XAG800",   "Bahamas $1 Ag800 (Side B)",           "xag800_bhs_b"),
    40: ("XUSSR10R", "USSR 10 Rubles Ag900 (Side A)",       "xussr10r_c8_a"),
    41: ("XUSSR10R", "USSR 10 Rubles Ag900 (Side A)",       "xussr10r_c8_a"),
    42: ("XUSSR10R", "USSR 10 Rubles Ag900 (Side A)",       "xussr10r_c8_a"),
    43: ("XUSSR10R", "USSR 10 Rubles Ag900 (Side A)",       "xussr10r_c8_a"),
    44: ("XUSSR10R", "USSR 10 Rubles Ag900 (Side A)",       "xussr10r_c8_a"),
    45: ("XUSSR10R", "USSR 10 Rubles Ag900 (Side B)",       "xussr10r_c8_b"),
    46: ("XUSSR10R", "USSR 10 Rubles Ag900 (Side B)",       "xussr10r_c8_b"),
    47: ("XUSSR10R", "USSR 10 Rubles Ag900 (Side B)",       "xussr10r_c8_b"),
    48: ("XUSSR10R", "USSR 10 Rubles Ag900 (Side B)",       "xussr10r_c8_b"),
    49: ("XUSSR10R", "USSR 10 Rubles Ag900 (Side B)",       "xussr10r_c8_b"),
}

# C-8 Silver groups carried forward (or merged) into gen-5.
# XUSSR10R from C-8 is DROPPED — superseded by C-10 re-seed with 10+10.
# Kennedy groups (xkennedy_c8_a/b) are kept here then merged with C-10 in PART C.
C8_SILVER_GROUPS = {"xag999_eagle_a", "xag999_eagle_b",
                    "xag999_kang_a",  "xag999_kang_b",
                    "xag800_bhs_a",   "xag800_bhs_b",
                    "xkennedy_c8_a",  "xkennedy_c8_b"}


# ── C-10/C-11 group key mapping ───────────────────────────────────────────────
# Maps coin_name → group_key used in the output entry id.
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
    """Extract 6D feature vector directly from production_vector (C-10 unified)."""
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
    print("CoinTrace gen-5 DB builder")
    print(f"  C-8 session : {C8_SESSION.name}")
    print(f"  C-10 unified: {C10_UNIFIED.name}")
    print(f"  Output      : {OUTPUT}")
    print()

    # ══════════════════════════════════════════════════════════════════════════
    # PART A — C-8 Silver classes
    # ══════════════════════════════════════════════════════════════════════════
    if not C8_SESSION.exists():
        print(f"ERROR: {C8_SESSION} not found", file=sys.stderr)
        sys.exit(1)

    c8_recs = load_ndjson(C8_SESSION)
    bfs_samples = [compute_basefs(r) for r in c8_recs
                   if "df_n" in r.get("production_vector", {})]
    calibrated_bfs = statistics.median(bfs_samples)
    print(f"  C-8 baseFS: {calibrated_bfs:.1f} Hz  (n={len(bfs_samples)})")

    c8_groups: dict[str, list[dict]] = {}
    c8_meta:   dict[str, tuple[str, str]] = {}   # gk → (metal_code, coin_name)
    c8_excl_drift = 0

    for rec in c8_recs:
        idx = rec["index"]
        mapping = C8_REMAP.get(idx)
        if mapping is None:
            continue
        metal_code, coin_name, gk = mapping
        if gk not in C8_SILVER_GROUPS:
            continue   # XUSSR10R from C-8 is superseded by C-10

        d = drift_ratio(rec)
        if d > DRIFT_THRESHOLD:
            c8_excl_drift += 1
            print(f"  [DRIFT C8] {gk} idx={idx} drift={d*100:.1f}% -> excluded")
            continue

        feats = compute_features_c8(rec, calibrated_bfs)
        if gk not in c8_groups:
            c8_groups[gk] = []
            c8_meta[gk] = (metal_code, coin_name)
        c8_groups[gk].append(feats)

    print(f"  C-8 drift excluded: {c8_excl_drift}")

    # ══════════════════════════════════════════════════════════════════════════
    # PART B — C-10/C-11 unified
    # ══════════════════════════════════════════════════════════════════════════
    if not C10_UNIFIED.exists():
        print(f"ERROR: {C10_UNIFIED} not found", file=sys.stderr)
        sys.exit(1)

    c10_recs = load_ndjson(C10_UNIFIED)
    print(f"  C-10 unified loaded: {len(c10_recs)} records")

    c10_groups: dict[str, list[dict]] = {}
    c10_meta:   dict[str, tuple[str, str]] = {}
    c10_excl_test  = 0
    c10_excl_drift = 0

    for rec in c10_recs:
        if rec.get("_test_only"):
            c10_excl_test += 1
            continue

        d = drift_ratio(rec)
        if d > DRIFT_THRESHOLD:
            c10_excl_drift += 1
            print(f"  [DRIFT C10] {rec.get('metal_code')} "
                  f"sess={rec.get('source_session','')} "
                  f"idx={rec.get('index')} drift={d*100:.1f}% -> excluded")
            continue

        coin_name  = rec["coin_name"]
        metal_code = rec["metal_code"]
        gk = C10_GROUP_KEYS.get(coin_name)
        if gk is None:
            # Fallback: derive key from metal_code + side
            side = "a" if "Side A" in coin_name else "b"
            gk = f"{metal_code.lower()}_c10_{side}"

        feats = features_from_pv(rec)
        if gk not in c10_groups:
            c10_groups[gk] = []
            c10_meta[gk] = (metal_code, coin_name)
        c10_groups[gk].append(feats)

    print(f"  C-10 test excluded: {c10_excl_test}  "
          f"drift excluded: {c10_excl_drift}")

    # ══════════════════════════════════════════════════════════════════════════
    # PART B.5 — Kennedy merge: C-8 (5+5) + C-10 (5+5) → XKENNED (10+10)
    # Centroids overlap in 6D wdist (wdist_A=0.15, wdist_B=0.07 vs r95 sums
    # 0.19 / 0.15) — same Ag400 coin, merge removes dual-candidate ambiguity.
    # ══════════════════════════════════════════════════════════════════════════
    KEN_MERGE = {"xkennedy_c8_a": "xkenned_c10_a",
                 "xkennedy_c8_b": "xkenned_c10_b"}
    for c8_gk, c10_gk in KEN_MERGE.items():
        c8_vecs  = c8_groups.pop(c8_gk, [])
        c10_vecs = c10_groups.get(c10_gk, [])
        if c8_vecs and c10_gk in c10_groups:
            c10_groups[c10_gk] = c8_vecs + c10_vecs
            print(f"  [MERGE] {c8_gk} (n={len(c8_vecs)}) + "
                  f"{c10_gk} (n={len(c10_vecs)}) -> XKENNED n={len(c10_groups[c10_gk])}")

    # ══════════════════════════════════════════════════════════════════════════
    # PART C — Build entries
    # ══════════════════════════════════════════════════════════════════════════
    entries: list[dict] = []

    print("\n─── C-8 Silver classes (carry-forward) ───────────────────────────")
    for gk in sorted(c8_groups.keys()):
        metal, coin = c8_meta[gk]
        entry = make_entry(f"{gk}/c8_hw_2026-04-03", metal, coin, c8_groups[gk])
        entries.append(entry)
        c = entry["centroid"]
        print(f"  {gk:22s}  n={len(c8_groups[gk]):2d}  "
              f"df_n={c['df_n']:.4f}  df1_n={c['df1_n']:.4f}  "
              f"r95={entry['radius_95pct']:.4f}")

    print("\n─── C-10/C-11 re-seed + new classes ──────────────────────────────")
    for gk in sorted(c10_groups.keys()):
        metal, coin = c10_meta[gk]
        entry = make_entry(f"{gk}/c10_hw_2026-04-04", metal, coin, c10_groups[gk])
        if metal == "XFENIP":
            entry["needs_reseed"] = True
        entries.append(entry)
        c = entry["centroid"]
        tag = ""
        if metal == "XFENIP":
            tag = "  <- NEW [needs_reseed: drift-limited n=5]"
        elif metal in ("XFECUP", "XNICKEL"):
            tag = "  <- NEW"
        elif metal in ("XFE", "XAL", "XCU", "XZNNIP", "XCUZN", "XAG900"):
            tag = "  <- re-seed (was C-7 placeholder)"
        elif metal == "XKENNED":
            tag = f"  <- re-seed + C-8 merge (n={len(c10_groups[gk])})"
        elif metal == "XUSSR10":
            tag = "  <- re-seed"
        print(f"  {gk:22s}  n={len(c10_groups[gk]):2d}  "
              f"df_n={c['df_n']:.4f}  df1_n={c['df1_n']:.4f}  "
              f"r95={entry['radius_95pct']:.4f}{tag}")

    # ── Write ─────────────────────────────────────────────────────────────────
    now_iso = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    db = {
        "version":      5,
        "generated_at": now_iso,
        "generation":   5,
        "session_id":   "c10_c11_unified",
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
    print("\n  Classes in gen-5 DB:")
    for mc in sorted(by_metal.keys()):
        sides = len(by_metal[mc])
        n_total = sum(e["records_count"] for e in by_metal[mc])
        r95_max = max(e["radius_95pct"] for e in by_metal[mc])
        df1_mean = statistics.mean(
            e["centroid"]["df1_n"] for e in by_metal[mc])
        print(f"  {mc:<12}  {sides} side(s)  n={n_total:3d}  "
              f"df1_n≈{df1_mean:.4f}  r95_max={r95_max:.4f}")

    print(f"\n  Written -> {OUTPUT}")
    print("  Done.")


if __name__ == "__main__":
    main()
