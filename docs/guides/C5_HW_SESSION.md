# C-5 Hardware Session — Coin Measurement Log

**Протокол:** `p3_MIKROE3240_b06_012mm`  
**Базовий spacer:** 0.6mm (3D-друк, hw-verified 2026-03-26)  
**Addon spacers:** +1mm, +2mm  
**Ефективні відстані:** 0.6 / 1.6 / 2.6 mm  
**Монети:** bare (без капсули)  
**Cycles per coin:** 5  

---

## Обладнання

| Компонент | Деталь |
|---|---|
| Сенсор | MIKROE-3240 (LDC1101), 909.2 kHz |
| МК | M5Stack Cardputer (ESP32-S3) |
| Firmware | `ce056ca` feat(p3) |
| Base spacer | 0.6mm (3D-друк, PLA) |
| Addon spacers | +1mm, +2mm (3D-друк, PLA) |

---

## Монети C-5

### Монета 1 — American Silver Eagle 1oz

| Параметр | Значення |
|---|---|
| `coin_name` | American Silver Eagle 1oz |
| `metal_code` | XAG999 |
| Склад | Ag 99.9% |
| Діаметр | 40.6 mm |
| Маса | 31.1 g |
| Рік | 1991 |
| Нотатки | bare, no capsule; перший тест C-5 |
| Виміри (ID) | 56–60 |

---

### Монета 2 — Російська імперія 5 копійок

| Параметр | Значення |
|---|---|
| `coin_name` | Russian Empire 5 Kopecks 1867–1917 |
| `metal_code` | XCU |
| Склад | Cu 100% |
| Діаметр | 32.6 mm |
| Маса | 16.4 g |
| Товщина | 2.1 mm |
| Рік | 1870 |
| Каталог | Y# 12 |
| Нотатки | чиста мідь, ребристий гурт, повністю перекриває котушку |
| Виміри (ID) | 61–65 |

---

### Монета 3 — Україна 10 гривень 2022

| Параметр | Значення |
|---|---|
| `coin_name` | Ukraine 10 UAH 2022 — Territorial Defence Forces |
| `metal_code` | XZNNIP |
| Каталог | UC# 101 |
| Склад | Цинк з нікелевим покриттям (Zn core + Ni plating) |
| Діаметр | 23.5 mm |
| Маса | 6.4 g |
| Товщина | 2.3 mm |
| Рік | 2022 |
| Тип | Пам'ятна монета |
| Нотатки | Не є феромагнетиком (Zn немагнітний); тонке Ni покриття може дати слабкий dL1 сигнал |
| Виміри (ID) | 66–70 |

---

### Монета 4 — Німеччина 1½ Євро 1997

| Параметр | Значення |
|---|---|
| `coin_name` | Germany 1.5 Euro 1997 — European Week Berlin |
| `metal_code` | XFE |
| Каталог | X# 5 |
| Склад | Сталь з мідним покриттям (Fe core + Cu plating) |
| Діаметр | 31.0 mm |
| Маса | 11.95 g |
| Товщина | 2.5 mm |
| Рік | 1997 |
| Тип | Пам'ятна монета (ECU серія) |
| Магнітна | ✅ Так (сталевий сердечник, μr >> 1) |
| Нотатки | Єдина феромагнітна монета в наборі; чекаємо великий dL1_n; Cu покриття дасть помірний dRp1 |
| Виміри (ID) | 71–75 |

---

### Монета 5 — Німеччина 50 пфенігів 1919–1922

| Параметр | Значення |
|---|---|
| `coin_name` | Germany 50 Pfennig 1919–1922 Weimar Republic |
| `metal_code` | XAL |
| Каталог | KM# 27 |
| Склад | Алюміній 100% (Al) |
| Діаметр | 23.0 mm |
| Маса | 1.6 g |
| Товщина | 1.5 mm |
| Рік | 1919–1922 |
| Тип | Обігова монета (Веймарська республіка) |
| Магнітна | ❌ Ні |
| Нотатки | Найлегша монета набору; σ(Al) ≈ 38 MS/m → dRp1 між Ag і Zn; тонка (1.5mm) — можлива слабша зміна між позиціями |
| Виміри (ID) | 76–80 |

---

