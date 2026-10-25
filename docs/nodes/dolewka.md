# DOLEWKA — lista punktów

Projekt: `top_off_water_new-iot` (commit `b13197d`), XIAO ESP32-C3, 192.168.10.2.
Wersja 1: 2026-10-25 — propozycja z ekstrakcji kodu, do przeglądu.

Automatyczna dolewka wody RO: pływak w sumpie wyzwala stałą dawkę (`dose_ml`), algorytm śledzi tempo
parowania (EMA) i liczy `alarm_score` anomalii. Zbiornik RO mierzony czujnikiem ToF VL53L4CD.

## Sprzęt

| Element | Rola | Źródło |
|---|---|---|
| Pływak w sumpie | wejście — wyzwala cykl (debounce 10 × 1 s) | `hardware/water_sensors.*`, `algorithm_config.h` |
| Pompa ATO (przekaźnik) | aktuator — dawka czasowa z kalibracji (`portion_ms`) | `hardware/pump_controller.*` |
| VL53L4CD | poziom w zbiorniku RO (informacyjnie) | `hardware/level_sensor.*` |
| Pływak zbiornika RO | stop przy pustym zbiorniku | `reserve_controller.h` — wyłączony: `RESERVE_FLOAT_ENABLED 0` |
| Buzzer | OFF / WARNING / ALARM | `hardware/buzzer_controller.*` |
| DS3231 + FRAM | czas, konfiguracja, historia | |

## Aktuatory — FMEA

| Aktuator | Błąd / skutek | Ochrona na węźle | Safety komend |
|---|---|---|---|
| Pompa ATO | zablokowany pływak → przelanie sumpa | limit dobowy (`daily_limit_ml`), `alarm_score` (red alert) → ERROR latch | ręczna porcja: 1 |
| Pompa ATO | pusty zbiornik → praca na sucho | brak sprzętowo (pływak zbiornika wyłączony); ToF tylko informacyjnie | — |
| Pompa ATO | duplikat ręcznej porcji (retry) | kolejka max 20 porcji, pauza 5 s; brak idempotencji | `cmd_id` wymagany |

## Parametry

| ID | key | Etykieta | Kat. | Typ | Dostęp | Jedn. | Zakres / krok | Panel | Safety | Odśw. | Faza | Źródło / uwagi |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0–8 | wspólne | | | | | | | | | | MVP | `system_enabled` = `/api/system-toggle` (blokowany w trakcie kalibracji) |
| 16 | `algo_state` | Stan | ST | ENUM `Czuwa\|Debounce\|Pompuje\|Zapis\|Błąd` | RO | | | tak | | zm | MVP | `AlgorithmState` |
| 17 | `error_code` | Błąd | ST | ENUM `—\|Limit dob.\|Anomalia\|Oba\|Pusty zb.` | RO | | | tak | | zm | MVP | `ErrorCode` |
| 18 | `sump_low` | Poziom sumpa | ST | BOOL `OK\|Niski` | RO | | | tak | | zm | MVP | `sensor_active` |
| 19 | `pump_on` | Pompa | ST | BOOL | RO | | | tak | | zm | MVP | `isPumpActive()` |
| 20 | `phase_remaining` | Pozostało | RO | INT | RO | s | | tak | | zm | MVP | `remaining_seconds` |
| 21 | `volume_24h` | Dolano 24 h | RO | INT | RO | ml | | tak | | zm | MVP | `getRolling24hVolume()` |
| 22 | `daily_limit` | Limit dobowy | CF | INT | RW | ml | 100–10000 / 50 | web? | 1 | zm | MVP | `/api/set-fill-water-max` |
| 23 | `dose` | Dawka | CF | INT | RO | ml | | web | | zm | później | ustawiana tylko kalibracją |
| 24 | `evap_rate` | Parowanie | RO | FIX1 | RO | ml/h | | tak | | zm | MVP | `ema_rate_ml_h` |
| 25 | `alarm_zone` | Trend | ST | ENUM `OK\|Uwaga\|Anomalia` | RO | | | tak | | zm | MVP | z `alarm_score` vs `zone_green/yellow` |
| 26 | `alarm_score` | Score | DG | FIX1 | RO | | | diag | | zm | później | |
| 27 | `zone_green` | Próg żółty | CF | FIX1 | RW | | 1,0–200,0 | web | 1 | zm | później | `/api/alg-config` |
| 28 | `zone_yellow` | Próg czerwony | CF | FIX1 | RW | | > zone_green, ≤ 500 | web | 1 | zm | później | `/api/alg-config` |
| 29 | `initial_ema` | Seed EMA | CF | FIX1 | RW | ml/h | 1,0–500,0 | web | 1 | zm | później | `/api/alg-config` |
| 30 | `reserve_zone` | Zbiornik RO | ST | ENUM `OK\|Niski\|Pusty\|?` | RO | | | tak | | zm | MVP | `res_zone`; niski = 30 % |
| 31 | `reserve_pct` | Zapas | RO | INT | RO | % | | tak | | zm | MVP | `res_pct` |
| 32 | `reserve_portions` | Zapas dolewek | RO | INT | RO | szt. | | tak | | zm | MVP | `res_portions` |
| 33 | `reserve_ml` | Zapas | RO | INT | RO | ml | | web | | zm | później | `res_ml` |
| 34 | `manual_portion` | Dolej porcję | AC | ACTION | X | | | tak | 1 | | MVP | `/api/pump/direct-on`; odrzuca przy pustym zbiorniku i kalibracji |
| 35 | `manual_queue` | W kolejce | RO | INT | RO | szt. | 0–20 | tak | | zm | MVP | `manual_left` |
| 36 | `pump_stop` | Stop pompy | AC | ACTION | X | | | tak | 0 | | MVP | `/api/pump/stop` |
| 37 | `error_reset` | Kasuj błąd | AC | ACTION | X | | | tak | 1 | | MVP | `/api/system-reset`; blokowany w fazie zapisu |
| 38 | `volume_24h_reset` | Zeruj licznik 24 h | AC | ACTION | X | | | web | 2 | | później | `/api/reset-daily-volume` — w praktyce znosi limit dobowy |
| 39 | `pump_calibrated` | Kalibracja | ST | BOOL | RO | | | diag | | zm | MVP | `cal_calibrated`; brak → `node_status = Uwaga` |
| 40 | `level_sensor_state` | Czujnik ToF | DG | ENUM | RO | | | diag | | zm | później | `getLevelSensorStateString()` |

