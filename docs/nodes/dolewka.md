# DOLEWKA — lista punktów

Projekt: `top_off_water_new-iot` (firmware `df4a2dc`), XIAO ESP32-C3, 192.168.10.2.
Wersja 2: 2026-11-14 — po raporcie zwrotnym `dolewka_raport_2026-10-31.md` i decyzjach z `docs/PLAN_DOLEWKA.md`.
Wersja 1 (2026-10-25, commit `b13197d`) — w historii gita.

Automatyczna dolewka wody RO: pływak w sumpie wyzwala stałą dawkę (`dose_ml`), algorytm śledzi tempo
parowania (EMA) i liczy `alarm_score` anomalii. Zbiornik RO mierzony czujnikiem ToF VL53L4CD, który też
zatrzymuje pompę na poziomie alarmowym.

Transport do panelu: ESP-NOW, protokół dolewki (HMAC, `cmd_nonce`) — ramki w `docs/PLAN_DOLEWKA.md`.

## Sprzęt

| Element | Rola | Źródło |
|---|---|---|
| Pływak w sumpie | wejście — wyzwala cykl (debounce 10 × 1 s) | `hardware/water_sensors.*`, `algorithm_config.h` |
| Pompa ATO (przekaźnik) | aktuator — dawka czasowa z kalibracji (`portion_ms`) | `hardware/pump_controller.*` |
| VL53L4CD | poziom w zbiorniku RO, stop pompy na poziomie alarmowym | `hardware/level_sensor.*`, `hardware/reserve_controller.*` |
| Buzzer | tylko alarmy krytyczne (rezerwa bez buzzera) | `hardware/buzzer_controller.*` |
| Przycisk | kasowanie alarmów (przytrzymanie 3 s) | |
| DS3231 + FRAM | czas, konfiguracja, zatrzaski błędów, historia; awaria = `HW_FAULT` | `hardware/hw_health.*` |

Pływak zbiornika RO usunięty (`1aa8eea`, GPIO 8 wolne).

## Aktuatory — FMEA

| Aktuator | Błąd / skutek | Ochrona na węźle | Komendy z panelu |
|---|---|---|---|
| Pompa ATO | zablokowany pływak → przelanie sumpa | limit dobowy sprawdzany przed dawką, `alarm_score` → ERROR (zatrzask w FRAM) | — |
| Pompa ATO | pusty zbiornik → praca na sucho | ToF: poziom ≤ alarm albo dawka by się nie zmieściła → `LOW_RESERVOIR` (zatrzask), szacunek przy braku odczytu | — |
| Pompa ATO | brak dolewki (pływak, pompa) | watchdog 24 h → `NO_TOPOFF` | — |
| Pompa ATO (porcje ręczne) | za dużo wody ręcznie | tylko Service Mode, kolejka ≤ 20, Service wraca do Auto po 180 min; nie blokowane limitem (świadoma akcja na miejscu) | `PORTIONS(n)`, n = 1–10 |
| Pompa ATO (porcje ręczne) | duplikat komendy (ponowienie) / odtworzona ramka | `cmd_id` (ponowienie = ten sam ACK) + `cmd_nonce` (stary nonce = odmowa) | |

## Parametry

ID niezamrożone (przenumerowanie dozwolone do implementacji tabeli `ParamDesc`). Kolumna „Panel”: `tak` = w statusie
ESP-NOW i na ekranie, `web` = tylko web GUI.

