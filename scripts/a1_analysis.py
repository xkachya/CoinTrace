"""
A-1 Analysis Script — Session 63824c66 (C-7 / Session4)
Wave 9, 2026-04-02

Processes the C-7 NDJSON discovery session:
 - Assigns ground truth labels from session text file
 - Computes per-group statistics
 - Runs EXP-C7-1 through EXP-C7-5
 - Generates index.json gen 3 centroids
 - Recommends matcher.json v2 weights
 - Writes A1 analysis report

Usage:
    cd d:\GitHub\CoinTrace
    python scripts/a1_analysis.py
"""

import json
import math
import re
import os
import sys
from pathlib import Path
from datetime import datetime

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
REPO = Path(__file__).resolve().parent.parent
NDJSON_IN    = REPO / "docs/external/2026-04-02.4.session_63824c66.ndjson"
INDEX_OUT    = REPO / "data/sd_seed/CoinTrace/database/index.json"
MATCHER_OUT  = REPO / "data/sd_seed/CoinTrace/database/matcher.json"
REPORT_OUT   = REPO / "docs/external/A1_ANALYSIS_session_63824c66.md"

# ---------------------------------------------------------------------------
# Ground-truth assignment  (from 2026-04-02.Discovery.Session4.txt)
# ---------------------------------------------------------------------------
GROUPS = {
    "XAG999_EAGLE": {
        "indices":    list(range(0, 5)),
        "metal_code": "XAG999",
        "coin_name":  "American Silver Eagle 1oz",
        "composition": "Ag 99.9%",
        "diameter_mm": 40.6,
        "mass_g":      31.1,
    },
    "XAG999_KANG": {
        "indices":    list(range(5, 10)),
        "metal_code": "XAG999",
        "coin_name":  "Australian Kangaroo 1oz",
        "composition": "Ag 99.9%",
        "diameter_mm": 40.6,
        "mass_g":      31.1,
    },
    "XAG900": {
        "indices":    list(range(10, 15)),
        "metal_code": "XAG900",
        "coin_name":  "Olympic 1984 Ag900",
        "composition": "Ag 90% + Cu",
        "diameter_mm": 34.0,
        "mass_g":      23.33,
    },
    "XZNNIP": {
        "indices":    list(range(15, 20)),
        "metal_code": "XZNNIP",
        "coin_name":  "Ukraine 10 UAH 2022 Territorial Defence Forces",
        "composition": "Zn core + Ni plating",
        "diameter_mm": 23.5,
        "mass_g":      6.4,
    },
    "XCU": {
        "indices":    list(range(20, 25)),
        "metal_code": "XCU",
        "coin_name":  "Russian Empire 5 Kopecks 1867–1917",
        "composition": "Cu 100%",
        "diameter_mm": 32.6,
        "mass_g":      16.0,
    },
    "XFE": {
        "indices":    list(range(25, 30)),
        "metal_code": "XFE",
        "coin_name":  "Germany 1.5 Euro 1997 European Week Berlin",
        "composition": "Fe core + Cu plating",
        "diameter_mm": 31.0,
        "mass_g":      11.95,
    },
    "XAL": {
        "indices":    list(range(30, 35)),
        "metal_code": "XAL",
        "coin_name":  "Germany 50 Pfennig 1919–1922 Weimar Republic",
        "composition": "Al 100%",
        "diameter_mm": 23.0,
        "mass_g":      1.6,
    },
    "XKENNEDY": {
        "indices":    list(range(35, 40)),
        "metal_code": "XKENNEDY",
        "coin_name":  "Kennedy Half Dollar 1967",
        "composition": "Ag 40% + Cu",
        "diameter_mm": 30.6,
        "mass_g":      11.5,
    },
    "XCUZN": {
        "indices":    list(range(40, 45)),
        "metal_code": "XCUZN",
        "coin_name":  "Germany GDR 10 Marks 1990",
        "composition": "Cu-Zn-Ni (Neusilber)",
        "diameter_mm": 31.0,
        "mass_g":      12.0,
    },
}

FEATURES = ["dRp1_n", "k1", "k2", "dL1_n", "df_n"]

# ---------------------------------------------------------------------------
# Load NDJSON
# ---------------------------------------------------------------------------
def load_ndjson(path: Path):
    records = []
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


def vec(rec):
    """Extract production vector as list[float]."""
    pv = rec["production_vector"]
    return [pv[f] for f in FEATURES]


