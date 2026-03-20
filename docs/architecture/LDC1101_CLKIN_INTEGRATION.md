# LDC1101 CLKIN Integration Guide (ESP32)

Версія: 1.0.0
Дата: 2026-03-20

Мета
- Надати детальний технічний гайд для інженерів з інтеграції зовнішнього тактового сигналу (CLKIN) до LDC1101 при використанні ESP32 плат (зокрема ESP32-S3 на M5Stack Cardputer-Adv).
- Документ призначено як доповнення до `LDC1101_ARCHITECTURE.md` і як чеклист та приклади для реалізації ADR-LHR-001.

Область застосування
- Hardware: MIKROE-3240 (LDC1101 Click) або кастомні плати на базі LDC1101
- MCU: ESP32 family (ESP32, ESP32-S2, ESP32-S3)
- Режим: тільки LHR (24-bit). RP+L (16-bit) не вимагає зовнішнього CLKIN.

Короткий висновок
- ESP32 може апаратно генерувати 16 MHz сигнал без постійного використання CPU.
- Рекомендований primary спосіб: SPI SCLK (master) @16 MHz з DMA (або постійний великий буфер)
- Альтернативи: LEDC (апаратний PWM), MCPWM, RMT/I2S (залежить від платформи)

1. Чому потрібен CLKIN
- LHR режим використовує зовнішній або внутрішній reference clock для підрахунку тактів у RCOUNT-вікні (Eq.11 даташиту) і точного визначення fSENSOR.
- Якщо LHR працює без стабільного CLKIN — значення fSENSOR будуть некоректні або підвищено шумні.

2. Які GPIO та пінування
- На MIKROE-3240 mikroBUS Pin 16 — підписаний як PWM (CLKIN). На M5 Cardputer-Adv з'ясуйте який GPIO відповідає цьому pin'у та встановіть константу `CLKIN_GPIO`.
- JP1 на платі MIKROE-3240 повинен бути в позиції Left (SDO) для коректної роботи SPI.
- Рівень сигналу: 3.3V. Заборонено подавати 5V.

3. Варіанти апаратної генерації (детально)

3.1 SPI SCLK + DMA (рекомендація)
- Ідея: налаштувати SPI master зі швидкістю 16 MHz, маршрутизувати SCLK на `CLKIN_GPIO`. Запустити DMA-трансфер великого буфера (наприклад 2–8 KB), апарат SPI формує SCLK під час DMA.
- Переваги:
  - Низький джиттер і висока стабільність
  - CPU лише ініціює трансфер і рідко переповнює буфер
  - Легко вивести SCLK на будь-який GPIO через GPIO matrix (ESP32)
- Недоліки:
  - Потрібно механізм підживлення DMA буфера; налаштування трохи складніше

3.2 LEDC (PWM) / MCPWM
- Ідея: використати LEDC або MCPWM апаратний таймер для створення 16 MHz квадратної хвилі на GPIO.
- Переваги:
  - Просте API (`ledcSetup`, `ledcAttachPin` в Arduino/IDF wrapper)
  - Після налаштування не навантажує CPU
- Недоліки:
  - Деякі ревізії чіпа або режими LEDC можуть не коректно досягати 16 MHz або мати більший джиттер, ніж SPI

3.3 RMT / I2S
- Використовується коли потрібні складніші шаблони або синхронізація з аудіопотоком. Для постійного 16 MHz значно складніше і рідко має переваги.

4. Порівняння: джиттер, стабільність, простота
- SPI+DMA: найнижчий джиттер, найвища стабільність, середня складність
- LEDC: середній джиттер, дуже проста реалізація, потрібно перевірити апаратно
- RMT: високий контроль над таймінгом, але не оптимально для простої 16 MHz задачі

5. ESP-IDF приклад: SPI SCLK @ 16 MHz + DMA

Примітка: замініть `CLKIN_GPIO` на фактичний GPIO для вашої плати (мікроBUS Pin16 mapping).

```cpp
// clkin_spi_dma_example.cpp (ESP-IDF)
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "esp_log.h"

#define SPI_HOST_ID HSPI_HOST
#define CLKIN_GPIO   21 // <<< замініть на ваше значення

static const char *TAG = "clkin_spi_dma";

void clkin_task(void *arg)
{
    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = -1,
        .sclk_io_num = CLKIN_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096
    };

    esp_err_t ret = spi_bus_initialize(SPI_HOST_ID, &buscfg, 1); // use DMA channel 1
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %d", ret);
        vTaskDelete(NULL);
        return;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 16000000, // 16 MHz
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 2,
    };

    spi_device_handle_t handle;
    ret = spi_bus_add_device(SPI_HOST_ID, &devcfg, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %d", ret);
        vTaskDelete(NULL);
        return;
    }

    static uint8_t dma_buf[4096];
    memset(dma_buf, 0xFF, sizeof(dma_buf));

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = sizeof(dma_buf) * 8; // bits
    t.tx_buffer = dma_buf;

    while (1) {
        ret = spi_device_transmit(handle, &t);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "spi_device_transmit failed: %d", ret);
        }
        // adjust sleep to reduce overhead; choose large buffer to minimize frequency of transmit calls
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main()
{
    xTaskCreatePinnedToCore(clkin_task, "clkin_task", 4096, NULL, 5, NULL, 0);
}
```

