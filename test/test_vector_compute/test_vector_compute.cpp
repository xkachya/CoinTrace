// test_vector_compute.cpp — Unit tests for VectorCompute (Wave 8 C-3)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3
//
// Tests pure math over Measurement::rp[4]/l[4].
// No hardware, no SPI, no FreeRTOS — runs on native (PC) via pio test -e native-test.
//
// Ground truth vectors validated against FINGERPRINT_DB_ARCHITECTURE.md §3.3
// synthetic data. Real sensor calibration happens at C-5 (post-LDC1101 arrival).

#include <unity.h>
#include "VectorCompute.h"
#include "Measurement.h"
#include <math.h>

// ── Test fixture helpers ──────────────────────────────────────────────────────

// Build a Measurement with rp[0..3] and l[0..1] set.
// rp[3] = drift check (return to 0mm); defaults to rp[0] (no drift).
static Measurement makeMeas(float rp0, float rp1, float rp2,
                             float l0,  float l1,
                             float rp3 = -1.0f) {
    Measurement m = {};
    m.rp[0] = rp0;
    m.rp[1] = rp1;
    m.rp[2] = rp2;
    m.rp[3] = (rp3 < 0.0f) ? rp0 : rp3;  // default: no drift
    m.l[0]  = l0;
    m.l[1]  = l1;
    m.pos_count = 4;
    return m;
}

// Typical Ag925 synthetic values (from ldc1101.json seed data, scaled to raw codes)
// rp decreases as coin approaches: rp0 > rp1 > rp2
static Measurement agMeas() { return makeMeas(1800.0f, 1350.0f, 1100.0f, 22.0f, 20.5f); }

// Typical gold (Au999) — steeper Rp drop
static Measurement auMeas() { return makeMeas(1600.0f, 1100.0f, 850.0f, 20.0f, 19.5f); }

// Ferrous (Fe) — small Rp drop, large L drop
static Measurement feMeas() { return makeMeas(2200.0f, 2100.0f, 2050.0f, 45.0f, 20.0f); }

// Zero baseline — guard condition
static Measurement zeroMeas() {
    Measurement m = {};
    return m;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

// 1. dRp1 > 0 for conductive metals (rp decreases as coin comes closer)
void test_dRp1_positive_for_conductive() {
    Measurement ag = agMeas();
    Measurement au = auMeas();
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, VectorCompute::dRp1(ag));
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, VectorCompute::dRp1(au));
}

// 2. k1 and k2 are in (0, 1) for typical metals
void test_k1_k2_in_unit_interval() {
    Measurement ag = agMeas();
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, VectorCompute::k1(ag));
    TEST_ASSERT_LESS_THAN_FLOAT   (1.0f, VectorCompute::k1(ag));
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, VectorCompute::k2(ag));
    TEST_ASSERT_LESS_THAN_FLOAT   (1.0f, VectorCompute::k2(ag));
}

// 3. k1 > k2 for typical conductors (steeper drop at 1mm than at 3mm from baseline)
void test_k2_less_than_k1_for_conductors() {
    Measurement ag = agMeas();
    Measurement au = auMeas();
    TEST_ASSERT_GREATER_THAN_FLOAT(VectorCompute::k2(ag), VectorCompute::k1(ag));
    TEST_ASSERT_GREATER_THAN_FLOAT(VectorCompute::k2(au), VectorCompute::k1(au));
}

// 4. slope < 0 for all metals (Rp/Rp0 ratio decreases with increasing distance)
void test_slope_negative() {
    TEST_ASSERT_LESS_THAN_FLOAT(0.0f, VectorCompute::slope(agMeas()));
    TEST_ASSERT_LESS_THAN_FLOAT(0.0f, VectorCompute::slope(auMeas()));
    TEST_ASSERT_LESS_THAN_FLOAT(0.0f, VectorCompute::slope(feMeas()));
}

// 5. dL1 near zero for Ag (diamagnetic); large for Fe (ferromagnetic)
void test_dL1_discriminates_ferrous() {
    // Ag925: l[0]-l[1] ~1.5 µH (small)
    TEST_ASSERT_LESS_THAN_FLOAT(5.0f, fabsf(VectorCompute::dL1(agMeas())));
    // Fe: l[0]-l[1] ~25 µH (large)
    TEST_ASSERT_GREATER_THAN_FLOAT(20.0f, VectorCompute::dL1(feMeas()));
}

// 6. div-by-zero guard: k1/k2/slope/dRp1_n return 0 when rp[0] = 0
void test_guard_zero_rp0() {
    Measurement z = zeroMeas();
    TEST_ASSERT_EQUAL_FLOAT(0.0f, VectorCompute::k1(z));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, VectorCompute::k2(z));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, VectorCompute::slope(z));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, VectorCompute::dRp1_n(z));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, VectorCompute::dL1_n(z));
}

// 7. slope OLS ground truth — hand-verified calculation
// Ag: rp0=1800, k1=1350/1800=0.75, k2=1100/1800≈0.6111
//   y_mean = (1.0 + 0.75 + 0.6111) / 3 = 0.7870
//   x_mean = 4/3 = 1.3333
//   Sxy = (0-1.3333)(1.0-0.7870) + (1-1.3333)(0.75-0.7870) + (3-1.3333)(0.6111-0.7870)
//       = (-1.3333)(0.2130) + (-0.3333)(-0.0370) + (1.6667)(-0.1759)
//       = -0.2840 + 0.0123 - 0.2932  = -0.5649
//   Sxx = 14/3 = 4.6667
//   slope = -0.5649 / 4.6667 = -0.12105...
void test_slope_ols_ground_truth() {
    Measurement ag = agMeas();  // rp0=1800, rp1=1350, rp2=1100
    const float s = VectorCompute::slope(ag);
    // Allow ±0.001 for floating-point rounding
    TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.121f, s);
}

// 8. driftRatio = 0 when rp[3] == rp[0] (no drift)
void test_drift_ratio_zero_no_drift() {
    Measurement ag = agMeas();  // rp[3] = rp[0] by default
    TEST_ASSERT_EQUAL_FLOAT(0.0f, VectorCompute::driftRatio(ag));
}

// 9. driftRatio > DRIFT_THRESHOLD when rp[3] deviates by >5%
void test_drift_ratio_detects_drift() {
    Measurement m = makeMeas(1800.0f, 1350.0f, 1100.0f, 22.0f, 20.5f,
                              1900.0f);  // rp[3] = 1900 vs rp[0]=1800 → 5.55% drift
    TEST_ASSERT_GREATER_THAN_FLOAT(VectorCompute::DRIFT_THRESHOLD,
                                   VectorCompute::driftRatio(m));
}

// ── Runner ────────────────────────────────────────────────────────────────────

void setUp() {}
void tearDown() {}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_dRp1_positive_for_conductive);
    RUN_TEST(test_k1_k2_in_unit_interval);
    RUN_TEST(test_k2_less_than_k1_for_conductors);
    RUN_TEST(test_slope_negative);
    RUN_TEST(test_dL1_discriminates_ferrous);
    RUN_TEST(test_guard_zero_rp0);
    RUN_TEST(test_slope_ols_ground_truth);
    RUN_TEST(test_drift_ratio_zero_no_drift);
    RUN_TEST(test_drift_ratio_detects_drift);
    return UNITY_END();
}
