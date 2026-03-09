# Prior Art Disclosure — CoinTrace

**Document type:** Public Prior Art Disclosure  
**Author:** Yuriy Kachmaryk  
**Date:** 2026-03-08 (first commit timestamp)  
**Location:** Lviv, Ukraine  
**Cryptographic Proof:** Git SHA `71720cb` in repository https://github.com/xkachya/CoinTrace  
**Archive Snapshots:**
- Wayback Machine: https://web.archive.org/web/20260309003946/https://github.com/xkachya/CoinTrace/blob/main/docs/concept/prior_art_disclosure.md
- Archive.today: https://archive.ph/5UMGt

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

**Mathematical proof:** For a circular coin with diameter D,
the sensor interaction area scales as D². Since both
numerator (ΔRp₂ or ΔRp₃) and denominator (ΔRp₁) scale
proportionally with interaction area, the ratio k = ΔRpᵢ/ΔRp₁
cancels out the diameter dependency, making k independent
of coin size while remaining sensitive to metal composition.

### 3. Fingerprint Vector
Feature vector [ΔRp₁, k1, k2, slope, ΔL₁] as unique
identifier for specific coin types comparable via
Euclidean distance metric.

### 4. Community Fingerprint Database
Shared database of normalized fingerprint vectors
enabling crowd-sourced coin identification across
multiple devices and users.

### 5. Hardware Implementation
Reference implementation using Texas Instruments LDC1101
inductive sensor with ESP32-S3 microcontroller, demonstrating
practical feasibility of the method with consumer-grade
components (~$30 total cost).

## Prior Art References

- Sigma Metalytics US10839633B2 (2019) — measures
  bulk resistivity at fixed distance (different method)
- US6739444 (2004) — LC oscillator frequency shift
  (different parameter, different method)

## Scope of Disclosure

The disclosed concepts are not limited to the specific
implementation described. They cover all variations including:
- Different inductive sensors (LDC, LC oscillators, eddy current)
- Different measurement distances and counts (2+, 3+, 4+ points)
- Different normalization ratios and feature vectors
- Different metal objects (coins, tokens, metals, alloys)

## Declaration

This disclosure is intentionally made public to ensure
these concepts remain freely available for use by
anyone and cannot be monopolized through patent claims
filed after this publication date.
