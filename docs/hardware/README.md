# Hardware Documentation

**🔧 Open Hardware Files**

---

## Purpose

This folder contains **hardware schematics, PCB designs, and CAD files** for CoinTrace. All content is open-source under **CERN OHL v2 (Strongly Reciprocal)** license.

---

## 📂 Planned Content

### Schematics:
- `schematics/CoinTrace_Main_v1.0.pdf` - Main circuit schematic
- `schematics/LDC1101_Interface.pdf` - Sensor interface details
- `schematics/Power_Supply.pdf` - Power management

### PCB Designs:
- `pcb/CoinTrace_Main_v1.0.kicad_pcb` - KiCad PCB file
- `pcb/gerbers/` - Manufacturing files (Gerber format)
- `pcb/bom/` - Bill of Materials (BOM)

### Mechanical:
- `3d-models/spacers/` - 3D printable spacers (STL, STEP)
- `3d-models/enclosure/` - Device enclosure (optional)
- `mounting/` - Mounting solutions, jigs

### Manufacturing:
- `assembly-instructions.md` - PCB assembly guide
- `pick-and-place/` - Pick-and-place files for automated assembly
- `testing-procedures.md` - Quality control testing

### Alternative Designs:
- `alternatives/DIY_Breadboard.md` - Breadboard prototype guide
- `alternatives/LDC1612_Version.md` - Using LDC1612 instead of LDC1101
- `alternatives/Other_MCUs.md` - ESP32, Arduino compatibility

---

## 🛠️ Tools

**Recommended software:**
- KiCad (schematics & PCB) - Free, open-source
- FreeCAD (mechanical) - Free, open-source
- OpenSCAD (parametric models) - Free, open-source

---

## 📝 Contributing

Hardware contributions welcome! Guidelines:
- Use open-source tools (KiCad preferred)
- Test designs before submitting
- Provide clear documentation
- Follow CERN OHL v2 license terms

---

## ⚖️ License

**Hardware designs:** CERN Open Hardware Licence v2 (Strongly Reciprocal)  
**Documentation:** CC BY-SA 4.0

**What this means:**
- ✅ You can manufacture and sell CoinTrace devices
- ✅ You can modify the design
- ✅ Commercial use allowed
- ❌ Modified designs must be published (reciprocal)
- ❌ Cannot close-source the schematics

Read full license: [CERN OHL v2](https://ohwr.org/cern_ohl_s_v2.txt)

---

**Created:** 2026-03-09
