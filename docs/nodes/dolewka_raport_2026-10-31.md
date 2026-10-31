# Raport zwrotny dla `home_display-iot` — dolewka

Stan: 2026-10-31, firmware `top_off_water_new-iot` `df4a2dc` (gałąź `main`). Dotyczy listy punktów
`dolewka.md` (wersja 1 z `b13197d`) — sekcja 7 planu
`top_off_water_new-iot/docs/AUDYT_I_PLAN_REZERWY_TOF_1.0.md` (dalej: „plan”).

Uwaga: zmiany z kroków 4–7 i `hw_health` są wgrane, ale część nie przeszła jeszcze testów na sprzęcie
(plan §8). Wnioski z diagnostyki padającego czujnika (W4) — jeszcze brak.

## 1. Commity od `b13197d`

| Commit | Zmiana |
|---|---|
| `1aa8eea` | pływak zbiornika RO usunięty (GPIO 8 wolne) |
| `27f8ab6` | `reserve_pct` / `alarm_pct`, nowe strefy rezerwy |
| `cda8f67` | stop pompy na poziomie alarmowym ToF, szacunek bez odczytu, „Reservoir refilled” |
| `1fd31b9` | zatrzaśnięte błędy ATO w FRAM, jeden reset, przycisk 3 s, odroczony restart nocny |
| `ed4ab37` | porcje ręczne tylko w Service Mode, bez blokad i bez wliczania |
| `2412e74` | limit dobowy sprawdzany przed dawką, limit ≥ 2 × dawka, licznik 24 h niezależny od RTC |
| `b576de1` | watchdog 24 h bez dolewki → alarm krytyczny |
| `bb64166` | alarm krytyczny FRAM / RTC (`hw_health`) |
| `17abeba` | hasło OTA per urządzenie |
| `da185cb` | seria ToF ważna tylko przy sd ≤ 10 mm |
| `9684313` | GUI: linia statusu (poziom wody / pompa / alarm zbiornika) |
| `df4a2dc` | `/api/system-toggle` z `enabled=0\|1` (idempotentny) |

## 2. Punkty audytu (sekcja 2 planu)

| # | Stan |
|---|---|
| K1 porcje ręczne | zrobione inaczej niż w propozycji (odp. 6.1): tylko Service Mode, bez blokad i wliczania |
| K2 stop przy pustym zbiorniku | zrobione — ToF, poziom alarmowy |
| K3 brak dolewki | zrobione — 24 h bez dolewki auto → alarm krytyczny |
| W1 błędy w RAM | zrobione — FRAM `AlgErrorState`, restart nocny odkładany |
| W2 przycisk reset | zrobione — przytrzymanie 3 s, 2 piknięcia |
| W3 dwa resety | zrobione — jeden `resetSystem()`, rozbrojenie anomalii max 24 h |
| W4 mutex I2C | odłożone (krok 8, najpierw test) |
| W5 limit przed dawką | zrobione |
| S1 hasło OTA | zrobione |
| S2 whitelist logu TCP | odłożone |
| S3 czas RTC | zrobione — cykl bez ważnego czasu dolewa, bez EMA / interwału / rekordu |
| S4 idempotentne komendy | `system-toggle` zrobione; `cmd_id` dla `direct-on` — tylko jeśli porcja ręczna będzie w panelu |
| S5 `alarm_mute` | odłożone |
| N1 `ERROR_BOTH` | otwarte (nieużywany) |
| N2 pompa niekalibrowana | otwarte (pytanie 6.4 bez odpowiedzi) |
| N3 `manualResetTs` w RAM | otwarte (do udokumentowania) |

## 3. Odpowiedzi na pytania (sekcja 6 planu)

1. Porcje ręczne: świadoma dolewka serwisowa — nigdzie niewliczane (tylko odejmowanie wody w rezerwie),
   nieblokowane, tylko w Service Mode; przejście do Auto przerywa sesję. Service Mode ręczny wraca do
   Auto po 180 min, po starcie po 15 min. **Po wdrożeniu panelu dolewka ręczna znika z GUI WWW** —
   działanie tylko na miejscu (panel).
2. Ukryte dno 10 % — usunięte, zastąpione poziomem alarmowym.
3. Rezerwa: bez buzzera — tylko GUI / panel (żółty status). Buzzer tylko przy alarmach krytycznych.
4. Pompa niekalibrowana — bez decyzji.
5. Brak odczytu od startu: ostatni odczyt i pobrana woda w FRAM, restart kontynuuje. Bez niczego w FRAM:
   auto zablokowane do pierwszego odczytu, po 60 s alarm krytyczny.
6. Watchdog braku dolewki: stałe 24 h, alarm krytyczny z buzzerem, bez blokady.

## 4. Parametry — zmiany względem listy punktów