## Процедура одного циклу

```
1. Покласти монету на base spacer (0.6mm) → натиснути ENTER/BtnA
   Дисплей: "Coin on base (d~0.6 mm)" → STEP_BASE
2. Покласти +1mm addon spacer ЗВЕРХУ монети → ENTER/BtnA
   Дисплей: "Add 1mm spacer (d~1.6 mm)" → STEP_1
3. Покласти +2mm addon spacer ЗВЕРХУ → ENTER/BtnA
   Дисплей: "Add 2mm spacer (d~2.6 mm)" → STEP_DRIFT
4. ⚠️ Зняти ТІЛЬКИ addon spacers (+1mm та +2mm)
   Монета ЗАЛИШАЄТЬСЯ на 0.6mm base spacer → натиснути ENTER/BtnA
   Drift check: rp[3] має бути ≈ rp[0] (< 5% відхилення)
5. Зняти монету → пристрій повертається в IDLE

> **Кнопка ENTER:** клавіша ↵ у правому нижньому куті міні-клавіатури (зручніша за бокову BtnA)
```

> **Порядок монет:** Eagle → 5 коп. (Cu) → 10 грн (Zn+Ni) → 1½ EUR (Fe+Cu) → 50 Pfennig (Al)  
> **Між монетами:** пристрій сам повертається в IDLE — просто міняти монету.

---

## Дані після hw-сесії

### Файл вимірів

Скачати командою (замінити IP):
```powershell
$ip = "192.168.88.53"
$status = Invoke-RestMethod "http://$ip/api/v1/status"
$count = $status.meas_count
$results = @()
for ($i = [Math]::Max(0, $count - 25); $i -lt $count; $i++) {
    $m = Invoke-RestMethod "http://$ip/api/v1/measure/$i"
    $results += $m
    Write-Host "[$i] rp=[$($m.rp -join ', ')] conf=$($m.conf) metal=$($m.metal_code)"
}
$results | ConvertTo-Json -Depth 5 | Out-File "c5_measurements.json"
Write-Host "Saved: c5_measurements.json"
```

### Надати Copilot для розрахунку

- Файл `c5_measurements.json`
- Маппінг: "ID 56-60 = Eagle, 61-65 = 5коп. Cu, 66-70 = 10грн (Zn+Ni), 71-75 = 1½ EUR (Fe+Cu), 76-80 = 50 Pfennig (Al)"

Copilot обчислить:
- Fingerprint vectors (dRp1_n, k1, k2, slope, dL1_n) з реальних rp[]/l[]
- Оптимальне `CONFIDENCE_SIGMA`
- Готові JSON записи для `index.json`
- Виправлені OLS константи slope() для x={0,1,2}

---

## Чеклист

- [x] Firmware `ce056ca` прошито
- [x] LittleFS (uploadfs-sys) залито
- [x] SD seed скопійовано: `SD:\CoinTrace\database\index.json` ✓
- [x] Serial monitor відкрито (COM4, 115200) — лог "FingerprintCache ready — 5 entries"
- [x] Монета 1 (Eagle): 5 циклів ✓ (IDs 56–60)
- [x] Монета 2 (5 коп Cu): 5 циклів ✓ (IDs 61–65)
- [x] Монета 3 (10 грн): 5 циклів ✓ (IDs 66–70) — підтверджено XZNNIP
- [x] Монета 4 (1½ EUR Fe+Cu): 5 циклів ✓ (IDs 71–75)
- [x] Монета 5 (50 Pfennig Al): 5 циклів ✓ (IDs 76–80)
- [x] `c5_measurements.json` скачано
- [x] Дані передано Copilot для розрахунку C-5
- [x] slope() виправлено, CONFIDENCE_SIGMA=0.35, index.json generation=2
- [x] 122/122 native tests PASSED
- [ ] SD карта фізично оновлена: скопіювати `data/sd_seed/.../index.json` → `SD:\CoinTrace\database\index.json`
- [ ] Верифікація на пристрої: Serial повинен показати "FingerprintCache ready — 5 entries (generation 2)"
- [ ] Eagle на котушку → `m` → очікується `conf > 0.80`
- [ ] `git commit` feat(C-5): real fingerprint DB + sigma tuning
