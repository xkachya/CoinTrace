#!/usr/bin/env python3
"""
A-7: Pairwise separation analysis — gen-5 DB  (v2 — per-side minimum distance)
================================================================================
Computes the minimum weighted 6D distance between all class pairs, using
individual DB entries (per coin side) rather than averaged class centroids.

Why per-side entries?  FingerprintCache::query() compares the measurement
vector against ALL 28 individual side-entries, not against class centroids.
The minimum inter-entry distance is the true separation floor that matters
for hardware misclassification risk.

Key ordering: [dRp1_n, k1, k2, df_n, dL1_n, df1_n]  (ADR-VEC-002, FingerprintCache.cpp)

Wave 9 exit criterion: min pairwise distance > 1.0 sigma for ALL class pairs.
"""

from __future__ import annotations

import json
import math
from pathlib import Path

REPO_ROOT    = Path(__file__).resolve().parent.parent
DB_PATH      = REPO_ROOT / "data" / "sd_seed" / "CoinTrace" / "database" / "index.json"
MATCHER_PATH = REPO_ROOT / "data" / "sd_seed" / "CoinTrace" / "matcher.json"

# Matches FingerprintCache::query() positional argument order (ADR-VEC-002):
#   query(dRp1_n, k1, k2, df_n, dL1_n, df1_n, results, maxResults, weights)
KEYS = ("dRp1_n", "k1", "k2", "df_n", "dL1_n", "df1_n")


def load() -> tuple[list[dict], list[float], float]:
    db      = json.loads(DB_PATH.read_text(encoding="utf-8"))
    matcher = json.loads(MATCHER_PATH.read_text(encoding="utf-8"))
    weights = matcher["full_weights"]   # positional: [w_dRp1_n, w_k1, w_k2, w_df_n, w_dL1_n, w_df1_n]
    sigma   = matcher["sigma"]
    return db["entries"], weights, sigma


def wdist(a: dict, b: dict, weights: list[float]) -> float:
    """Weighted Euclidean distance between two centroid dicts."""
    return math.sqrt(sum(w * (a[k] - b[k]) ** 2 for k, w in zip(KEYS, weights)))


def dominant_dimension(a: dict, b: dict, weights: list[float]) -> str:
    contribs = {k: weights[i] * (a[k] - b[k]) ** 2 for i, k in enumerate(KEYS)}
    total = sum(contribs.values()) or 1.0
    best_k = max(contribs, key=contribs.__getitem__)
    return f"{best_k} ({100 * contribs[best_k] / total:.0f}%)"


