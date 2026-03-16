// SDCardManager.cpp — SD Card Archive Tier (Wave 7 P-4)
// CoinTrace — Open Source Inductive Coin Analyzer
// License: GPL v3

#include "SDCardManager.h"
#include <Arduino.h>  // log_e / log_i / log_w / millis()

// ── tryMount() ────────────────────────────────────────────────────────────────

bool SDCardManager::tryMount(SPIClass* spi, SemaphoreHandle_t spiMutex) {
    // [SD-A-01] Null-guard: cannot operate without SPI bus handle + mutex.
    if (!spi || !spiMutex) {
        log_e("SDCard: tryMount() — null spi or spiMutex [SD-A-01]");
        return false;
    }
    spi_      = spi;
    spiMutex_ = spiMutex;

    // [SPI-3] 50 ms timeout — portMAX_DELAY is FORBIDDEN (ADR-ST-008).
    if (xSemaphoreTake(spiMutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        log_w("SDCard: tryMount() — SPI mutex timeout [SPI-3]");
        return false;
    }
    available_ = SD.begin(SD_CS, *spi_, SPI_FREQ);
    xSemaphoreGive(spiMutex_);

    if (available_) {
        log_i("SDCard: mounted — type=%u size=%lluMB",
              (unsigned)SD.cardType(),
              (unsigned long long)(SD.cardSize() / (1024ULL * 1024ULL)));
    } else {
        log_w("SDCard: tryMount() — card absent or unreadable (non-fatal; LittleFS-only mode)");
    }
    return available_;
}

// ── umount() ──────────────────────────────────────────────────────────────────

void SDCardManager::umount() {
    if (!available_) return;
    if (xSemaphoreTake(spiMutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
        SD.end();
        xSemaphoreGive(spiMutex_);
    }
    available_ = false;
    log_i("SDCard: unmounted");
}

// ── ensureDirLocked() ─────────────────────────────────────────────────────────
// [SD-A-04] Caller MUST hold spiMutex_ before calling this function.
// SD.mkdir() creates all intermediate path components on the fly.

void SDCardManager::ensureDirLocked(const char* dir) {
    if (!SD.exists(dir)) {
        SD.mkdir(dir);
    }
}

// ── writeMeasurement() ────────────────────────────────────────────────────────

bool SDCardManager::writeMeasurement(uint32_t globalId, const uint8_t* buf, size_t len) {
    if (!available_ || !buf || len == 0) return false;

    char path[64];
    snprintf(path, sizeof(path), "/CoinTrace/measurements/m_%06lu.json",
             (unsigned long)globalId);

    // [SD-A-02] [SPI-4] Single atomic spiMutex_ scope: mkdir + open + write + close.
    if (xSemaphoreTake(spiMutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        log_w("SDCard: writeMeas(%lu) — SPI mutex timeout [SPI-3]", (unsigned long)globalId);
        return false;
    }

    ensureDirLocked("/CoinTrace/measurements");

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        xSemaphoreGive(spiMutex_);
        log_e("SDCard: writeMeas — SD.open(%s) failed", path);
        return false;
    }
    f.write(buf, len);
    f.close();
    xSemaphoreGive(spiMutex_);

    log_i("SDCard: measurement %lu archived → %s (%u B)",
          (unsigned long)globalId, path, (unsigned)len);
    return true;
}

// ── copyLogToSD() ─────────────────────────────────────────────────────────────

bool SDCardManager::copyLogToSD(LittleFSManager& lfs, const char* lfsPath) {
    if (!available_) return false;

    // Destination: /CoinTrace/logs/log_<uptime_s:010d>.jsonl
    // uptime (seconds) provides a monotonic, collision-avoiding file name.
    char sdPath[80];
    snprintf(sdPath, sizeof(sdPath), "/CoinTrace/logs/log_%010lu.jsonl",
             (unsigned long)(millis() / 1000UL));

    // ── Phase 1: create SD destination file ──────────────────────────────────
    // [SA2-6] Separate spiMutex_ scope — no lfsDataMutex held at this point.
    if (xSemaphoreTake(spiMutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        log_w("SDCard: copyLog — SPI mutex timeout (create) [SPI-3]");
        return false;
    }
    ensureDirLocked("/CoinTrace/logs");
    File dst = SD.open(sdPath, FILE_WRITE);
    xSemaphoreGive(spiMutex_);

    if (!dst) {
        log_e("SDCard: copyLog — SD.open(%s) failed", sdPath);
        return false;
    }

    // ── Phase 2: open LittleFS source ────────────────────────────────────────
    // Brief lfsDataMutex scope — just to open a read handle.
    SemaphoreHandle_t& lfsMutex = lfs.lfsDataMutex();

    if (xSemaphoreTake(lfsMutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        log_w("SDCard: copyLog — LFS mutex timeout (open)");
        // Clean up the SD file that was already created.
        if (xSemaphoreTake(spiMutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
            dst.close();
            xSemaphoreGive(spiMutex_);
        }
        return false;
    }
    File src = lfs.data().open(lfsPath, "r");
    xSemaphoreGive(lfsMutex);  // release immediately after open

    if (!src) {
        // log.1 may not exist before the first rotation — silent non-error.
        if (xSemaphoreTake(spiMutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
            dst.close();
            xSemaphoreGive(spiMutex_);
        }
        return false;
    }

    // ── Phase 3: alternating per-chunk stream copy ────────────────────────────
    // Per iteration: LFS mutex (read COPY_CHUNK bytes) → release
    //              → SPI mutex (write COPY_CHUNK bytes) → release
    // [SA2-6] Never holds lfsDataMutex and spiMutex_ simultaneously.
    uint8_t buf[COPY_CHUNK];
    bool ok = true;

    for (;;) {
        // --- LittleFS read phase ---
        size_t n = 0;
        if (xSemaphoreTake(lfsMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (src.available()) n = src.read(buf, COPY_CHUNK);
            xSemaphoreGive(lfsMutex);  // release BEFORE acquiring SPI mutex
        } else {
            log_w("SDCard: copyLog — LFS mutex timeout (read chunk)");
            ok = false;
            break;
        }

        if (n == 0) break;  // EOF — copy complete

        // --- SD write phase ---
        if (xSemaphoreTake(spiMutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
            dst.write(buf, n);
            xSemaphoreGive(spiMutex_);  // release BEFORE re-acquiring LFS mutex
        } else {
            log_w("SDCard: copyLog — SPI mutex timeout (write chunk) [SPI-3]");
            ok = false;
            break;
        }
    }

    // ── Phase 4: close both handles ───────────────────────────────────────────
    if (xSemaphoreTake(lfsMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        src.close();
        xSemaphoreGive(lfsMutex);
    }
    if (xSemaphoreTake(spiMutex_, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) == pdTRUE) {
        dst.close();
        xSemaphoreGive(spiMutex_);
    }

    if (ok) log_i("SDCard: log archived → %s", sdPath);
    return ok;
}
