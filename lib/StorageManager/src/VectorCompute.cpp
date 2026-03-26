// VectorCompute.cpp — slope() OLS implementation (Wave 8 C-3)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3

#include "VectorCompute.h"

namespace VectorCompute {

// slope()  — OLS linear regression of normalized Rp over 3 distances.
//
// ⚠️ TODO Wave 8 C-5: formula uses p1 protocol constants (x={0,1,3}).
//   p3 protocol (0.6mm base + 0/+1/+2mm spacers) has uniform x={0,1,2}:
//     x_mean = 1.0  (was 4/3)
//     Sxx    = 2.0  (was 14/3)
//     x3 term = 2.0  (was 3.0)
//   With x={0,1,2}: slope = (k2 − 1) / 2  — purely a linear transform of k2.
//   This means slope contributes ZERO independent information to the fingerprint
//   vector with p3 protocol. Consequence: set full_weights[3]=0.0 in matcher.json.
//   Formula update deferred: requires reseed of synthetic DB slope centroids AND
//   update of test_slope_ols_ground_truth expected value (−0.121 → −0.1944 for Ag925).
//
// Current implementation (p1 formula — kept for test compatibility until C-5):
//
// Input points (xi, yi):
//   (0mm, rp[0]/rp[0]) = (0, 1.0)
//   (1mm, rp[1]/rp[0]) = (1, k1)
//   (2mm, rp[2]/rp[0]) = (2, k2)   ← physical position (p3); formula below still uses x3=3
//
// OLS closed form for n=3 with LEGACY x={0,1,3}:
//   x  = {0, 1, 3}    x_mean = 4/3
//   y  = {1, k1, k2}  y_mean = (1 + k1 + k2) / 3
//
//   Sxy = Σ(xi - x_mean)(yi - y_mean)
//       = (0 - 4/3)(1 - y_mean) + (1 - 4/3)(k1 - y_mean) + (3 - 4/3)(k2 - y_mean)
//       = (-4/3)(1 - y_mean) + (-1/3)(k1 - y_mean) + (5/3)(k2 - y_mean)
//
//   Sxx = Σ(xi - x_mean)²
//       = (4/3)² + (1/3)² + (5/3)²
//       = 16/9 + 1/9 + 25/9
//       = 42/9 = 14/3
//
//   slope = Sxy / Sxx
//
// Returns 0.0f if rp[0] < 1.0f (same guard as k1/k2).
float slope(const Measurement& m) {
    if (m.rp[0] < 1.0f) return 0.0f;

    const float rp0  = m.rp[0];
    const float y0   = 1.0f;          // rp[0] / rp[0]
    const float y1   = m.rp[1] / rp0; // k1
    const float y2   = m.rp[2] / rp0; // k2

    const float x_mean = 4.0f / 3.0f;
    const float y_mean = (y0 + y1 + y2) / 3.0f;

    // Sxy = Σ (xi - x_mean)(yi - y_mean)
    const float sxy = (0.0f - x_mean) * (y0 - y_mean)
                    + (1.0f - x_mean) * (y1 - y_mean)
                    + (3.0f - x_mean) * (y2 - y_mean);

    // Sxx = 14/3 (constant for fixed x = {0,1,3})
    constexpr float sxx = 14.0f / 3.0f;

    return sxy / sxx;
}

} // namespace VectorCompute
