# Plan wdrożenia panelu — etap 3: termostat

Stan: 2026-11-15, szkic do akceptacji (pytania na końcu).

Kontekst: `docs/nodes/termostat.md` (lista punktów v1), `docs/PLAN_DOLEWKA.md` (wzorzec: transport, etapy,
pending/ACK), `~/dev-knowledge/esp32/espnow-sync.md`. Projekt węzła: `thermo_control-iot` (ESP32-S3,
192.168.10.4, commit `7746a4b`).

## Zakres (ustalenia 2026-11-15)

Na panelu: odczyty z karty „System Status” web GUI (bez energii) i wszystkie elementy karty „System Control”.
Bez wykresów, bez energii, bez ustawień algorytmu (te zostają w web GUI).

| Funkcja | Na panelu | Źródło w węźle |
|---|---|---|
| Temperatura | tylko odczyt (bez temp. zadanej), duża wartość, kolor jak w web GUI (grzanie / bufor / chłodzenie / alarm, alarm niski osobnym kolorem) | `getTemperature()` |
| Stan algorytmu | Czuwa / Grzeje / Chłodzi / Za zimno / Za ciepło / Awaria czujnika / Grzałka nieskuteczna | `AlgorithmState` |
| Grzałka, wentylator | kontrolki stanu zawsze widoczne: grzałka ON/OFF, wentylator ON/OFF + % | `isHeaterOn()`, `getFanSpeedPct()` |
| Alarmy | pasek alarmów + lista na L2 | `alarm_flags`, `sensor_fault_latched`, `hw_alarm` |
| Auto ⇄ Serwis | przełącznik bez dialogu (jak dolewka), w Serwisie odliczanie do Auto (15 min) | `system-toggle` → idempotentny `SET_MODE` |
| Wycisz alarm | przycisk, bez dialogu, aktywny tylko przy `TEMP_LOW` / `TEMP_HIGH` | `muteActiveAlarms()` |
| Kasuj awarię czujnika | przycisk z dialogiem (safety 1) | `resetSensorFaultLatch()` |
| Grzałka ręcznie | przełącznik ON/OFF, tylko w Serwisie | `/api/test/heater` |
| Wentylator ręcznie | przełącznik ON/OFF + krok ▼▲ 5 %, tylko w Serwisie | `/api/test/fan` |

Poza panelem: energia (dziś / razem / moc, kasowanie liczników), wykresy i historia, log alarmów, ustawienia
algorytmu (`/api/thermo-config`), PIN-lock, seed/clear historii.

## Transport

Jak dolewka (decyzja 2026-11-14, wariant A dla kolejnych węzłów): ESP-NOW, własny protokół termostatu, HMAC-SHA256/16
z kluczem z hasła Wi-Fi i kontekstem `THERMO-ESPNOW-v1:`, nonce dla komend. W panelu adapter `src/thermo/` obok
`src/topoff/`; listener lamp przekazuje ramki z magic termostatu (jak dolewki).

| Ramka | Kierunek | Treść |
|---|---|---|
| `TH_STATUS` | termostat → broadcast, co 5 s + od razu po zmianie | nazwa, fw, temp ×100, `algo_state`, flaga „bufor” (kolor temp.), grzałka, wentylator %, bufor, bity alarmów, alarm wyciszony, latch czujnika, Serwis + sekundy do Auto, `cmd_nonce`, UTC |
| `TH_CMD` | panel → termostat | `cmd_id`, `cmd_nonce`, komenda, argument |
| `TH_ACK` | termostat → panel | `cmd_id`, wynik, wartość faktyczna, alarmy po komendzie |

Komendy: `SET_MODE` (1 Auto / 0 Serwis, idempotentnie), `ALARM_MUTE`, `SENSOR_RESET`, `HEATER` (0/1, tylko Serwis),
`FAN` (0–100 %, tylko Serwis). Wyniki ACK: `OK`, `NOT_SERVICE`, `SENSOR_FAULT` (ręczne sterowanie przy latchu —
do ustalenia w kodzie, co dziś robi web), `STALE_NONCE`, `OUT_OF_RANGE`, `BAD_CMD`.

## Ekrany

- Kafelek L1 „Termostat”: temperatura (duża), linia 2: stan / „Serwis · Auto za N min”; kolor wg najgorszego alarmu,
  Serwis tylko kolorem (bez alarmu).
- Ekran L2: podsumowanie (status, pierwszy alarm, stan) · duża temperatura z grzałką i wentylatorem · rząd 1:
  Auto, Serwis, Wycisz alarm, Kasuj awarię czujnika · rząd 2 (aktywny tylko w Serwisie): Grzałka ON/OFF,
  Wentylator ON/OFF, ▼ % ▲.
- Każdy przycisk: „wysłano” do ACK i następnego statusu, potem toast z wynikiem (jak dolewka).