| ID | key | Etykieta | Kat. | Typ | Dostęp | Jedn. | Zakres / krok | Panel | Safety | Faza | Źródło / uwagi |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `node_status` | Status | ST | ENUM `OK\|Uwaga\|Alarm\|Awaria\|Serwis` | RO | | | tak | | MVP | nowe, wyliczane w węźle — reguła niżej |
| 1 | `system_enabled` | Automatyka | MD | BOOL `Serwis\|Auto` | RW | | | tak | 0 | MVP | `/api/system-toggle` `enabled=0\|1`; odmowa w kalibracji; bez dialogu (2026-11-14) |
| 2 | `alarm_mute` | — | | | | | | — | | — | nie będzie: alarm kasuje się tylko resetem (2026-11-14) |
| 3–8 | wspólne | | | | | | | diag | | MVP | wg `_konwencje.md`; `time_quality` w statusie |
| 16 | `algo_state` | Stan | ST | ENUM `Czuwa\|Debounce\|Pompuje\|Zapis\|Błąd` | RO | | | tak | | MVP | `AlgorithmState` |
| 17 | `error_code` | Błąd | ST | ENUM `—\|Limit dob.\|Anomalia\|Oba\|Pusty zb.` | RO | | | tak | | MVP | `ErrorCode`; dziś brak w `/api/status` — dodać (D1) |
| 18 | `sump_low` | Poziom sumpa | ST | BOOL `OK\|Niski` | RO | | | tak | | MVP | `sensor_active` |
| 19 | `pump_on` | Pompa | ST | BOOL | RO | | | tak | | MVP | `isPumpActive()` |
| 20 | `phase_remaining` | Pozostało | RO | INT | RO | s | | tak | | MVP | `remaining_seconds` |
| 21 | `volume_24h` | Dolano 24 h | RO | INT | RO | ml | | tak | | MVP | `getRolling24hVolume()`; niezależny od RTC; bez porcji ręcznych |
| 22 | `daily_limit` | Limit dobowy | SP | INT | RW | ml | 100–10000 / 50, ≥ 2 × dawka | tak | 1 | MVP | `/api/set-fill-water-max`; panel: −/+ z dialogiem (2026-11-14); domyślne 2000 ml za mało przy 2–4 l/dobę |
| 23 | `dose` | Dawka | CF | INT | RO | ml | | tak | | MVP | w statusie do przeliczeń (porcje → ml); ustawiana kalibracją |
| 24 | `evap_rate` | Parowanie | RO | FIX1 | RO | ml/h | | web | | później | `ema_rate_ml_h` |
| 25 | `alarm_zone` | Trend | ST | ENUM `OK\|Uwaga\|Anomalia` | RO | | | web | | później | z `alarm_score` vs `zone_green/yellow` |
| 26 | `alarm_score` | Score | DG | FIX1 | RO | | | web | | później | |
| 27 | `zone_green` | Próg żółty | CF | FIX1 | RW | | 1,0–200,0 | web | | później | `/api/alg-config` |
| 28 | `zone_yellow` | Próg czerwony | CF | FIX1 | RW | | > zone_green, ≤ 500 | web | | później | `/api/alg-config` |
| 29 | `initial_ema` | Seed EMA | CF | FIX1 | RW | ml/h | 1,0–500,0 | web | | później | `/api/alg-config` |
| 30 | `reserve_zone` | Zbiornik RO | ST | ENUM `OK\|Rezerwa\|Alarm\|Nieznany` | RO | | | tak | | MVP | `res_zone` 0–3 |
| 31 | `reserve_fill` | Zbiornik | RO | INT | RO | % | 0–100 | tak | | MVP | `res_pct` (napełnienie); pasek na panelu |
| 32 | `reserve_portions` | Zapas dolewek | RO | INT | RO | szt. | | tak | | MVP | `res_portions` — ponad poziomem alarmowym |
| 33 | `reserve_ml` | Zapas | RO | INT | RO | ml | | web | | później | `res_ml`, `res_avail_ml` |
| 34 | `manual_portions` | Dolej porcje | AC | ACTION + INT | X | szt. | 1–10 | tak | 0 | MVP | `PORTIONS(n)`; tylko Service Mode; panel: bargraf + ON bez dialogu (2026-11-14); dziś `/api/pump/direct-on` po 1 porcji |
| 35 | `manual_left` | W kolejce | RO | INT | RO | szt. | 0–20 | tak | | MVP | `manual_left` (z bieżącą) + wykonane w sesji |
| 36 | `pump_stop` | STOP | AC | ACTION | X | | | tak | 0 | MVP | `/api/pump/stop` — kończy bieżącą porcję, czyści kolejkę |
| 37 | `error_reset` | Kasuj alarm | AC | ACTION | X | | | tak | 1 | MVP | `/api/system-reset` = przycisk 3 s; blokowany w fazie zapisu; ACK zwraca alarmy, które trwają |
| 38 | `volume_24h_reset` | Zeruj licznik 24 h | AC | ACTION | X | | | web | 2 | później | `/api/reset-daily-volume` — znosi limit dobowy |
| 39 | `pump_calibrated` | Kalibracja | ST | BOOL | RO | | | tak | | MVP | `cal_calibrated`; brak → `node_status = Uwaga` |
| 40 | `level_sensor` | Czujnik ToF | DG | ENUM `brak\|ok\|awaria` | RO | | | tak | | MVP | `level_sensor` |
| 41 | `reserve_threshold` | Próg rezerwy | CF | INT | RW | % | do 90, ≥ alarm + 2 porcje | web | | MVP | `reserve_pct`, `/api/reserve-config`; panel tylko rysuje kreskę |
| 42 | `alarm_threshold` | Próg alarmu | CF | INT | RW | % | 5–50 | web | | MVP | `alarm_pct`, `/api/reserve-config`; panel tylko rysuje kreskę |
| 43 | `reserve_hours` | Zapas czasu | RO | FIX1 | RO | h | | tak | | MVP | `res_hours_left` (−1 = brak tempa) |
| 44 | `reserve_estimated` | Szacunek | ST | BOOL | RO | | | tak | | MVP | `res_estimated` — brak odczytu ToF, wartość minimalna |
| 45 | `service_left` | Do Auto | RO | INT | RO | min | | tak | | MVP | pozostały czas Service Mode (180 min ręcznie, 15 min po starcie) |
| 46 | `reserve_refilled` | Uzupełniono zbiornik | AC | ACTION | X | | | web | 1 | MVP | `/api/reserve/refill`, tylko gdy ToF nie mierzy — wyłącznie web GUI (2026-11-14) |