Tylko web GUI (bez ID na panelu): kalibracja pompy (procedura 4 przebiegów), geometria zbiornika
(`/api/reserve-config`), diagnostyka ToF (`level_mm`, `sd`, `signal`), historia cykli, statystyki
błędów, log zdarzeń czujnika.

## Alarmy

| Kod | Warunek | Prio | Skutek / węzeł | Reakcja | Kasowanie | Źródło |
|---|---|---|---|---|---|---|
| `LOW_RESERVOIR` | zbiornik RO pusty | P1 | ERROR, pompa zablokowana | uzupełnić zbiornik, skasować błąd | latch + `error_reset` | `ERROR_LOW_RESERVOIR` |
| `DAILY_LIMIT` | dolano ≥ `daily_limit` w 24 h | P2 | ERROR, pompa zablokowana | sprawdzić wyciek / pływak, skasować | latch + reset | `ERROR_DAILY_LIMIT` |
| `RATE_ANOMALY` | `alarm_score ≥ zone_yellow` | P2 | ERROR, pompa zablokowana | sprawdzić pływak, parowanie, skasować | latch + reset | `ERROR_RED_ALERT` |
| `RESERVE_LOW` | zapas < 30 % | P3 | buzzer WARNING | uzupełnić wkrótce | auto | `res_zone = low` |
| `LEVEL_SENSOR_FAULT` | ToF w stanie FAULT | P3 | zapas nieznany | sprawdzić czujnik | auto | `level_event_log` |
| `RTC_BATTERY` | wspólny | P3 | | wymienić baterię | auto | |

`alarm_zone = Uwaga` (score między progami) to status, nie alarm — nie wymaga reakcji.

## Do ustalenia

- [x] ~~Pływak zbiornika RO czy stop na ToF?~~ (2026-10-25) Pływak usuwany z kodu. W ustawieniach dochodzi
      poziom alarmowy (stop) poniżej zwiększonego poziomu rezerwy. Realizacja i audyt dolewki:
      `top_off_water_new-iot/docs/AUDYT_I_PLAN_REZERWY_TOF_1.0.md`. Ta lista zostanie zaktualizowana
      po raporcie zwrotnym (sekcja 7 tamtego pliku).
- [ ] Czy ręczne porcje liczą się do limitu dobowego? W `pump_controller.cpp` nie ma sprawdzenia limitu.
- [ ] `daily_limit` na panelu czy tylko web? (zmiana rzadka; argument za panelem: podniesienie po podmianie wody)
- [ ] Ręczna porcja: safety 1 wystarczy (150 ml, limit kolejki), czy 2?
