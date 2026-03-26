// VectorCompute.h — Fingerprint Vector Computation (Wave 8 C-3)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
//
// Pure math over Measurement::rp[4] / l[4]. No hardware, no SPI, no FreeRTOS.
// Unit-testable on native (PC) without any embedded hardware.
//
// Cross-ref: FINGERPRINT_DB_ARCHITECTURE.md §3.3 — canonical vector definition
//            WAVE8_ROADMAP.md §C-3 — implementation spec
//            FingerprintCache::query() — consumes these values
//
// Normalization contract (FingerprintCache::query() expects pre-normalized input):
//   dRp1_n = dRp1(m) / 800.0f       [dRp1_MAX = 800 Ω]
//   dL1_n  = dL1(m)  / 2000.0f      [dL1_MAX  = 2000 µH]
//   k1, k2, slope — dimensionless, no normalization needed
//
// Wave 8 C-2 multi-position semantics / p3 protocol (Measurement.h):
//   rp[0] = reading at 0.6mm base spacer (d≈0.6mm — bare coin, no capsule)
//   rp[1] = reading at 0.6mm + 1mm spacer (d≈1.6mm)
//   rp[2] = reading at 0.6mm + 2mm spacer (d≈2.6mm)  ← p3: was 3.4mm in p2
//   rp[3] = drift check (return to base) — NOT used in vector computation
//
// ⚠️ MATH NOTE — slope() with uniform x={0,1,2} (p3 protocol):
//   OLS closed form simplifies to: slope = (k2 − 1) / 2
//   This is a linear transform of k2 → slope adds ZERO independent information
//   to the fingerprint vector when x is uniformly spaced.
//   Consequence: full_weights[3] (slope) should be 0.0 in matcher.json for p3.
//   Formula update (x={0,1,2}) + synthetic DB slope recomputation deferred to
//   Wave 8 C-5 (first real hw measurements) to avoid breaking 9/9 native tests.
//   See WAVE8_ROADMAP.md §C-5 and VectorCompute.cpp ⚠️ TODO.

#pragma once
#include "Measurement.h"
#include <math.h>

namespace VectorCompute {

// ── Raw vector components ─────────────────────────────────────────────────────

// dRp1 = Rp(0mm) − Rp(1mm)  [Ω]
// Represents total conductivity response at near-contact distance.
// Always positive for conductive metals (Rp decreases as coin approaches).
// FINGERPRINT_DB §3.3: used as absolute discriminator (size-dependent, filtered).
inline float dRp1(const Measurement& m) {
    return m.rp[0] - m.rp[1];
}

// k1 = Rp(1mm) / Rp(0mm)  [dimensionless, 0.0..1.0]
// First spatial normalization ratio. Size-independent (coin diameter cancels).
// Mathematical proof: FINGERPRINT_DB §2.1 — interaction area scales as D²,
// both numerator and denominator scale proportionally → ratio cancels D.
inline float k1(const Measurement& m) {
    if (m.rp[0] < 1.0f) return 0.0f;  // guard: div-by-zero if sensor not reading
    return m.rp[1] / m.rp[0];
}

// k2 = Rp(2mm) / Rp(0mm)  [dimensionless, 0.0..1.0]   ← p3: spacer offset from 0.6mm base
// Second spatial normalization ratio. Combined with k1 gives 2D metal signature.
// k1 ≈ k2 → flat curve (poor conductors); k1 >> k2 → steep curve (good conductors).
inline float k2(const Measurement& m) {
    if (m.rp[0] < 1.0f) return 0.0f;
    return m.rp[2] / m.rp[0];
}

// slope = OLS linear regression coefficient of Rp vs distance  [1/mm]
// Points: (0mm, rp[0]/rp[0]=1.0), (1mm, rp[1]/rp[0]=k1), (2mm, rp[2]/rp[0]=k2)
// ⚠️ FORMULA PENDING UPDATE (see file header — Wave 8 C-5):
//   Current impl uses p1 constants x={0,1,3} / x̄=4/3 / Sxx=14/3.
//   Correct p3 constants: x={0,1,2} / x̄=1 / Sxx=2 → slope=(k2−1)/2.
//   Formula unchanged to preserve 9/9 passing native tests until C-5 DB reseed.
// Typically negative (−0.05 .. −0.20): Rp/Rp0 decreases with distance.
// Non-inline: OLS requires 6+ arithmetic ops — kept in VectorCompute.cpp.
float slope(const Measurement& m);

// dL1 = L(0mm) − L(1mm)  [µH]
// Magnetic permeability component. Near-zero for Ag/Au/Cu (diamagnetic/weakly
// paramagnetic). Large positive for Fe/Ni (ferromagnetic). Used to distinguish
// ferrous from non-ferrous metals when Rp curves are ambiguous.
inline float dL1(const Measurement& m) {
    return m.l[0] - m.l[1];
}

// ── Normalized values for FingerprintCache::query() ──────────────────────────

// dRp1_normalized = dRp1 / 800.0f  (dRp1_MAX from FINGERPRINT_DB §3.3)
inline float dRp1_n(const Measurement& m) {
    return dRp1(m) / 800.0f;
}

// dL1_normalized = dL1 / 2000.0f  (dL1_MAX from FINGERPRINT_DB §3.3)
inline float dL1_n(const Measurement& m) {
    return dL1(m) / 2000.0f;
}

// ── Drift validation (WAVE8_ROADMAP §C-2, W-07 ADR) ──────────────────────────

// Returns |rp[3] - rp[0]| / rp[0].
// If > DRIFT_THRESHOLD (0.05 = 5%), sensor drift detected during measurement.
// Caller should log WARNING and set Measurement::conf = 0.0f.
inline float driftRatio(const Measurement& m) {
    if (m.rp[0] < 1.0f) return 0.0f;
    const float delta = m.rp[3] - m.rp[0];
    return (delta < 0.0f ? -delta : delta) / m.rp[0];
}

static constexpr float DRIFT_THRESHOLD = 0.05f;  // 5%

} // namespace VectorCompute
