# Prior Art Disclosure — CoinTrace

**Document type:** Public Prior Art Disclosure
**Author:** Yuriy Kachmaryk
**Date:** 2026-03-08 (first commit timestamp)
**Location:** Lviv, Ukraine

## Purpose

This document serves as a formal prior art disclosure
for the novel concepts described herein. Publication
on GitHub establishes a public record with verifiable
timestamp that may be used to challenge any future
patent claims covering these concepts.

## Novel Concepts Disclosed

### 1. Attenuation Curve Method
Measuring inductive sensor Rp parameter at multiple
controlled distances (0mm, 1mm, 3mm + baseline) to
create a distance-attenuation curve characteristic
of the measured metal object.

### 2. Size-Independent Normalization
Coefficients k1 = ΔRp₂/ΔRp₁ and k2 = ΔRp₃/ΔRp₁
are mathematically independent of coin diameter,
enabling cross-device comparison without calibration.

### 3. Fingerprint Vector
Feature vector [ΔRp₁, k1, k2, slope, ΔL₁] as unique
identifier for specific coin types comparable via
Euclidean distance metric.

### 4. Community Fingerprint Database
Shared database of normalized fingerprint vectors
enabling crowd-sourced coin identification across
multiple devices and users.

## Prior Art References

- Sigma Metalytics US10839633B2 (2019) — measures
  bulk resistivity at fixed distance (different method)
- US6739444 (2004) — LC oscillator frequency shift
  (different parameter, different method)

## Declaration

This disclosure is intentionally made public to ensure
these concepts remain freely available for use by
anyone and cannot be monopolized through patent claims
filed after this publication date.
