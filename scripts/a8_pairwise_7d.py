#!/usr/bin/env python3
"""
A-8: Pairwise separation analysis — gen-8 DB  (7D vector with mass_n)
========================================================================
Wave 10 exit criterion: minimum pairwise 7D distance > 2.0σ for ALL class pairs.

Extends A-7 (gen-5/7, 6D) to 7D by adding mass_n as the 7th dimension.

7D key ordering: [dRp1_n, k1, k2, df_n, dL1_n, df1_n, mass_n]
                  (matches FingerprintCache::query() positional order + W_mass at index 6)

Behavior for entries with mass_n = -1.0 (SENTINEL — gen-7 or partially migrated DB):
  - These entries are treated as 6D (mass_n dimension contributes 0 to distance).
  - A warning is printed for any sentinel entry found.
  - Full 7D analysis requires gen-8 DB (all entries must have mass_n ≥ 0.0).

Usage:
    python scripts/a8_pairwise_7d.py                    # uses gen-8 DB + matcher.json
    python scripts/a8_pairwise_7d.py --threshold 2.0    # explicit threshold (default: 2.0)
    python scripts/a8_pairwise_7d.py --also-6d          # show 6D distances alongside 7D
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

# Fix Windows console encoding for Unicode output (sigma, arrows, etc.)
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

REPO_ROOT    = Path(__file__).resolve().parent.parent
DB_PATH      = REPO_ROOT / "data" / "sd_seed" / "CoinTrace" / "database" / "index.json"
MATCHER_PATH = REPO_ROOT / "data" / "sd_seed" / "CoinTrace" / "matcher.json"

KEYS_7D = ("dRp1_n", "k1", "k2", "df_n", "dL1_n", "df1_n", "mass_n")
KEYS_6D = ("dRp1_n", "k1", "k2", "df_n", "dL1_n", "df1_n")
SENTINEL = -1.0


def load(threshold: float) -> tuple[list[dict], list[float], list[float], float]:
    db      = json.loads(DB_PATH.read_text(encoding="utf-8"))
    matcher = json.loads(MATCHER_PATH.read_text(encoding="utf-8"))
    fw = matcher["full_weights"]   # 7 elements for gen-8 matcher v6
    # Pad to 7 elements if running against older matcher.json (6D)
    while len(fw) < 7:
        fw.append(0.0)
    fw6 = fw[:6]
    fw7 = fw[:7]
    sigma = matcher["sigma"]
    return db["entries"], fw6, fw7, sigma


def wdist_7d(a: dict, b: dict, weights: list[float]) -> float:
    """7D weighted Euclidean distance. Sentinel entries contribute w[6]=0 implicitly."""
    total = 0.0
    for i, k in enumerate(KEYS_7D):
        va = a.get(k, SENTINEL)
        vb = b.get(k, SENTINEL)
        # If either side has no mass measurement, skip mass dimension (6D fallback)
        if k == "mass_n" and (va == SENTINEL or vb == SENTINEL):
            continue
        total += weights[i] * (va - vb) ** 2
    return math.sqrt(total)


def wdist_6d(a: dict, b: dict, weights: list[float]) -> float:
    return math.sqrt(sum(weights[i] * (a.get(k, 0) - b.get(k, 0)) ** 2
                         for i, k in enumerate(KEYS_6D)))


def dominant_dim_7d(a: dict, b: dict, weights: list[float]) -> str:
    contribs: dict[str, float] = {}
    for i, k in enumerate(KEYS_7D):
        va = a.get(k, SENTINEL)
        vb = b.get(k, SENTINEL)
        if k == "mass_n" and (va == SENTINEL or vb == SENTINEL):
            continue
        contribs[k] = weights[i] * (va - vb) ** 2
    total = sum(contribs.values()) or 1.0
    best_k = max(contribs, key=contribs.__getitem__)
    return f"{best_k} ({100 * contribs[best_k] / total:.0f}%)"


def main() -> None:
    parser = argparse.ArgumentParser(description="A-8: 7D pairwise separation analysis")
    parser.add_argument("--threshold", type=float, default=2.0,
                        help="Sigma threshold for exit criterion (default: 2.0)")
    parser.add_argument("--also-6d", action="store_true",
                        help="Print 6D distance alongside 7D for comparison")
    args = parser.parse_args()

    entries, fw6, fw7, sigma = load(args.threshold)

    all_codes = sorted({e["metal_code"] for e in entries})
    n = len(all_codes)

    # Group entries by metal_code
    by_mc: dict[str, list[dict]] = {mc: [] for mc in all_codes}
    for e in entries:
        centroid = dict(e["centroid"])   # copy so we can check sentinel
        # Propagate sentinel if no mass_n in centroid at all
        if "mass_n" not in centroid:
            centroid["mass_n"] = SENTINEL
        by_mc[e["metal_code"]].append(centroid)

    # ── Sentinel check ───────────────────────────────────────────────────────
    sentinel_entries = [
        (e["metal_code"], e["id"])
        for e in entries
        if e["centroid"].get("mass_n", SENTINEL) == SENTINEL
    ]
    has_sentinels = bool(sentinel_entries)

    # ── Print header ─────────────────────────────────────────────────────────
    db_meta    = json.loads(DB_PATH.read_text(encoding="utf-8"))
    match_meta = json.loads(MATCHER_PATH.read_text(encoding="utf-8"))
    print("A-8 Pairwise separation analysis — 7D vector  (per-side entries)")
    print(f"  DB version   : {db_meta['version']}")
    print(f"  DB generation: {db_meta.get('generation', '?')}")
    print(f"  Matcher v    : {match_meta['version']}")
    print(f"  Classes (n)  : {n}")
    print(f"  Entries      : {len(entries)}")
    print(f"  sigma        : {sigma}")
    print(f"  full_weights : {fw7}")
    print(f"  7D keys      : {list(KEYS_7D)}")
    print(f"  Threshold    : {args.threshold}σ (Wave 10 exit criterion)")
    if has_sentinels:
        print(f"  [WARN] {len(sentinel_entries)} entries have mass_n=SENTINEL (6D fallback for those pairs):")
        for mc, eid in sentinel_entries:
            print(f"         {mc}: {eid}")
    print()

    # ── Pairwise minimum distances ────────────────────────────────────────────
    pairs: list[tuple] = []
    for i in range(n):
        for j in range(i + 1, n):
            mc_a, mc_b = all_codes[i], all_codes[j]
            best_d7 = float("inf")
            best_d6 = float("inf")
            best_ea = best_eb = None
            for ea in by_mc[mc_a]:
                for eb in by_mc[mc_b]:
                    d7 = wdist_7d(ea, eb, fw7)
                    d6 = wdist_6d(ea, eb, fw6)
                    if d7 < best_d7:
                        best_d7, best_d6, best_ea, best_eb = d7, d6, ea, eb
            pairs.append((best_d7, best_d7 / sigma, best_d6, best_d6 / sigma,
                           mc_a, mc_b, best_ea, best_eb))

    pairs.sort(key=lambda x: x[0])

    # ── Print table ───────────────────────────────────────────────────────────
    THRESHOLD = args.threshold

    col_6d = "  6D-σ" if args.also_6d else ""
    header = (f"  {'Pair':<28} {'wdist7':>7} {'7D-σ':>6}"
              f"{col_6d:>8}  {'r95_a':>7}  {'r95_b':>7}  {'dominant dim':<28}  Verdict")
    print(header)
    print("  " + "-" * (len(header) - 2 + (8 if args.also_6d else 0)))

    fail_count    = 0
    warning_count = 0

    # Collect r95 by metal_code → min per class (for display)
    r95_by_mc: dict[str, float] = {}
    for e in entries:
        mc = e["metal_code"]
        r95 = e.get("radius_95pct", 0.0)
        r95_by_mc[mc] = min(r95_by_mc.get(mc, float("inf")), r95)

    for d7, s7, d6, s6, mc_a, mc_b, ea, eb in pairs:
        r95_a = r95_by_mc.get(mc_a, 0.0)
        r95_b = r95_by_mc.get(mc_b, 0.0)
        pair_str = f"{mc_a} ↔ {mc_b}"
        dom = dominant_dim_7d(ea, eb, fw7)

        mass_a = ea.get("mass_n", SENTINEL)
        mass_b = eb.get("mass_n", SENTINEL)
        sentinel_flag = "(6D)" if (mass_a == SENTINEL or mass_b == SENTINEL) else "     "

        if s7 < THRESHOLD:
            verdict = f"❌ FAIL {sentinel_flag}"
            fail_count += 1
        elif s7 < THRESHOLD * 1.5:
            verdict = f"⚠️  WARN {sentinel_flag}"
            warning_count += 1
        else:
            verdict = f"✅ OK   {sentinel_flag}"

        col_6d_val = f"  {s6:>5.2f}σ" if args.also_6d else ""
        print(f"  {pair_str:<28} {d7:>7.4f} {s7:>5.2f}σ"
              f"{col_6d_val}  {r95_a:>7.4f}  {r95_b:>7.4f}  {dom:<28}  {verdict}")

    # ── Summary ───────────────────────────────────────────────────────────────
    total_pairs = len(pairs)
    pass_count  = total_pairs - fail_count - warning_count

    print()
    print(f"  Total pairs   : {total_pairs}")
    print(f"  ✅ OK (≥{THRESHOLD}σ)  : {pass_count}")
    print(f"  ⚠️  WARN (≥{THRESHOLD}σ)  : {warning_count}")
    print(f"  ❌ FAIL (<{THRESHOLD}σ)  : {fail_count}")
    print()

    if fail_count == 0 and not has_sentinels:
        print(f"  ✅ WAVE 10 EXIT CRITERION MET — all {total_pairs} pairs ≥ {THRESHOLD}σ in 7D")
    elif fail_count == 0 and has_sentinels:
        print(f"  ⚠️  All passing pairs ≥ {THRESHOLD}σ BUT {len(sentinel_entries)} entries still in 6D mode.")
        print("      Complete gen-8 DB (C-14) to get full 7D analysis.")
    else:
        print(f"  ❌ Wave 10 exit criterion NOT met — {fail_count} pair(s) below {THRESHOLD}σ in 7D.")
        print("     Analysis:")
        for d7, s7, d6, s6, mc_a, mc_b, ea, eb in pairs:
            if d7 / sigma < THRESHOLD:
                mass_a = ea.get("mass_n", SENTINEL)
                mass_b = eb.get("mass_n", SENTINEL)
                dm_g = abs(mass_a - mass_b) * 33.3 if (mass_a != SENTINEL and mass_b != SENTINEL) else "?"
                print(f"       {mc_a} ↔ {mc_b}: {s7:.2f}σ  Δmass_g={dm_g if isinstance(dm_g, str) else f'{dm_g:.1f}'}")
                print(f"         Suggestion: verify mass measurements, check for class overlap")


if __name__ == "__main__":
    main()
