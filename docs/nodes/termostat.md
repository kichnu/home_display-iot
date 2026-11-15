# TERMOSTAT — lista punktów

Projekt: `thermo_control-iot` (commit `7746a4b`), Waveshare ESP32-S3-Pico, 192.168.10.4.
Wersja 1: 2026-10-25 — propozycja z ekstrakcji kodu, do przeglądu.

Sterowanie temperaturą akwarium 240 l: grzałka przez SSR (on/off z histerezą), wentylator PWM (rampa od
`fan_on_point = target_temp + thermal_buffer`), czujnik STS35, licznik energii impulsowy, buzzer.

## Sprzęt

| Element | Rola | Źródło |
|---|---|---|
| STS35 (I2C) | temperatura wody, odczyt co 15 s, retry 3× | `hardware/temp_sensor.*` |
| SSR grzałki | aktuator on/off | `isHeaterOn()`, `setHeaterOn/Off()` |
| Wentylator PWM | aktuator 0–100 %, min. `fan_min_pct` | `setFanSpeed()` |
| Licznik energii (impulsy) | kWh dziś / razem, moc W | `hardware/energy_meter.*` |
| Buzzer | ALARM / WARNING | |
| DS3231, FRAM SPI | czas, konfiguracja, historia, alarmy | |

## Aktuatory — FMEA

| Aktuator | Błąd / skutek | Ochrona na węźle | Safety komend |
|---|---|---|---|
| Grzałka | przegrzanie (zła nastawa, awaria czujnika) | awaria czujnika = latch, grzałka i wentylator stop; alarm HIGH | `target_temp`: 1 |
| Grzałka | nie grzeje (spalona, brak zasilania) | alarm trendu (brak wzrostu o 0,2 °C w 60 min przy grzaniu) | — |
| Grzałka / wentylator | test ręczny zostaje włączony | Service Mode wraca do Auto po 15 min | tylko web |
| Konfiguracja | sprzeczne progi (wentylator walczy z grzałką) | walidacja zależności w `/api/thermo-config` | tylko web |

## Parametry

| ID | key | Etykieta | Kat. | Typ | Dostęp | Jedn. | Zakres / krok | Panel | Safety | Odśw. | Faza | Źródło / uwagi |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0–8 | wspólne | | | | | | | | | | MVP | `system_enabled` = `/api/system-toggle` (Service Mode, auto-powrót po 15 min); `alarm_mute` = `/api/mute-alarm` |
| 16 | `temp` | Temperatura | RO | FIX1 | RO | °C | | tak | | 15s | MVP | `temp_c` (z `temp_offset`) |
| 17 | `algo_state` | Stan | ST | ENUM `Czuwa\|Grzeje\|Chłodzi\|Za zimno\|Za ciepło\|Awaria czujnika\|Grzałka nieskuteczna` | RO | | | tak | | zm | MVP | `AlgorithmState` |
| 18 | `heater_on` | Grzałka | ST | BOOL | RO | | | tak | | zm | MVP | `heater_on` |
| 19 | `fan_pct` | Wentylator | RO | INT | RO | % | 0–100 | tak | | zm | MVP | `fan_pct` |
| 20 | `in_buffer_zone` | Bufor ciepła | ST | BOOL | RO | | | tak | | zm | później | „cicha uwaga”: temp ≥ target, wentylator jeszcze nie |
| 21 | `target_temp` | Temp. zadana | SP | FIX1 | RW | °C | 22,0–30,0 / 0,1 ? | tak | 1 | zm | MVP | zakres w firmware niewalidowany — do dodania |
| 22 | `service_remaining` | Serwis jeszcze | RO | INT | RO | s | | tak | | zm | MVP | `auto_enable_remaining_s` |
| 23 | `sensor_fault_reset` | Kasuj awarię czujnika | AC | ACTION | X | | | tak | 1 | | MVP | `/api/reset-sensor-fault` |
| 24 | `energy_today` | Energia dziś | RO | FIX2 | RO | kWh | | tak | | 60s | MVP | `energy_kwh_today` |
| 25 | `power` | Moc | RO | INT | RO | W | | web | | 15s | później | `getPowerWatt()` |
| 26 | `energy_total` | Energia razem | RO | FIX1 | RO | kWh | | web | | 60s | później | `/api/energy` |
| 27 | `heat_hyst` | Histereza grzania | CF | FIX1 | RW | °C | ≥ 0,1 | web | 1 | zm | później | |
| 28 | `thermal_buffer` | Bufor dzień/noc | CF | FIX1 | RW | °C | ≥ `cool_start` | web | 1 | zm | później | |
| 29 | `cool_start` | Histereza chłodz. | CF | FIX1 | RW | °C | ≥ 0,1 | web | 1 | zm | później | |
| 30 | `cool_full` | Wentylator 100 % | CF | FIX1 | RW | °C | > 0, < `alarm_delta_high` | web | 1 | zm | później | |
| 31 | `fan_min_pct` | Min. wentylatora | CF | INT | RW | % | 0–100 | web | 1 | zm | później | |
| 32 | `alarm_delta_low` | Alarm poniżej | CF | FIX1 | RW | °C | > `heat_hyst` | web | 1 | zm | później | względem `target_temp` |
| 33 | `alarm_delta_high` | Alarm powyżej | CF | FIX1 | RW | °C | > `cool_full` | web | 1 | zm | później | względem `fan_on_point` |
| 34 | `trend_delta` | Trend: wzrost | CF | FIX1 | RW | °C | > 0 | web | 1 | zm | później | |
| 35 | `trend_window` | Trend: okno | CF | INT | RW | min | ≥ 5 | web | 1 | zm | później | |
| 36 | `temp_offset` | Kalibracja | CF | FIX1 | RW | °C | | web | 1 | zm | później | |

