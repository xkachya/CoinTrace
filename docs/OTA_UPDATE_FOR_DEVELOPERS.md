# CoinTrace OTA Firmware Update Guide (for Developers)

**Last updated:** 2026-03-18

---

## Overview
This document describes the step-by-step process for performing a safe and reliable OTA (Over-The-Air) firmware update on CoinTrace devices. The procedure is intended for developers and advanced users with physical access to the device.

---

## Prerequisites
- **Physical access** to the device (to press the 'O' key)
- **Device powered on** and connected to the same network as your computer (AP or STA mode)
- **Firmware binary** (`firmware.bin`) built for your target environment
- **Python 3** installed (for running helper scripts)

---

## OTA Update Procedure

### 1. Build the Firmware
- Use PlatformIO or your preferred toolchain to build the firmware for the target environment.
- The output binary should be located at `.pio/build/<env>/firmware.bin`.

### 2. Connect to the Device
- Determine the device's IP address (displayed on the device screen or via your router).
- Ensure your computer can reach the device (e.g., `ping <device_ip>`).

### 3. Open the OTA Window
- On the device, **press the 'O' key**. This opens a 30-second OTA upload window.
- The device display will indicate that the OTA window is open (countdown timer).

### 4. Upload the Firmware
- Use the provided Python script to upload the firmware:

```sh
python scripts/test_ota_flash.py --ip <device_ip> --env <env_name>
```
- Replace `<device_ip>` with the actual IP address and `<env_name>` with your PlatformIO environment (e.g., `cointrace-dev`).
- The script will:
  - Check device status
  - Prompt you to press 'O' (if not already done)
  - Upload the firmware via `POST /api/v1/ota/update`
  - Wait for the device to reboot
  - Verify the new firmware version

**Manual alternative:**
You can use `curl` or `http` to POST the binary:
```sh
curl -X POST --data-binary @firmware.bin -H "Content-Type: application/octet-stream" http://<device_ip>/api/v1/ota/update
```

### 5. Confirm the Update
- After reboot, the device will run the new firmware in **pending** mode.
- **Press 'O' again** within 60 seconds to confirm and make the update permanent.
- If not confirmed, the device will automatically roll back to the previous firmware.

### 6. Verify the Update
- Check the device display and/or Web UI for the new firmware version.
- Optionally, query the API:
  - `GET /api/v1/ota/status` — check `pending`, `confirmed`, and `pre_version` fields.

---

## Error Handling & Safety
- If the OTA window is not open, the device will reject the upload (`403 ota_window_not_active`).
- If the firmware is invalid or upload fails, the device will not reboot and will report an error.
- If the new firmware is not confirmed, the device will auto-rollback after 60 seconds.

---

## References
- `scripts/test_ota_flash.py` — main OTA upload script
- `scripts/test_ota_hw.py` — hardware-level OTA tests
- `docs/architecture/WAVE8_ROADMAP.md` — architecture and acceptance criteria
- `lib/HttpServer/src/HttpServer.cpp` — OTA API implementation

---

## Notes
- OTA via Web UI is not implemented in v1. All updates must be performed via API/CLI as described above.
- For production/end-user OTA, see future roadmap (v2+).

---

**Contact:** For questions or issues, contact the CoinTrace development team.