Tylko web GUI (bez ID): kalibracja pompy, geometria zbiornika, diagnostyka ToF (`level_mm`, `sd`, `signal`,
`/api/level-events`), historia cykli, statystyki błędów.

Reguła `node_status` (pierwsza pasująca):

| Status | Warunek |
|---|---|
| Awaria | `HW_FAULT` |
| Alarm | dowolny inny alarm P1 (`LOW_RESERVOIR`, `DAILY_LIMIT`, `RATE_ANOMALY`, `NO_TOPOFF`) |
| Serwis | Service Mode |
| Uwaga | alarm P3 albo pompa niekalibrowana |
| OK | pozostałe |

## Alarmy

| Kod | Warunek | Prio | Skutek / węzeł | Reakcja | Kasowanie | Pola w `/api/status` |
|---|---|---|---|---|---|---|
| `LOW_RESERVOIR` | ToF: poziom ≤ alarm, dawka by się nie zmieściła, szacunek doszedł do alarmu, brak odczytu 60 s po starcie bez danych w FRAM | P1, buzzer | zatrzask (FRAM), brak porcji auto; ręczna — odmowa | uzupełnić zbiornik, skasować | reset przy zmierzonym poziomie ≥ alarm + 1 porcja; bez odczytu — „Uzupełniono zbiornik” (web) | `reserve_empty`, `reserve_alarm` (1 poziom, 2 szacunek, 3 brak danych, 4 dawka) |
| `DAILY_LIMIT` | `rolling + dose > limit` przed dawką | P1, buzzer | ERROR (FRAM), auto zablokowane | sprawdzić wyciek / pływak, ewent. podnieść limit, skasować | reset | `system_error`, `error_code` = 1 |
| `RATE_ANOMALY` | `alarm_score ≥ zone_yellow` | P1, buzzer | ERROR (FRAM); po resecie w czerwonej strefie rozbrojenie ≤ 24 h | sprawdzić pływak, parowanie, skasować | reset | `system_error`, `error_code` = 2, `alarm_armed` |
| `NO_TOPOFF` | 24 h bez dolewki auto | P1, buzzer | bez blokady | sprawdzić pływak i pompę | następna dolewka albo reset | `no_topoff_alarm` |
| `HW_FAULT` | FRAM lub RTC nie działa (co 60 s + po błędzie transferu) | P1, buzzer | ATO działa dalej (RAM, limit dobowy) | naprawa sprzętu | reset, gdy blok znów działa | `hw_alarm`, `hw_alarm_text`, `fram_ok`, `rtc_ok` |
| `RESERVE_LOW` | strefa Rezerwa (≤ `reserve_threshold`) | P3, bez buzzera | — | uzupełnić wkrótce | auto | `reserve_low` |
| `LEVEL_NO_READING` | 30 min bez ważnego odczytu ToF | P3, bez buzzera | szacunek (porcja liczona podwójnie) od 5 min | sprawdzić czujnik | auto | `reserve_no_reading` |
| `LEVEL_SENSOR_FAULT` | ToF w FAULT (3 złe serie) | P3 | dziś niewidoczny w GUI przez 5–30 min (plan rezerwy §8) | sprawdzić czujnik | auto | `level_sensor` = `fault` |
| `TOPOFF_OFFLINE` | panel: brak statusu > 15 s (3 × 5 s) | P2 | sterowanie na panelu zablokowane, dolewka działa sama | sprawdzić zasilanie / radio | auto | — (panel) |

Wyciszania buzzera z panelu nie ma (2026-11-14): alarm ma wymusić działanie, kasuje go tylko reset
(panel, web GUI albo przycisk 3 s).

## Do ustalenia

- [x] ~~Pływak zbiornika RO czy stop na ToF?~~ (2026-10-25) ToF z poziomem alarmowym, pływak usunięty.
- [x] ~~Czy ręczne porcje liczą się do limitu dobowego?~~ Nie — tylko Service Mode, nigdzie niewliczane (raport, odp. 6.1).
- [x] ~~`daily_limit` na panelu czy tylko web?~~ Panel, z dialogiem (2026-11-14).
- [x] ~~Ręczna porcja: safety?~~ Bargraf 1–10 + ON bez dialogu (2026-11-14).
- [ ] `ERROR_BOTH` (kod 3) nieużywany — usunąć z enum czy zostawić (raport N1).
- [ ] Pompa niekalibrowana: tylko `Uwaga` czy blokada auto (raport N2, pytanie 6.4 planu rezerwy).
- [ ] `LEVEL_SENSOR_FAULT` widoczny od razu czy po 5 / 30 min (plan rezerwy §8).
- [ ] Nazwa węzła w top-barze web GUI = nazwa z provisioningu (`_konwencje.md`, „Do sprawdzenia”).