Пояснення: апарат SPI формує SCLK на `CLKIN_GPIO` під час DMA трансферу; CPU тільки періодично викликає `spi_device_transmit` для перезапуску трансферу.

6. ESP-IDF / Arduino приклад: LEDC (альтернатива)

```cpp
// ledc_clkin_example.cpp
#include "driver/ledc.h"
#include "esp_err.h"

#define CLKIN_GPIO 21 // <<< замініть

void ledc_clkin_init()
{
    ledc_timer_config_t ledc_timer = {
        .duty_resolution = LEDC_TIMER_1_BIT,
        .freq_hz = 16000000,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ch = {
        .channel = LEDC_CHANNEL_0,
        .duty = 1,
        .gpio_num = CLKIN_GPIO,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .hpoint = 0,
        .timer_sel = LEDC_TIMER_0
    };
    ledc_channel_config(&ch);
}

void app_main() {
    ledc_clkin_init();
    while(1) vTaskDelay(pdMS_TO_TICKS(1000));
}
```

7. Інтеграційні тести та валідація
- Перевірити сигнал осцилографом: 3.3V TTL, 16.000 MHz, duty ≈ 50%
- Перевірити, що JP1 = Left, SDO доступний
- Firmware: при активації LHR, робити `calibrate()` і перевірити, що `fSENSOR` обчислюється і `lhrRaw` веде себе адекватно
- Моніторити CPU load: steady-state CPU usage ≈ 0% для LEDC; для SPI+DMA — незначна періодична активність

8. Checklist для розробника (copy into ADR-LHR-001)
- [ ] Визначити `CLKIN_GPIO` mapping → mikroBUS Pin 16
- [ ] JP1 position = Left (SDO)
- [ ] Implement SPI SCLK @16MHz or LEDC
- [ ] Verify with oscilloscope
- [ ] Set `clkin_freq_hz` = 16000000 in `data/plugins/ldc1101.json`
- [ ] Run `calibrate()` LHR and verify results

9. Notes & Risks
- Якщо LEDC не може видати 16 MHz на вашому ревізіонному ESP32, переключайтесь на SPI+DMA.
- Для production: документуйте `CLKIN_GPIO` mapping для кожної таргет-плати в репозиторії.

---
Автор та джерела
- TI LDC1101 Datasheet
- ESP-IDF peripherals documentation

---

10. Wiring & PCB Integration (detailed)

This section is intended for PCB designers and hardware integrators. It expands the short checklist above into actionable steps that must be completed and documented before production.

10.1 Board mapping and metadata
- For every supported carrier board add a JSON metadata entry under `boards/` (or the equivalent board config) containing at least:

```json
{
    "board": "m5cardputer-adv",
    "ldc1101": {
        "clk_in_gpio": 21,          // verified GPIO number for mikroBUS Pin 16
        "spi_cs_pin": 5,
        "jp1_required_position": "left",
        "clkin_source_default": "spi"
    }
}
```

10.2 PCB routing checklist (to include in PCB review)
- Place the LDC1101 as close as possible to the mikroBUS connector.
- Route `CLKIN` as a short single-ended trace on the top layer; target < 50 mm trace length.
- Provide a continuous ground plane directly beneath `CLKIN` route; avoid splits under the trace.
- Add a 22–47 Ω series resistor near the MCU output pin (for traces > 20 mm).
- Minimize vias on the `CLKIN` net; if unavoidable, keep via count low and place vias near source side.
- Keep `CLKIN` away from switching regulators, USB data, or high-current traces.
- Place the 0.1 µF decoupling capacitor within 1–2 mm of the LDC1101 VDD pin.

10.3 Connector and cable guidance
- If an external cable is used to connect the Click board, use a short well-grounded cable and pair `CLKIN` with its ground return.
- Do not rely on long ribbon cables for `CLKIN` (high jitter and reflections).

10.4 Test points and production checks
- Add labeled test points: `TP_LDC1101_CLKIN`, `TP_LDC1101_CS`, `TP_LDC1101_MISO`, `TP_LDC1101_SCLK`.
- Production bring-up test steps:
    1. With LDC1101 unpopulated, enable MCU generator and verify 16 MHz at `TP_LDC1101_CLKIN` (scope): 3.3 V TTL, ±0.5% freq.
    2. Populate LDC1101 and verify SPI `DEVICE_ID` == 0xD4.
    3. Run `calibrate()` for LHR and confirm `lhrRaw` stability and plausible `fSENSOR`.

10.5 Firmware integration actions
- Add board-init code that reads board metadata and starts the chosen `clkin_source` (`spi` or `ledc`) during bring-up.
- Provide runtime diagnostic that logs `clkin_freq_hz` and whether the chosen generator is running. Example log entry: `LDC1101 CLKIN: source=spi, gpio=21, freq=16MHz, status=running`.
- Add an optional self-test that toggles the generator and records a short SPI read of `DEVICE_ID` to confirm synchronous behavior.

10.6 Documentation and traceability
- Link `LDC1101_CLKIN_INTEGRATION.md` from main `LDC1101_ARCHITECTURE.md` §2 and ADR-LHR-001.
- Store the per-board `CLKIN_GPIO` mapping and the results of the oscilloscope acceptance test in the board's revision notes in `boards/`.

---

End of integration additions.