## Alarmy na panelu

| Kod | Prio | Kasowanie |
|---|---|---|
| `SENSOR_FAULT` (latch) | P1 | „Kasuj awarię czujnika” (bez wyciszania) |
| `HW_FAULT` (latch: FRAM / RTC / Wi-Fi z fallbacku) | P1 | potwierdzenie w web GUI (bez wyciszania) |
| `TEMP_LOW`, `TEMP_HIGH` | P2 | auto; „Wycisz alarm” do końca epizodu |
| `HEATER_INEFFECTIVE` (trend) | P3 | auto; ostrzeżenie bez buzzera |
| `THERMO_OFFLINE` (panel) | P2 | auto po powrocie statusu |

## Zmiany w firmware termostatu

1. `src/network/espnow_panel.*` jak w dolewce: init po Wi-Fi, `WiFi.setSleep(false)`, kolejka RX, obsługa w `loop()`.
2. Komendy przez te same funkcje co handlery HTTP; `/api/system-toggle` dostaje `enabled=0|1` (idempotentnie, bez
   parametru przełącza jak dziś), wejście w Serwis wycisza buzzer jak dziś.
3. Bez zmian w web GUI.

Odłożone (jak przy dolewce): lampy liczą ramki termostatu (inny `magic`) jako błędne — `rx_bad` w web GUI lamp
rośnie o ~1 co 5 s. Poprawka w lampach (obcy `magic` ignorowany) — razem z kolejną zmianą w lampach.

## Etapy

| # | Etap | Repo | Kryterium |
|---|---|---|---|
| T0 | Plan, odpowiedzi na pytania, aktualizacja `docs/nodes/termostat.md` | panel | plan zaakceptowany |
| T1 | `TH_STATUS` w termostacie (tylko nadawanie) | termostat | ramki w logu panelu, `bad HMAC 0`, wartości = web GUI — ✅ 2026-11-15 (termostat FW 1.0.0 — pierwsza wersja w statusie, panel: listener + Serial `h`) |
| T2 | Ekrany w symulatorze (kafelek, L2, alarmy) | panel | układ zaakceptowany na PC — ✅ 2026-11-15 (temperatura 96 px w kolorze stanu, kontrolki grzałki / wentylatora, rząd sterowania ręcznego tylko w Serwisie) |
| T3 | Adapter `src/thermo/` + dane na żywo, `THERMO_OFFLINE` | panel | stan zgodny z web GUI, alarm po odłączeniu; pamięć (Serial `i`) po dodaniu czcionki 96 px |
| T4 | `TH_CMD` / `TH_ACK` w termostacie + klawisze testowe Serial panelu | termostat + panel | komendy, powtórka bez ponownego wykonania, `STALE_NONCE` |
| T5 | Sterowanie z ekranu | panel | macierz testów niżej |

## Testy (T5)

| Scenariusz | Oczekiwane | Wynik |
|---|---|---|
| Auto → Serwis → Auto | zmiana < 1 s, web GUI zgodne, odliczanie widoczne, buzzer cichnie po wejściu w Serwis | |
| Grzałka / wentylator ręcznie w Serwisie | stan zgodny z web GUI, ▼▲ co 5 % | |
| Sterowanie ręczne w Auto | przyciski wyszarzone z podpowiedzią | |
| Wycisz alarm (`TEMP_LOW` / `TEMP_HIGH`) | buzzer cichnie, alarm zostaje na pasku; nowe wystąpienie znów dzwoni; przy innych alarmach przycisk nieaktywny | |
| Awaria czujnika → kasowanie | P1 na pasku, dialog, latch skasowany (albo wraca, gdy czujnik dalej nie działa) | |
| Termostat offline | `THERMO_OFFLINE` ≤ 15 s, sterowanie zablokowane | |

## Pytania

1. ~~Wyciszanie / zasada alarmów~~ (2026-11-15): alarm wymagający interwencji zatrzaśnięty, bez wyciszania
   (`SENSOR_FAULT`, `HW_FAULT`); wyciszalne tylko samoczynnie mijające `TEMP_LOW` / `TEMP_HIGH`. Trend grzałki to
   ostrzeżenie bez buzzera (problemy z konfiguracją / mocą grzałki; zbiornik chronią `TEMP_LOW` / `TEMP_HIGH`).
   Zmiany w firmware termostatu zrobione przed T1.
2. ~~Temperatura zadana~~ (2026-11-15): nie pokazujemy — tylko odczyt temperatury; kontrolki grzałki i wentylatora
   zawsze widoczne.
3. ~~Sterowanie ręczne~~ (2026-11-15): bez dialogu (tylko w Serwisie).
4. ~~`SERVICE_MODE`~~ (2026-11-15): tylko kolor kafelka, nie alarm.
