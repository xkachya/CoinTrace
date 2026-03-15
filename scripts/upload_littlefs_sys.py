"""
upload_littlefs_sys.py — Custom uploadfs for a specific LittleFS partition
===========================================================================
PlatformIO post: extra_scripts hook that patches FS_START and FS_SIZE so the
built-in uploadfs pipeline flashes to the CORRECT partition.

Background
----------
PlatformIO's espressif32 platform always finds the LAST "spiffs" partition in
the CSV and sets:

  FS_START = <last spiffs offset>   (used in UPLOADERFLAGS as the flash address)
  FS_SIZE  = <last spiffs size>    (used by mklittlefs to size the image)

CoinTrace has two LittleFS partitions ordered:

  littlefs_sys  @ 0x510000  (1.00 MB)  <- FIRST  (web UI, device config)
  littlefs_data @ 0x610000  (1.75 MB)  <- LAST   <- platform always picks this

After patching: FS_START = 0x510000, FS_SIZE = 0x100000.
The existing esptool call in UPLOADERFLAGS then flashes to the right address.

This script MUST be loaded as post: (not pre:) in platformio.ini.
Reason: pre: runs before the platform's main.py sets FS_START/FS_SIZE.
        post: runs after, so our Replace() sticks.

Usage in platformio.ini:
  [env:uploadfs-sys]
  extra_scripts = post:scripts/upload_littlefs_sys.py
  custom_partition_label = littlefs_sys   # must match CSV Name column

Then: pio run -e uploadfs-sys -t uploadfs

Copyright (c) 2026 CoinTrace Project. GPL v3.
Cross-ref: docs/guides/UPLOADFS_GUIDE.md, STORAGE_ARCHITECTURE.md section 8 (ADR-ST-002)
"""

import os
import sys

Import("env")  # PlatformIO SCons environment -- injected at build time


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _parse_partition_csv(csv_path):
    """
    Parse an ESP32 partition CSV file.
    Skips blank lines and comment lines (starting with #).
    Returns a list of dicts: {name, type, subtype, offset, size}.
    """
    partitions = []
    with open(csv_path, "r", encoding="ascii") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 5:
                continue
            partitions.append({
                "name":    parts[0],
                "type":    parts[1],
                "subtype": parts[2],
                "offset":  parts[3],
                "size":    parts[4],
            })
    return partitions


def _find_partition(partitions, label):
    """Return the partition dict whose name matches label, or None."""
    for p in partitions:
        if p["name"] == label:
            return p
    return None


def _parse_int(value):
    """Parse '0x100000' or '1048576' to int. Strips whitespace."""
    value = value.strip()
    return int(value, 16) if value.lower().startswith("0x") else int(value)


def _resolve_csv_path(env):
    """
    Resolve the absolute path to the partition CSV.
    Checks project root first, then the framework-bundled partitions folder.
    """
    csv_name = env.GetProjectOption("board_build.partitions", "default_8MB.csv")
    local = os.path.join(env["PROJECT_DIR"], csv_name)
    if os.path.isfile(local):
        return local
    framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
    bundled = os.path.join(framework_dir, "tools", "partitions", csv_name)
    if os.path.isfile(bundled):
        return bundled
    return None


# ---------------------------------------------------------------------------
# Patch FS_START and FS_SIZE
# ---------------------------------------------------------------------------

label = env.GetProjectOption("custom_partition_label", "littlefs_sys")

csv_path = _resolve_csv_path(env)
if not csv_path:
    csv_name = env.GetProjectOption("board_build.partitions", "default_8MB.csv")
    sys.stderr.write("[uploadfs] ERROR: partition CSV not found: {}\n".format(csv_name))
    env.Exit(1)

partitions = _parse_partition_csv(csv_path)
part = _find_partition(partitions, label)
if not part:
    available = [p["name"] for p in partitions]
    sys.stderr.write(
        "[uploadfs] ERROR: partition '{}' not found in {}\n"
        "[uploadfs]        Available: {}\n".format(label, csv_path, available)
    )
    env.Exit(1)

offset = _parse_int(part["offset"])
size   = _parse_int(part["size"])

# Patch the two variables the built-in uploadfs pipeline uses:
#   FS_START -> flashing offset (last element of UPLOADERFLAGS, passed to esptool)
#   FS_SIZE  -> LittleFS image size (passed to mklittlefs via the build target)
prev_start = env.get("FS_START", None)
prev_size  = env.get("FS_SIZE",  None)
env.Replace(FS_START=offset, FS_SIZE=size)

print("[uploadfs] Partition : '{}'".format(label))
if isinstance(prev_start, int):
    print("[uploadfs] FS_START  : 0x{:08X} -> 0x{:08X}".format(prev_start, offset))
else:
    print("[uploadfs] FS_START  : {} -> 0x{:08X}".format(prev_start, offset))
if isinstance(prev_size, int):
    print("[uploadfs] FS_SIZE   : 0x{:X} -> 0x{:X} ({} KB)".format(prev_size, size, size // 1024))
else:
    print("[uploadfs] FS_SIZE   : {} -> 0x{:X} ({} KB)".format(prev_size, size, size // 1024))