def main() -> None:
    entries, weights, sigma = load()

    all_codes = sorted({e["metal_code"] for e in entries})
    n = len(all_codes)

    # Group entries by metal_code (each entry = one coin side)
    by_mc: dict[str, list[dict]] = {mc: [] for mc in all_codes}
    for e in entries:
        by_mc[e["metal_code"]].append(e)

    # ── Minimum per-side pairwise distances ──────────────────────────────────
    # For each (mc_a, mc_b): find the closest entry pair (one from each class).
    # PairInfo: (dist, sigma_val, mc_a, mc_b, entry_a, entry_b)
    pairs: list[tuple] = []
    for i in range(n):
        for j in range(i + 1, n):
            mc_a, mc_b = all_codes[i], all_codes[j]
            best_d  = float("inf")
            best_ea = best_eb = None
            for ea in by_mc[mc_a]:
                for eb in by_mc[mc_b]:
                    d = wdist(ea["centroid"], eb["centroid"], weights)
                    if d < best_d:
                        best_d, best_ea, best_eb = d, ea, eb
            pairs.append((best_d, best_d / sigma, mc_a, mc_b, best_ea, best_eb))

    pairs.sort(key=lambda x: x[0])

    # ── Print header ─────────────────────────────────────────────────────────
    db_meta    = json.loads(DB_PATH.read_text(encoding="utf-8"))
    match_meta = json.loads(MATCHER_PATH.read_text(encoding="utf-8"))
    print("A-7 Pairwise separation analysis — gen-5 DB  (per-side entries, v2)")
    print(f"  DB version  : {db_meta['version']}")
    print(f"  Matcher v   : {match_meta['version']}")
    print(f"  Classes (n) : {n}")
    print(f"  sigma       : {sigma}")
    print(f"  weights     : {weights}")
    print(f"  6D keys     : {list(KEYS)}")
    print()

    THRESHOLD_SIGMA = 1.0

    print(f"  {'Pair':<28} {'wdist':>7} {'sigma':>7}  {'r95_a':>7}  {'r95_b':>7}  {'r95_sum':>8}  {'dominant dim':<25}  Verdict")
    print("  " + "-" * 115)

    alarm_count   = 0
    warning_count = 0

    for d, d_sigma, mc_a, mc_b, ea, eb in pairs:
        r95_a   = ea["radius_95pct"]
        r95_b   = eb["radius_95pct"]
        r95_sum = r95_a + r95_b
        dom     = dominant_dimension(ea["centroid"], eb["centroid"], weights)
        overlap = d < r95_sum

        if d_sigma < THRESHOLD_SIGMA:
            verdict = "❌ < 1.0 sigma"
            alarm_count += 1
        elif overlap:
            verdict = "⚠️  wdist < r95_sum"
            warning_count += 1
        elif d_sigma < 2.0:
            verdict = "⚠️  < 2.0 sigma"
            warning_count += 1
        else:
            verdict = "✅"

        pair_label = f"{mc_a} ↔ {mc_b}"
        print(f"  {pair_label:<28} {d:>7.4f}  {d_sigma:>6.2f}σ  "
              f"{r95_a:>7.4f}  {r95_b:>7.4f}  {r95_sum:>8.4f}  {dom:<25}  {verdict}")

    # ── Per-class nearest neighbour ──────────────────────────────────────────
    print()
    print("── Nearest neighbour per class (min dist to any entry of other class) ───")
    print(f"  {'Class':<12} {'Nearest':<14} {'wdist':>7}  {'sigma':>7}  {'r95_self':>9}  {'r95_nn':>8}")
    print("  " + "-" * 75)

    for mc in all_codes:
        best_d = float("inf")
        best_nn = ""
        best_r95_self = best_r95_nn = 0.0
        for ea in by_mc[mc]:
            for other_mc in all_codes:
                if other_mc == mc:
                    continue
                for eb in by_mc[other_mc]:
                    d = wdist(ea["centroid"], eb["centroid"], weights)
                    if d < best_d:
                        best_d        = d
                        best_nn       = other_mc
                        best_r95_self = ea["radius_95pct"]
                        best_r95_nn   = eb["radius_95pct"]
        d_sigma = best_d / sigma
        flag = " ❌" if d_sigma < THRESHOLD_SIGMA else (" ⚠️" if d_sigma < 2.0 else "")
        print(f"  {mc:<12} {best_nn:<14} {best_d:>7.4f}  {d_sigma:>6.2f}σ  "
              f"{best_r95_self:>9.4f}  {best_r95_nn:>8.4f}{flag}")

    # ── Feature contribution breakdown for the 5 closest pairs ─────────────
    print()
    print("── Top-5 closest pairs — per-dimension breakdown ──────────────────────────")
    for d, d_sigma, mc_a, mc_b, ea, eb in pairs[:5]:
        ca, cb = ea["centroid"], eb["centroid"]
        id_a = ea["id"].split("/")[0]
        id_b = eb["id"].split("/")[0]
        total_sq = sum(weights[i] * (ca[k] - cb[k]) ** 2 for i, k in enumerate(KEYS)) or 1e-9
        print(f"\n  {mc_a} ↔ {mc_b}  [{id_a} ↔ {id_b}]  (wdist={d:.4f}, {d_sigma:.2f}σ):")
        for i, k in enumerate(KEYS):
            contrib_sq = weights[i] * (ca[k] - cb[k]) ** 2
            pct = 100 * contrib_sq / total_sq
            delta = cb[k] - ca[k]
            bar = "█" * int(pct / 5 + 0.5)
            print(f"    {k:<10} delta={delta:+.4f}  contrib={pct:5.1f}%  {bar}")

    # ── Summary verdict ──────────────────────────────────────────────────────
    min_pair = pairs[0]
    print()
    print("═" * 80)
    print(f"  Wave 9 exit criterion : min pairwise > {THRESHOLD_SIGMA:.1f}σ")
    print(f"  Method                : per-side entry minimum distance (ADR-VEC-002)")
    print(f"  Minimum separation    : {min_pair[2]} ↔ {min_pair[3]}")
    id_a = min_pair[4]["id"].split("/")[0]
    id_b = min_pair[5]["id"].split("/")[0]
    print(f"                          [{id_a} ↔ {id_b}]")
    print(f"                          wdist={min_pair[0]:.4f}  ({min_pair[1]:.2f}σ)")
    print()
    if alarm_count == 0:
        print("  RESULT: ✅ ALL pairs > 1.0σ — Wave 9 criterion MET")
    else:
        print(f"  RESULT: ❌ {alarm_count} pair(s) below 1.0σ — Wave 9 criterion NOT MET")
    if warning_count:
        print(f"          ⚠️  {warning_count} additional pair(s) below 2.0σ or overlapping r95")
    print("═" * 80)


if __name__ == "__main__":
    main()
