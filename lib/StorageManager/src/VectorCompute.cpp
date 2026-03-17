// VectorCompute.cpp — slope() OLS implementation (Wave 8 C-3)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3

#include "VectorCompute.h"

namespace VectorCompute {

// slope()  — OLS linear regression of normalized Rp over 3 distances.
//
// Input points (xi, yi):
//   (0mm, rp[0]/rp[0]) = (0, 1.0)
//   (1mm, rp[1]/rp[0]) = (1, k1)
//   (3mm, rp[2]/rp[0]) = (3, k2)
//
// OLS closed form for n=3:
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