| ID / key | Zmiana |
|---|---|
| 0–8 `system_enabled` | `POST /api/system-toggle` z `enabled=1` (Auto) / `enabled=0` (Service) — stan docelowy, ponowienie nic nie zmienia (timer Service Mode zostaje); odpowiedź `enabled`, `changed`; 409 w trakcie kalibracji. Bez `enabled` — przełączanie (stare zachowanie) — panel ma zawsze wysyłać `enabled` |
| 17 `error_code` | `/api/status` **nie ma pola z kodem błędu** — jest `system_error` (bool) i `state_description` (tekst). Kody: 0 brak, 1 limit dobowy, 2 anomalia, 3 oba (nieużywany), 4 pusty zbiornik. Jeśli panel ma pokazywać ENUM — dodać pole w węźle (do zrobienia) |
| 18 `sump_low` | bez zmian (`sensor_active`) |
| 22 `daily_limit` | walidacja: 100–10000 ml i ≥ 2 × `dose_ml`; sprawdzany przed dawką (`rolling + dose > limit` → ERROR). Uwaga: domyślne 2000 ml za mało przy 2–4 l/dobę |
| 30 `reserve_zone` | `res_zone`: 0 OK, 1 REZERWA, 2 ALARM, 3 NIEZNANY |
| nowe `reserve_pct` | % pojemności do znacznika, domyślnie 30, max 90, rezerwa ≥ alarm + 2 porcje; RW przez `/api/reserve-config` (form) |
| nowe `alarm_pct` | % pojemności, domyślnie 15, zakres 5–50; RW przez `/api/reserve-config` |
| 31 / 32 / 33 | `res_pct`, `res_portions` (porcje **ponad poziomem alarmowym**), `res_ml`; nowe: `res_avail_ml`, `res_reading` (pomiar), `res_estimated` (szacunek — wartość minimalna), `res_hours_left` |
| 34 `manual_portion` | tylko w Service Mode; poza nim odmowa. Nie blokowana przez błędy / limit / rezerwę. Do usunięcia z WWW po wdrożeniu panelu |
| 37 `error_reset` | jeden reset (`/api/system-reset` = przycisk 3 s); kasuje też zatrzaski: rezerwy (tylko przy zmierzonym poziomie ≥ alarm + 1 porcja), `NO_TOPOFF`, `hw_alarm` (tylko gdy blok działa) |
| nowa akcja `reserve_refilled` | `POST /api/reserve/refill` — tylko gdy czujnik ToF nie mierzy: szacunek od pełnego zbiornika, kasuje alarm zbiornika. Wymaga potwierdzenia (safety 1) |
| usunięte | `reserve_float` (pływak zbiornika) |
| 39 `pump_calibrated` | bez zmian (`cal_calibrated`) |
| 40 `level_sensor_state` | `level_sensor`: `absent` / `ok` / `fault` |

## 5. Alarmy — nowa tabela

Wspólne pole dla panelu: `GET /api/health` (bez logowania, whitelist IP) → `alarm` (bool, każdy alarm
krytyczny = ten z buzzerem) i `alarm_text` (lista, separator ` | `).

| Kod | Warunek | Prio | Skutek | Kasowanie | Pola w `/api/status` |
|---|---|---|---|---|---|
| `LOW_RESERVOIR` | ToF: poziom ≤ alarm, dawka by się nie zmieściła, szacunek doszedł do alarmu, brak odczytu 60 s po starcie bez danych w FRAM | P1, buzzer ALARM | zatrzask w FRAM, brak nowej porcji auto (ręczna — odmowa bez zatrzasku) | reset przy zmierzonym poziomie ≥ alarm + 1 porcja albo „Reservoir refilled” bez odczytu | `reserve_empty`, `reserve_alarm` (1 poziom, 2 szacunek, 3 brak danych, 4 dawka), `reserve_alarm_text` |
| `DAILY_LIMIT` | `rolling + dose > limit` przed dawką | P1, buzzer | ERROR (zatrzask w FRAM), pompa auto zablokowana | reset | `system_error`, `state_description` |
| `RATE_ANOMALY` | `alarm_score ≥ zone_yellow` | P1, buzzer | ERROR (FRAM); po resecie w czerwonej strefie rozbrojenie max 24 h | reset | `system_error`, `alarm_armed`, `alarm_disarmed_until` |
| `NO_TOPOFF` | 24 h bez dolewki auto (okresy bez możliwości dolewki liczą od nowa) | P1, buzzer | bez blokady | następna dolewka albo reset | `no_topoff_alarm`, `no_topoff_ref` |
| `HW_FAULT` | FRAM lub RTC nie działa (co 60 s + po błędzie transferu) | P1, buzzer | ATO działa dalej (RAM, limit dobowy) | reset, gdy blok znowu działa | `hw_alarm`, `hw_alarm_text`, `hw_alarm_since`, `fram_ok`, `rtc_ok` |
| `RESERVE_LOW` | strefa REZERWA (≤ `reserve_pct`) | P3, **bez buzzera** | — | auto | `reserve_low` |
| `LEVEL_NO_READING` | 30 min bez ważnego odczytu ToF | P3, bez buzzera | szacunek (każda porcja liczona podwójnie) od 5 min | auto | `reserve_no_reading`, `res_no_reading_s` |
| `LEVEL_SENSOR_FAULT` | ToF w FAULT (3 złe serie) | P3 | dziś **niewidoczny w GUI** do 5 min / 30 min — do decyzji (plan §8) | auto | `level_sensor` = `fault` |

Persystencja: ERROR ATO, czas rozbrojenia anomalii, odniesienie watchdoga — FRAM `AlgErrorState`;
alarm zbiornika — FRAM `ReserveTrack`; `hw_alarm` — pamięć RTC ESP (przeżywa restart programowy).
Restart nocny odkładany przy pompie, cyklu, sesji ręcznej, kalibracji i w Service Mode.

`alarm_mute` — jeszcze nie ma (S5).

## 6. Diagnostyka padającego czujnika ToF

- Potwierdzone: VL53L4CD resetuje się przy przełączaniu pompy — wykrywanie self-resetu i re-init
  (`7891d81`), log zdarzeń w FRAM (`/api/level-events`).
- W4 (kolizje I2C z handlerami WWW) — niezweryfikowane; test: doba z zamkniętym vs otwartym dashboardem.
- Seria z rozrzutem > 10 mm (np. poruszony czujnik) traktowana jako zła (`da185cb`).
