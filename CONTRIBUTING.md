# Contributing to CoinTrace™

Thank you for your interest in contributing to CoinTrace™! We welcome contributions from the community.

## 📋 Ways to Contribute

- **🔬 Add coin fingerprints** — Most valuable! Measure coins and submit fingerprint data
- **🐛 Report bugs** — Open an issue with detailed reproduction steps
- **💡 Suggest features** — Open an issue with `[FEATURE]` tag
- **🔧 Submit code** — Fork, create a branch, and submit a pull request
- **📝 Improve documentation** — Fix typos, clarify instructions, add examples
- **⭐ Star the repo** — Helps others discover CoinTrace™

## 🔐 Developer Certificate of Origin (DCO)

By contributing to this project, you certify that:

1. You have the right to submit your contribution under the project's licenses
2. You understand your contribution will be licensed under:
   - **GPL v3.0** for code (firmware, software)
   - **CERN OHL v2** for hardware (schematics, PCB layouts)
   - **CC BY-SA 4.0** for documentation

**All commits MUST be signed off.**

### How to Sign Your Commits

```bash
git commit -s -m "Your commit message"
```

The `-s` flag adds a "Signed-off-by" line:

```
Signed-off-by: Your Name <your.email@example.com>
```

This certifies you agree to the Developer Certificate of Origin (https://developercertificate.org/).

## 🔬 Contributing Coin Fingerprints

Fingerprint contributions are the most valuable way to help the community!

### Requirements

- CoinTrace™ hardware device (or compatible setup)
- Verified authentic coin
- Stable measurement conditions (room temperature, level surface)

### Process

1. **Measure your coin**:
   - Place coin on sensor at 0mm, 1mm, 3mm distances
   - Device will generate fingerprint JSON

2. **Export data**:
   - Press `SAVE` → `EXPORT` on device
   - Copy JSON from microSD card

3. **Submit**:
   - Fork this repository
   - Add your JSON file to `database/samples/`
   - Name format: `CountryCode_CoinName_Year.json`
   - Example: `USA_Quarter_2020.json`

4. **Pull Request**:
   - Include coin photos (obverse, reverse)
   - Mention your device version
   - Note temperature during measurement

### Fingerprint JSON Format

```json
{
  "version": 1,
  "coin": {
    "name": "Austrian Maria Theresa Thaler",
    "year": 1780,
    "country": "Austria",
    "denomination": "1 Thaler",
    "diameter_mm": 39.5,
    "weight_g": 28.06,
    "metal": "Silver 833"
  },
  "fingerprint": {
    "dRp1": 312.4,
    "k1": 0.714,
    "k2": 0.388,
    "slope": -89.2,
    "dL1": 724.1
  },
  "metadata": {
    "device": "CoinTrace™ v1.0",
    "temp_c": 23.4,
    "date": "2026-03-21",
    "contributor": "your_github_username"
  }
}
```

## 🐛 Reporting Bugs

Open an issue with:

- **Clear title**: "LDC1101 fails to initialize on ESP32-S3"
- **Environment**: Hardware version, firmware version, OS
- **Steps to reproduce**: Detailed step-by-step
- **Expected behavior**: What should happen
- **Actual behavior**: What actually happens
- **Logs**: Serial output, error messages

## 💡 Suggesting Features

Open an issue with:

- **Title**: `[FEATURE] Support for LDC1612 sensor`
- **Use case**: Why is this needed?
- **Proposed solution**: How could it work?
- **Alternatives**: Other approaches considered

## 🔧 Code Contributions

### Setup Development Environment

```bash
# Clone your fork
git clone https://github.com/YOUR_USERNAME/CoinTrace.git
cd CoinTrace

# Create a branch
git checkout -b feature/your-feature-name

# Make changes and test
pio run --target upload
pio test

# Commit with sign-off
git commit -s -m "Add support for temperature compensation"

# Push and create PR
git push origin feature/your-feature-name
```

### Code Style

- **C++**: Follow Arduino style guide
- **Python**: PEP 8
- **Comments**: Explain *why*, not *what*
- **Commits**: One logical change per commit

### Testing

- Test on real hardware before submitting
- Include unit tests for new functions
- Verify no regression in existing features

## 📝 Documentation Contributions

- Fix typos, grammar, formatting
- Clarify confusing sections
- Add missing examples
- Translate to other languages (future)

All documentation is licensed under CC BY-SA 4.0.

## ✅ Pull Request Process

1. **Fork and branch**: Create feature branch from `main`
2. **Make changes**: Follow code style, add tests
3. **Sign commits**: Use `git commit -s`
4. **Update docs**: If changing functionality
5. **Submit PR**: Clear title and description
6. **Review**: Respond to feedback
7. **Merge**: Maintainer will merge when approved

### PR Checklist

- [ ] Code compiles without warnings
- [ ] Tested on hardware
- [ ] All commits signed off (`-s` flag)
- [ ] Documentation updated
- [ ] No unrelated changes included

## 📄 License Agreement

By contributing, you agree that:

- Your code contributions are licensed under **GPL v3.0**
- Your hardware contributions are licensed under **CERN OHL v2**
- Your documentation contributions are licensed under **CC BY-SA 4.0**
- You have the legal right to make these contributions

## 🤝 Code of Conduct

Please be respectful and constructive:

- Be welcoming to newcomers
- Respect different viewpoints
- Accept constructive criticism
- Focus on what is best for the community

## 📞 Questions?

- Open a [Discussion](https://github.com/xkachya/CoinTrace/discussions)
- Check existing [Issues](https://github.com/xkachya/CoinTrace/issues)

---

Thank you for contributing to CoinTrace™! 🙏

*CoinTrace™ is an open-source project. This document is licensed under CC BY-SA 4.0.*