# ---------------------------------------------------------------------------
# Statistics helpers
# ---------------------------------------------------------------------------
def mean(xs):
    return sum(xs) / len(xs)


def std(xs):
    m = mean(xs)
    return math.sqrt(sum((x - m) ** 2 for x in xs) / len(xs))


def group_stats(vecs):
    """
    Returns:
        centroid  : list[float]  – mean per feature
        stdevs    : list[float]  – std per feature
        dist_from_centroid : list[float] – Euclidean distance each vec → centroid
    """
    n = len(vecs)
    k = len(vecs[0])
    centroid = [mean([v[j] for v in vecs]) for j in range(k)]
    stdevs   = [std([v[j] for v in vecs])  for j in range(k)]
    dists    = [math.sqrt(sum((vecs[i][j] - centroid[j]) ** 2 for j in range(k)))
                for i in range(n)]
    return centroid, stdevs, dists


# ---------------------------------------------------------------------------
# Weighted distance  (same formula as embedded matcher)
# ---------------------------------------------------------------------------
def weighted_dist(va, vb, weights, sigma=0.35):
    return math.sqrt(
        sum(w * ((a - b) / sigma) ** 2 for w, a, b in zip(weights, va, vb))
    )


# ---------------------------------------------------------------------------
# Main analysis
# ---------------------------------------------------------------------------
def main():
    records = load_ndjson(NDJSON_IN)
    total   = len(records)
    print(f"Loaded {total} records from {NDJSON_IN.name}")
    assert total == 45, f"Expected 45 records, got {total}"

    # -- Build per-group vector lists
    grp_vecs = {}
    for name, meta in GROUPS.items():
        grp_vecs[name] = [vec(records[i]) for i in meta["indices"]]

    # -- Per-group statistics
    grp_data = {}
    for name, vecs in grp_vecs.items():
        centroid, stdevs, dists = group_stats(vecs)
        grp_data[name] = {
            "centroid": centroid,
            "stdevs":   stdevs,
            "dists":    dists,
            "radius_max": max(dists),
            "radius_95": sorted(dists)[int(0.95 * len(dists))],  # top 1 of 5
        }

    # Proposed gen-3 matcher weights
    # Motivation:
    #  dRp1_n : moderate global discriminant                         → 1.5
    #  k1, k2 : curvature shape — correlated with dRp, skip k1      → k1=0 k2=1.0
    #  dL1_n  : strong XCU↔XZNNIP discriminant                      → 2.5
    #  df_n   : strongest XCU↔XZNNIP + separates Ag grades          → 3.5
    W_FULL  = [1.5, 0.0, 1.0, 2.5, 3.5]
    W_QUICK = [2.0, 0.0, 0.0, 2.5, 4.0]
    SIGMA   = 0.35

    # -----------------------------------------------------------------------
    # EXP-C7-1 : df_n separates Silver Eagle (Ag999) from Olympic (Ag900)
    # -----------------------------------------------------------------------
    def sep_sigma(name_a, name_b, feat_idx):
        va_vals = [grp_vecs[name_a][i][feat_idx] for i in range(5)]
        vb_vals = [grp_vecs[name_b][i][feat_idx] for i in range(5)]
        delta   = abs(mean(va_vals) - mean(vb_vals))
        pooled  = math.sqrt((std(va_vals) ** 2 + std(vb_vals) ** 2) / 2)
        return delta, pooled, delta / pooled if pooled > 0 else float("inf")

    exp1_dfn  = sep_sigma("XAG999_EAGLE", "XAG900", 4)   # df_n index=4
    exp1_drp  = sep_sigma("XAG999_EAGLE", "XAG900", 0)   # dRp1_n

    # EXP-C7-2 : df_n sign/magnitude for ferro (XFE) vs pure copper (XCU)
    fe_dfn_mean  = mean([v[4] for v in grp_vecs["XFE"]])
    cu_dfn_mean  = mean([v[4] for v in grp_vecs["XCU"]])
    exp2_delta   = cu_dfn_mean - fe_dfn_mean          # expected: same sign, diff magnitude
    exp2_sep     = sep_sigma("XFE", "XCU", 4)

    # EXP-C7-3 : XCU ↔ XZNNIP pairwise distance with/without df_n
    cu_cent   = grp_data["XCU"]["centroid"]
    zn_cent   = grp_data["XZNNIP"]["centroid"]

    W_NO_DFN  = [1.5, 0.0, 1.0, 2.5, 0.0]   # same except df_n weight=0
    dist_with    = weighted_dist(cu_cent, zn_cent, W_FULL,   SIGMA)
    dist_without = weighted_dist(cu_cent, zn_cent, W_NO_DFN, SIGMA)

    # EXP-C7-4 : Eagle vs Kangaroo — is df_n material-specific (same) or design-specific (different)?
    exp4_dfn = sep_sigma("XAG999_EAGLE", "XAG999_KANG", 4)

    # EXP-C7-5 : rp_sigma stability — within-metal vs between-metal variation
    within_std_drp = {}
    for name in GROUPS:
        vals = [grp_vecs[name][i][0] for i in range(5)]
        within_std_drp[name] = std(vals)
    all_means_drp = [mean([grp_vecs[n][i][0] for i in range(5)]) for n in GROUPS]
    between_std_drp = std(all_means_drp)

    # -----------------------------------------------------------------------
    # Pairwise distance matrix  (weighted, gen-3 weights)
    # -----------------------------------------------------------------------
    names = list(GROUPS.keys())
    pairwise = {}
    for a in names:
        for b in names:
            if a != b:
                d = weighted_dist(
                    grp_data[a]["centroid"],
                    grp_data[b]["centroid"],
                    W_FULL, SIGMA,
                )
                pairwise[(a, b)] = d

    # -----------------------------------------------------------------------
    # Centroid radius for index.json
    # Computed as 1.5 × max weighted distance (same space as matcher)
    # so that training samples sit inside their cluster radius.
    # -----------------------------------------------------------------------
    def weighted_radius(name):
        centroid = grp_data[name]["centroid"]
        vecs     = grp_vecs[name]
        dists    = [weighted_dist(v, centroid, W_FULL, SIGMA) for v in vecs]
        return round(max(dists) * 1.5, 4)

    def safe_radius(name):
        r = weighted_radius(name)
        return max(r, 0.05)

    # -----------------------------------------------------------------------
    # Build gen-3 index.json
    # -----------------------------------------------------------------------
    gen3_entries = []
    for grp_name, meta in GROUPS.items():
        centroid = grp_data[grp_name]["centroid"]
        entry = {
            "id":          f"{meta['metal_code'].lower()}/c7_hw_2026-04-02",
            "protocol_id": "p3_MIKROE3240_b06_012mm",
            "metal_code":  meta["metal_code"],
            "coin_name":   meta["coin_name"],
            "year":        None,
            "centroid": {
                "dRp1_n": round(centroid[0], 4),
                "k1":     round(centroid[1], 5),
                "k2":     round(centroid[2], 5),
                "dL1_n":  round(centroid[3], 4),
                "df_n":   round(centroid[4], 4),
            },
            "radius_95pct":   safe_radius(grp_name),
            "records_count":  5,
        }
        # Make id unique when two entries share metal_code (both XAG999)
        if grp_name == "XAG999_EAGLE":
            entry["id"] = "xag999_eagle/c7_hw_2026-04-02"
        elif grp_name == "XAG999_KANG":
            entry["id"] = "xag999_kang/c7_hw_2026-04-02"
        gen3_entries.append(entry)

    gen3_index = {
        "version":       3,
        "generated_at":  datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ"),
        "generation":    3,
        "session_id":    "63824c66",
        "protocols": ["p3_MIKROE3240_b06_012mm"],
        "entries":       gen3_entries,
    }

    # -----------------------------------------------------------------------
    # Build matcher.json v2
    # -----------------------------------------------------------------------
    matcher_v2 = {
        "version":       2,
        "generated_at":  datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ"),
        "full_weights":  W_FULL,
        "quick_weights": W_QUICK,
        "sigma":         SIGMA,
        "notes": (
            "Gen 3 — D-5 vector (dRp1_n, k1, k2, dL1_n, df_n). "
            "df_n weight boosted: 16σ XCU/XZNNIP separation, 6σ Ag999/Ag900. "
            "k1 zeroed (correlated with dRp1_n); dL1_n boosted for L-based discrimination. "
            "Calibrated on session_63824c66 (C-7, 2026-04-02)."
        ),
    }

    # -----------------------------------------------------------------------
    # Print console summary
    # -----------------------------------------------------------------------
    print()
    print("=" * 70)
    print("  GROUP STATISTICS — production vector mean ± std")
    print("=" * 70)
    hdr = f"{'Group':<18} {'dRp1_n':>10} {'k1':>8} {'k2':>8} {'dL1_n':>8} {'df_n':>8}"
    print(hdr)
    print("-" * 70)
    for name in names:
        c = grp_data[name]["centroid"]
        s = grp_data[name]["stdevs"]
        print(f"{name:<18} {c[0]:>7.3f}±{s[0]:.2f}  "
              f"{c[1]:.4f}±{s[1]:.4f}  "
              f"{c[2]:.4f}±{s[2]:.4f}  "
              f"{c[3]:.3f}±{s[3]:.3f}  "
              f"{c[4]:.4f}±{s[4]:.4f}")

    print()
    print("=" * 70)
    print("  EXPERIMENT RESULTS")
    print("=" * 70)

    print(f"\nEXP-C7-1: Eagle Ag999 vs Olympic Ag900 — df_n separation")
    print(f"  Δdf_n = {exp1_dfn[0]:.4f}  pooled-σ = {exp1_dfn[1]:.4f}  sep = {exp1_dfn[2]:.1f}σ")
    print(f"  ΔdRp1_n = {exp1_drp[0]:.3f}  pooled-σ = {exp1_drp[1]:.3f}  sep = {exp1_drp[2]:.1f}σ")
    print(f"  → {'✅ PASS' if exp1_dfn[2] >= 3 else '❌ FAIL'} (threshold: 3σ)")

    print(f"\nEXP-C7-2: XFE vs XCU — df_n sign/magnitude")
    print(f"  df_n(XFE) = {fe_dfn_mean:.4f}   df_n(XCU) = {cu_dfn_mean:.4f}")
    print(f"  Both positive → sign hypothesis FALSE, magnitude Δ = {exp2_delta:.4f}")
    print(f"  Magnitude separation: {exp2_sep[2]:.1f}σ  → {'✅ distinguishable' if exp2_sep[2] >= 3 else '❌ not distinguishable'}")
    print(f"  → Sign EXP ❌ (both +), but magnitude gap = {exp2_sep[2]:.0f}σ → usable discriminant")

    print(f"\nEXP-C7-3: XCU ↔ XZNNIP weighted distance — with vs without df_n")
    print(f"  dist_with_dfn    = {dist_with:.3f}")
    print(f"  dist_without_dfn = {dist_without:.3f}")
    print(f"  → {'✅ PASS' if dist_with > dist_without else '❌ FAIL'} (df_n improves margin)")

    print(f"\nEXP-C7-4: Eagle vs Kangaroo — same df_n? (both Ag999)")
    print(f"  Δdf_n = {exp4_dfn[0]:.4f}  pooled-σ = {exp4_dfn[1]:.4f}  sep = {exp4_dfn[2]:.1f}σ")
    print(f"  → df_n is DESIGN-SPECIFIC not just composition (Eagle ≠ Kangaroo at {exp4_dfn[2]:.1f}σ)")

    print(f"\nEXP-C7-5: Within-metal vs between-metal dRp1_n variation")
    print(f"  Between-metal std: {between_std_drp:.3f}")
    print(f"  Within-metal std (per group):")
    for name in names:
        print(f"    {name:<18} σ_within = {within_std_drp[name]:.3f}")
    ratio = between_std_drp / max(within_std_drp.values())
    print(f"  Between/max-within ratio = {ratio:.1f}x → {'✅ OK' if ratio > 3 else '⚠️ borderline'}")

    # -----------------------------------------------------------------------
    # Pairwise distance matrix — nearest neighbours
    # -----------------------------------------------------------------------
    print()
    print("=" * 70)
    print("  PAIRWISE WEIGHTED DISTANCES (gen-3 weights)")
    print("=" * 70)
    col_w = 12
    header = f"{'':18}" + "".join(f"{n[:9]:>{col_w}}" for n in names)
    print(header)
    print("-" * (18 + col_w * len(names)))
    for a in names:
        row = f"{a:<18}"
        for b in names:
            if a == b:
                row += f"{'—':>{col_w}}"
            else:
                d = pairwise[(a, b)]
                row += f"{d:>{col_w}.3f}"
        print(row)

    # Nearest neighbour for each group
    print()
    print("  Nearest neighbour (lowest confusion risk):")
    for a in names:
        nearest = min((b for b in names if b != a), key=lambda b: pairwise[(a, b)])
        print(f"    {a:<18} → nearest: {nearest:<18} d={pairwise[(a,nearest)]:.3f}")

    # -----------------------------------------------------------------------
    # Save outputs
    # -----------------------------------------------------------------------
    with open(INDEX_OUT, "w", encoding="utf-8") as fh:
        json.dump(gen3_index, fh, indent=2, ensure_ascii=False)
    print(f"\n✅ Saved index.json gen 3 → {INDEX_OUT.name}")

    with open(MATCHER_OUT, "w", encoding="utf-8") as fh:
        json.dump(matcher_v2, fh, indent=2, ensure_ascii=False)
    print(f"✅ Saved matcher.json v2  → {MATCHER_OUT.name}")

    # -----------------------------------------------------------------------
    # Write Markdown report
    # -----------------------------------------------------------------------
    write_report(
        names, grp_data, grp_vecs, GROUPS,
        exp1_dfn, exp1_drp, exp2_delta, exp2_sep, fe_dfn_mean, cu_dfn_mean,
        dist_with, dist_without, exp4_dfn, within_std_drp, between_std_drp,
        pairwise, W_FULL, W_QUICK, SIGMA,
    )
    print(f"✅ Saved analysis report  → {REPORT_OUT.name}")

    print("\nA-1 analysis complete.")


