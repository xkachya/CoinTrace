#!/usr/bin/env python3
"""Simulate real top-3 match ranking using actual C-14 measurement records."""
import json, math, pathlib, sys

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

REPO = pathlib.Path(__file__).resolve().parent.parent
NDJSON = REPO / "docs" / "external" / "\u0421-14" / "2026-04-11.C14_unified.ndjson"
DB_PATH = REPO / "data" / "sd_seed" / "CoinTrace" / "database" / "index.json"

WEIGHTS = [1.5, 0.0, 1.0, 3.0, 2.5, 0.4, 5.0]
KEYS    = ["dRp1_n", "k1", "k2", "df_n", "dL1_n", "df1_n", "mass_n"]
SIGMA   = 0.35

records = [json.loads(l) for l in NDJSON.read_text(encoding="utf-8").splitlines() if l.strip()]
db      = json.loads(DB_PATH.read_text(encoding="utf-8"))
entries = db["entries"]


def wdist(a: dict, b: dict) -> float:
    return math.sqrt(sum(WEIGHTS[i] * (a.get(KEYS[i], 0) - b.get(KEYS[i], 0)) ** 2 for i in range(7)))


def match_top3(pv: dict) -> list:
    ranked = [(wdist(pv, e["centroid"]), e["metal_code"], e["id"].split("/")[0], e["coin_name"]) for e in entries]
    ranked.sort()
    return ranked[:3]


TARGET_CLASSES = {
    "American Silver Eagle 1oz (Side A)", "American Silver Eagle 1oz (Side B)",
    "USSR 10 Rubles Ag900 (Side A)",      "USSR 10 Rubles Ag900 (Side B)",
    "Germany 1.5 Euro CuNi (Side A)",     "Germany 1.5 Euro CuNi (Side B)",
    "Kennedy Half Dollar Ag400 (Side A)", "Kennedy Half Dollar Ag400 (Side B)",
    "Olympic 1984 Ag900 (Side A)",        "Olympic 1984 Ag900 (Side B)",
}

total = correct_top1 = in_top3_count = not_in_top3 = 0

for rec in records:
    cn = rec.get("coin_name", "")
    if cn not in TARGET_CLASSES:
        continue
    pv = rec.get("production_vector", {})
    if "mass_n" not in pv:
        continue

    top3 = match_top3(pv)
    correct_mc = rec["metal_code"]
    top1_correct = top3[0][1] == correct_mc
    in_top3 = any(correct_mc == t[1] for t in top3)

    flag = "\u2705" if top1_correct else ("\U0001f536" if in_top3 else "\u274c")
    total += 1
    if top1_correct:
        correct_top1 += 1
    if in_top3:
        in_top3_count += 1
    else:
        not_in_top3 += 1

    idx = rec["index"]
    mass = rec["mass_g"]
    print(f"{flag} [{idx:3d}] {cn:<42}  mass={mass:.2f}g")
    for rank, (d, mc, eid, coin_label) in enumerate(top3, 1):
        marker = " <-- CORRECT" if mc == correct_mc else ""
        print(f"       #{rank}: {mc:<10} {eid:<24} {d/SIGMA:.2f}s{marker}")
    print()

print("=" * 72)
print(f"  Records tested     : {total}")
print(f"  Top-1 correct      : {correct_top1}/{total}  ({100*correct_top1//total}%)")
print(f"  Correct in top-3   : {in_top3_count}/{total}  ({100*in_top3_count//total}%)")
print(f"  NOT in top-3       : {not_in_top3}/{total}")
print("=" * 72)
