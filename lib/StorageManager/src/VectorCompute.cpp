// VectorCompute.cpp — slope() OLS implementation (Wave 8 C-3)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3

#include "VectorCompute.h"

namespace VectorCompute {

// slope()  — OLS linear regression of normalized Rp over 3 distances.
//
// Wave 8 C-5: updated to p3 protocol constants (x={0,1,2}).
//   p3 protocol (0.6mm base + 0/+1/+2mm spacers → uniform x={0,1,2}):
//     x_mean = 1.0
//     Sxx    = 2.0
//     Closed form: slope = (k2 − 1) / 2
//
// NOTE: with uniform x={0,1,2}, slope is a pure linear transform of k2
//   (slope = (k2-1)/2 = (Rp2/Rp0 - 1)/2). This means full_weights[3] (slope)
//   contributes no independent information to the fingerprint distance. Set
//   full_weights[3]=0.0 in matcher.json when empirically tuning weights.
//
// Input points (xi, yi):
//   (0, rp[0]/rp[0]) = (0, 1.0)
//   (1, rp[1]/rp[0]) = (1, k1)
//   (2, rp[2]/rp[0]) = (2, k2)
//
// OLS closed form for n=3 with x={0,1,2}:
//   x_mean = 1.0,  y_mean = (1 + k1 + k2) / 3
//   Sxx = 2.0
//   Sxy = (0-1)*(1-y_mean) + (1-1)*(k1-y_mean) + (2-1)*(k2-y_mean)
//       = -(1-y_mean) + (k2-y_mean)
//       = k2 - 1
//   slope = Sxy / Sxx = (k2 - 1) / 2
//
// Returns 0.0f if rp[0] < 1.0f (same guard as k1/k2).
float slope(const Measurement& m) {
    if (m.rp[0] < 1.0f) return 0.0f;

    const float k2 = m.rp[2] / m.rp[0];
    return (k2 - 1.0f) / 2.0f;
}

} // namespace VectorCompute