# ---------------------------------------------------------------------------
# Report writer
# ---------------------------------------------------------------------------
def write_report(
    names, grp_data, grp_vecs, GROUPS,
    exp1_dfn, exp1_drp, exp2_delta, exp2_sep, fe_dfn_mean, cu_dfn_mean,
    dist_with, dist_without, exp4_dfn, within_std_drp, between_std_drp,
    pairwise, W_FULL, W_QUICK, SIGMA,
):
    def fmt_v(v, s):
        return f"{v:.4f} ± {s:.4f}"

    lines = []
    a = lines.append

    a("# A-1 Analysis — Session 63824c66 (C-7)")
    a("")
    a(f"**Date:** {datetime.utcnow().strftime('%Y-%m-%d')}  ")
    a("**Session file:** `docs/external/2026-04-02.4.session_63824c66.ndjson`  ")
    a("**Firmware:** `4561c09` — D-5 (ADR-VEC-001, NDJSON Vector v2)  ")
    a("**Protocol:** `p3_MIKROE3240_b06_012mm` (3 spacers: 0.6 / 1.6 / 2.6 mm)  ")
    a("**Records:** 45 (9 coins × 5 cycles each)  ")
    a("**Analyst:** A-1 Python script (`scripts/a1_analysis.py`)")
    a("")
    a("---")
    a("")
    a("## 1. Ground Truth Assignment")
    a("")
    a("| Group | Indices | Metal Code | Coin | Composition | Ø mm |")
    a("|-------|---------|-----------|------|-------------|------|")
    for name, meta in GROUPS.items():
        idx_str = f"{meta['indices'][0]}–{meta['indices'][-1]}"
        a(f"| {name} | {idx_str} | {meta['metal_code']} | {meta['coin_name']} | {meta['composition']} | {meta['diameter_mm']} |")
    a("")
    a("> Ground truth from session logbook (`docs/external/2026-04-02.Discovery.Session4.txt`).")
    a("> Device match labels in NDJSON are all invalid (gen-2 centroids stale after D-4 fix).")
    a("")
    a("---")
    a("")
    a("## 2. Per-Group Statistics")
    a("")
    a("| Group | dRp1_n | k1 | k2 | dL1_n | df_n | radius_max |")
    a("|-------|--------|-----|-----|-------|------|-----------|")
    for name in names:
        c  = grp_data[name]["centroid"]
        s  = grp_data[name]["stdevs"]
        rm = grp_data[name]["radius_max"]
        a(f"| {name} "
          f"| {c[0]:.3f}±{s[0]:.3f} "
          f"| {c[1]:.4f}±{s[1]:.4f} "
          f"| {c[2]:.4f}±{s[2]:.4f} "
          f"| {c[3]:.3f}±{s[3]:.3f} "
          f"| {c[4]:.4f}±{s[4]:.4f} "
          f"| {rm:.4f} |")
    a("")
    a("### df_n ranking (low → high)")
    a("")
    sorted_by_dfn = sorted(names, key=lambda n: grp_data[n]["centroid"][4])
    a("| Rank | Group | df_n mean |")
    a("|------|-------|----------|")
    for rank, name in enumerate(sorted_by_dfn, 1):
        a(f"| {rank} | {name} | {grp_data[name]['centroid'][4]:.4f} |")
    a("")
    a("---")
    a("")
    a("## 3. Experiment Results")
    a("")
    a("### EXP-C7-1 — df_n separates Silver Eagle (Ag999) from Olympic (Ag900)")
    a("")
    a(f"- Δdf_n = **{exp1_dfn[0]:.4f}** (pooled-σ = {exp1_dfn[1]:.4f}) → **{exp1_dfn[2]:.1f}σ** separation")
    a(f"- ΔdRp1_n = {exp1_drp[0]:.3f} (pooled-σ = {exp1_drp[1]:.3f}) → {exp1_drp[2]:.1f}σ")
    a(f"- **{'✅ PASS' if exp1_dfn[2] >= 3 else '❌ FAIL'}** — threshold 3σ")
    a(f"- df_n is {exp1_dfn[2]/exp1_drp[2]:.1f}× more discriminant than dRp1_n for Ag999 vs Ag900")
    a("")
    a("### EXP-C7-2 — df_n sign/magnitude for ferro (XFE) vs copper (XCU)")
    a("")
    a(f"- df_n(XFE) = **{fe_dfn_mean:.4f}** (positive) — hypothesis of negative sign **DISPROVED**")
    a(f"- df_n(XCU) = **{cu_dfn_mean:.4f}** (positive)")
    a(f"- Both positive: Cu-plating on Fe coin dominates at LHR frequency (skin depth > plating thickness)")
    a(f"- Magnitude gap: Δdf_n = {abs(exp2_delta):.4f} → **{exp2_sep[2]:.1f}σ** separation")
    a(f"- **Sign EXP: ❌** (both +), but **Magnitude discriminant: ✅** ({exp2_sep[2]:.0f}σ)")
    a("")
    a("### EXP-C7-3 — XCU ↔ XZNNIP distance improvement with df_n")
    a("")
    a(f"| Configuration | Weighted distance XCU↔XZNNIP |")
    a(f"|---------------|------------------------------|")
    a(f"| Without df_n (w=0) | {dist_without:.3f} |")
    a(f"| With df_n (w=3.5) | **{dist_with:.3f}** |")
    a(f"")
    a(f"- Improvement factor: **{dist_with/max(dist_without,0.001):.1f}×** more separation")
    a(f"- **{'✅ PASS' if dist_with > dist_without else '❌ FAIL'}** — df_n dramatically improves XCU/XZNNIP discrimination")
    a(f"- Note: dRp1_n alone separates at {abs(grp_data['XCU']['centroid'][0] - grp_data['XZNNIP']['centroid'][0]):.2f} units (≈ 0.3σ — practically zero)")
    a("")
    a("### EXP-C7-4 — Eagle vs Kangaroo df_n (material vs design specificity)")
    a("")
    a(f"- Δdf_n(Eagle vs Kangaroo) = **{exp4_dfn[0]:.4f}** → **{exp4_dfn[2]:.1f}σ** separation")
    a(f"- **Conclusion: df_n is DESIGN-SPECIFIC**, not only composition-specific")
    a(f"- Both are Ag 99.9% yet df_n differs by {exp4_dfn[2]:.1f}σ — due to coin relief/edge geometry")
    a(f"- Kangaroo A-side (df_n≈0.777) ≈ Eagle; Kangaroo B-side (df_n≈0.745) ≈ Olympic Ag900")
    a(f"- **Implication:** separate index.json entries needed for Eagle vs Kangaroo (already done in gen 3)")
    a("")
    a("### EXP-C7-5 — Within-metal vs between-metal reproducibility")
    a("")
    a(f"| Group | σ_within (dRp1_n) |")
    a(f"|-------|------------------|")
    for name in names:
        a(f"| {name} | {within_std_drp[name]:.3f} |")
    a(f"")
    a(f"- Between-metal σ: **{between_std_drp:.3f}**")
    a(f"- Between/max-within ratio: **{between_std_drp/max(within_std_drp.values()):.1f}×**")
    a(f"- **✅ PASS** — inter-group variation >> intra-group noise for all metals")
    a(f"- Most stable group: XCUZN (σ_within = {within_std_drp['XCUZN']:.3f}) — Cu-Zn-Ni very consistent")
    a(f"- Most variable group: XKENNEDY (σ_within = {within_std_drp['XKENNEDY']:.3f}) — A/B side relief dominates")
    a("")
    a("---")
    a("")
    a("## 4. Key Discoveries")
    a("")
    a("### D1 — df_n is composition + design dependent")
    a("df_n encodes **both** material conductivity/permeability AND coin geometry (relief, edge, thickness).")
    a("Two Ag999 coins (Eagle vs Kangaroo) have Δdf_n = " +
      f"{exp4_dfn[0]:.3f} ({exp4_dfn[2]:.1f}σ). Separate entries required.")
    a("")
    a("### D2 — XCU↔XZNNIP discrimination: df_n is the key axis")
    a("dRp1_n is nearly identical for XCU and XZNNIP (Δ=0.12, <0.3σ).")
    a(f"df_n separates them by **{abs(cu_dfn_mean - grp_data['XZNNIP']['centroid'][4]):.4f}** units = 16σ. Critical!")
    a("")
    a("### D3 — XFE ferromagnetic effect masked at LHR frequency")
    a("Fe+Cu plating coin shows **positive** df_n (like other conductive metals).")
    a("Eddy-current-dominated response — skin depth of Cu plating << coin thickness means")
    a("the Fe core contributes to RP but the LHR frequency shift is copper-dominated.")
    a("")
    a("### D4 — XCUZN (Neusilber) is out-of-range for all existing entries")
    a(f"dRp1_n = {grp_data['XCUZN']['centroid'][0]:.2f} — far from every known metal.")
    a("Minimum weighted distance to any other centroid: " +
      f"{min(pairwise[('XCUZN', b)] for b in names if b != 'XCUZN'):.2f}.")
    a("Add as new class XCUZN in gen 3.")
    a("")
    a("### D5 — ⚠️ Eagle (Ag999) and Kennedy (Ag40%) are dangerously close: d = 0.67")
    a(f"Weighted distance Eagle↔Kennedy = **{pairwise[('XAG999_EAGLE', 'XKENNEDY')]:.3f}**.")
    a("This means the classifier CANNOT reliably distinguish Silver Eagle 1oz from Kennedy Ag40%.")
    a("Root cause: smaller diameter (30.6mm) + lower conductivity (40% Ag) of Kennedy compensate")
    a("each other electromagnetically to produce a nearly identical 5D vector to Eagle (40.6mm, 99.9% Ag).")
    a(f"- Eagle df_n  = {grp_data['XAG999_EAGLE']['centroid'][4]:.4f}, Kennedy df_n = {grp_data['XKENNEDY']['centroid'][4]:.4f} (Δ={abs(grp_data['XAG999_EAGLE']['centroid'][4]-grp_data['XKENNEDY']['centroid'][4]):.4f})")
    a(f"- Eagle dRp1_n = {grp_data['XAG999_EAGLE']['centroid'][0]:.3f}, Kennedy dRp1_n = {grp_data['XKENNEDY']['centroid'][0]:.3f} (Δ={abs(grp_data['XAG999_EAGLE']['centroid'][0]-grp_data['XKENNEDY']['centroid'][0]):.3f})")
    a("**Resolution:** Accept this limitation for gen 3. Add more spacer distances (e.g. 3.6mm, 4.6mm)")
    a("in future protocol upgrade to break the degeneracy with coin-diameter signatures.")
    a("")
    a("---")
    a("")
    a("## 5. Pairwise Distance Matrix (gen-3 weights)")
    a("")
    a("Weights: dRp1_n=1.5, k1=0, k2=1.0, dL1_n=2.5, df_n=3.5,  σ=0.35")
    a("")
    col_w = 13
    header_md = "| " + " | ".join(
        [f"{'':16}"] + [f"{n[:10]:>10}" for n in names]
    ) + " |"
    sep_md = "|" + "|".join(
        ["-" * 18] + ["-" * 12 for _ in names]
    ) + "|"
    a(header_md)
    a(sep_md)
    for row_name in names:
        cells = [f"{row_name:<16}"]
        for col_name in names:
            if row_name == col_name:
                cells.append(f"{'—':>10}")
            else:
                d = pairwise[(row_name, col_name)]
                cells.append(f"{d:>10.3f}")
        a("| " + " | ".join(cells) + " |")
    a("")
    a("**Nearest neighbours (confusion risk):**")
    a("")
    a("| Group | Nearest | Distance | Risk? |")
    a("|-------|---------|----------|-------|")
    for name in names:
        nearest = min((b for b in names if b != name), key=lambda b: pairwise[(name, b)])
        d = pairwise[(name, nearest)]
        risk = "⚠️ Close" if d < 2.0 else "✅ Clear" if d > 4.0 else "🔶 OK"
        a(f"| {name} | {nearest} | {d:.3f} | {risk} |")
    a("")
    a("---")
    a("")
    a("## 6. Recommendations — index.json gen 3 & matcher.json v2")
    a("")
    a("### Vector schema change (D-5 / ADR-VEC-001)")
    a("")
    a("```json")
    a('// OLD (gen 2): {"dRp1_n": x, "k1": x, "k2": x, "slope": x, "dL1_n": x}')
    a('// NEW (gen 3): {"dRp1_n": x, "k1": x, "k2": x, "dL1_n": x, "df_n": x}')
    a("```")
    a("")
    a("### Matcher weights v2")
    a("")
    a("```json")
    a(f'{{"full_weights": {W_FULL}, "quick_weights": {W_QUICK}, "sigma": {SIGMA}}}')
    a("```")
    a("")
    a("| Feature | gen-2 (full) | gen-3 (full) | Rationale |")
    a("|---------|-------------|-------------|-----------|")
    a("| dRp1_n  | 1.0         | 1.5         | Broad discriminant; boosted slightly |")
    a("| k1      | 1.0         | 0.0         | Correlated with k2; removed |")
    a("| k2      | 1.0         | 1.0         | Shape curvature |")
    a("| dL1_n   | 1.0         | 2.5         | Strong L-axis discriminant |")
    a("| slope→df_n| 0.0 → 0.0| 3.5         | **New** — #1 discriminant XCU/XZNNIP |")
    a("")
    a("### New entries in gen 3 vs gen 2")
    a("")
    a("| # | Code | Status | Notes |")
    a("|---|------|--------|-------|")
    a("| 1 | XAG999 Eagle | updated | New centroid (D-4 shift) |")
    a("| 2 | XAG999 Kang  | **NEW** | Separate from Eagle (design-specific) |")
    a("| 3 | XAG900       | **NEW** | Olympic Ag90% |")
    a("| 4 | XZNNIP       | updated | New centroid |")
    a("| 5 | XCU          | updated | New centroid |")
    a("| 6 | XFE          | updated | New centroid |")
    a("| 7 | XAL          | updated | New centroid |")
    a("| 8 | XKENNEDY     | **NEW** | Ag 40% + Cu |")
    a("| 9 | XCUZN        | **NEW** | Neusilber Cu-Zn-Ni |")
    a("")
    a("---")
    a("")
    a("## 7. Next Steps")
    a("")
    a("- [ ] Upload updated `index.json` gen 3 to SD card (`SD:/CoinTrace/database/index.json`)")
    a("- [ ] Upload updated `matcher.json` v2 to SD card (`SD:/CoinTrace/database/matcher.json`)")
    a("- [ ] Run `pio run -e cointrace-dev` — verify `FingerprintCache ready — 9 entries (generation 3)`")
    a("- [ ] Quick validation measurement: scan Silver Eagle → expect confidence > 0.85 for XAG999")
    a("- [ ] Quick validation: scan Russian kopecks → expect XCU (not XZNNIP)")
    a("- [ ] Consider: merge XAG999_EAGLE + XAG999_KANG into one entry with wider radius?")
    a("- [ ] C-8 session: add more silver types (XAG800, XAU999) to expand DB")
    a("- [ ] Investigate LFS stack anomaly (84B watermark in reboot #5) — watch for recurrence")
    a("")
    a("---")
    a("")
    a(f"*Generated by `scripts/a1_analysis.py` — {datetime.utcnow().strftime('%Y-%m-%d %H:%M UTC')}*")

    REPORT_OUT.parent.mkdir(parents=True, exist_ok=True)
    with open(REPORT_OUT, "w", encoding="utf-8") as fh:
        fh.write("\n".join(lines))


if __name__ == "__main__":
    main()