Tylko web GUI: testy grzałki i wentylatora (`/api/test/*`), historia godzinowa i dobowa, log alarmów,
`pulses_per_kwh`, kasowanie liczników energii (dzień / razem z PIN-em), seed / clear historii.

## Alarmy

| Kod | Warunek | Prio | Skutek / węzeł | Reakcja | Kasowanie | Źródło |
|---|---|---|---|---|---|---|
| `SENSOR_FAULT` | 3 kolejne nieudane cykle odczytu STS35 | P1 | grzałka i wentylator stop (latch), buzzer ALARM bez wyciszania (też w Serwisie) | sprawdzić czujnik / kabel, skasować | latch + `sensor_fault_reset` | `ALARM_FLAG_SENSOR` |
| `HW_FAULT` | FRAM / DS3231 / dane Wi-Fi z fallbacku (`hw_health`, 2026-11-15) | P1 | buzzer ALARM bez wyciszania, regulacja działa dalej | naprawić blok, potwierdzić | latch + Acknowledge (web GUI) | `hw_alarm` |
| `TEMP_LOW` | temp < target − `alarm_delta_low` | P2 | buzzer ALARM, wyciszalny (epizod) | sprawdzić grzałkę | auto (histereza) | `ALARM_FLAG_TEMP_LOW` |
| `TEMP_HIGH` | temp > `fan_on_point` + `alarm_delta_high` | P2 | buzzer ALARM, wyciszalny (epizod) | sprawdzić wentylator / otoczenie | auto | `ALARM_FLAG_TEMP_HIGH` |
| `HEATER_INEFFECTIVE` | grzanie ≥ `trend_window`, wzrost < `trend_delta` | P3 | ostrzeżenie bez buzzera (2026-11-15), trwa, dopóki grzałka grzeje bez wzrostu | sprawdzić grzałkę / konfigurację | auto | `ALARM_FLAG_TREND` |
| `SERVICE_MODE` | `system_enabled = 0` | P3 | sterowanie wyłączone, auto-powrót 15 min | — (informacja) | auto | `isSystemDisabled()` |
| `RTC_BATTERY` | wspólny | P3 | | wymienić baterię | auto | |

`in_buffer_zone` to status, nie alarm. Zasada (2026-11-15): alarm wymagający interwencji jest zatrzaśnięty i bez
wyciszania — buzzer milknie dopiero po usunięciu przyczyny i skasowaniu. Wyciszenie (`alarm_mute`) tylko dla
samoczynnie mijających `TEMP_LOW` / `TEMP_HIGH` (przyczyna bywa poza zasięgiem, np. upał; bez Mute jedyną ciszą byłby
Serwis, który wyłącza regulację), tylko bieżący epizod — ponowne wystąpienie znów dzwoni.

## Do ustalenia

- [ ] Zakres `target_temp` dla panelu (proponuję 22,0–30,0 °C) — w firmware dziś brak walidacji zakresu.
- [ ] `SERVICE_MODE` jako alarm P3 czy tylko status (kolor `Serwis` na kafelku)?
- [ ] Testy grzałki / wentylatora na panelu (np. przy serwisie w szafce) czy tylko web?
