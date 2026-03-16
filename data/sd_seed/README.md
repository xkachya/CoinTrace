# SD Card Seed Data — CoinTrace P-4 Development

Цей каталог містить **синтетичні тестові дані** для P-4 розробки (FingerprintCache).

## Що тут є

```
CoinTrace/database/
├── index.json                              ← Quick Screen индекс (5 металів)
└── samples/
    └── p1_UNKNOWN_013mm/                   ← protocol_id (частота уточниться після R-01)
        ├── XAG999/  (Срібло 999)
        │   ├── synthetic_ag999_001.json
        │   └── _aggregate.json
        ├── XAG925/  (Срібло 925)
        │   ├── synthetic_ag925_001.json
        │   └── _aggregate.json
        ├── XAG833/  (Срібло 833)
        │   ├── synthetic_ag833_001.json
        │   └── _aggregate.json
        ├── XCU/     (Мідь)
        │   ├── synthetic_xcu_001.json
        │   └── _aggregate.json
        └── XNI/     (Нікель — феромагнітний!)
            ├── synthetic_xni_001.json
            └── _aggregate.json
```

## Як записати на SD карту

1. Вийняти microSD з Cardputer
2. Вставити в Card Reader (USB або вбудований)
3. Скопіювати папку `CoinTrace\` до **кореня** SD карти
4. Повернути SD в Cardputer

Результат на SD: `SD:\CoinTrace\database\index.json`, `SD:\CoinTrace\database\samples\...`

## ⚠️ ВАЖЛИВО — це НЕ реальні виміри

```
status: "reviewed"
reference_quality: "unknown"
notes: "SYNTHETIC TEST DATA — не використовувати для реальної ідентифікації"
```

Вектори є **теоретично обґрунтованими** (формули з FINGERPRINT_DB_ARCHITECTURE.md), але
НЕ виміряними реальним LDC1101. Вони призначені виключно для тестування логіки FingerprintCache
до прибуття MIKROE-3240 (R-01).

## Фізичні характеристики металів у тестових даних

| Метал  | dRp1 (Ω) | k1    | k2    | dL1 (µH) | Особливість          |
|--------|----------|-------|-------|----------|----------------------|
| XAG999 | 380.0    | 0.870 | 0.620 | 20.0     | Найвища провідність  |
| XAG925 | 345.0    | 0.858 | 0.591 | 18.5     | Стерлінгове срібло   |
| XAG833 | 312.4    | 0.856 | 0.587 | 18.0     | З прикладу в spec    |
| XCU    | 290.0    | 0.820 | 0.545 | 15.0     | Мідь                 |
| XNI    | 180.0    | 0.780 | 0.480 | 350.0    | ⚠️ Феромагнітний!    |

Нікель відрізняється великим `dL1` (~350 µH vs ~18 µH у срібла) — головний дискримінатор.

## protocol_id = "p1_UNKNOWN_013mm"

Частота fSensor MIKROE-3240 ≈ 200–500 kHz (НЕ 1 MHz!). Уточниться після R-01
(першого реального вимірювання). Після підтвердження — замінити на фінальний `protocol_id`
і перегенерувати `index.json`.
