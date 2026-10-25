# SWITCH TIMER — lista punktów

Projekt: `switch_timer-iot` (commit `2496bda` + niezacommitowane zmiany w `conf.h`, `platformio.ini`),
XIAO ESP32-C3, 192.168.10.10.
Wersja 1: 2026-10-25 — propozycja z ekstrakcji kodu, do przeglądu.

Wyłącznik czasowy: przycisk fizyczny (krótko = START / dodaj czas, długo = STOP) i web GUI sterują jednym
wyjściem. Stany: IDLE, ACTIVE (odliczanie `CYCLE_TIME_S` = 300 s), PERMANENT (wymuszone włączenie).
Bez RTC i FRAM; restart co 24 h.

## Sprzęt

| Element | Rola | Źródło |
|---|---|---|
| Wejście przycisku (GPIO7, active LOW) | START < 500 ms, STOP > 1200 ms | `src/conf.h` (wersja robocza) |
| Wyjście (GPIO21, active HIGH) | aktuator — przekaźnik / urządzenie ? | `src/conf.h` |

## Aktuatory — FMEA

| Aktuator | Błąd / skutek | Ochrona na węźle | Safety komend |
|---|---|---|---|
| Wyjście | zostaje włączone na stałe (PERMANENT) | brak limitu czasu PERMANENT | zależy od podłączonego urządzenia ? |
| Wyjście | wielokrotne „dodaj czas” (retry) | brak górnego limitu odliczania | `cmd_id` wymagany |

## Parametry

| ID | key | Etykieta | Kat. | Typ | Dostęp | Jedn. | Zakres / krok | Panel | Safety | Odśw. | Faza | Źródło / uwagi |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0–8 | wspólne | | | | | | | | | | MVP | bez RTC: `time_quality`, `rtc_battery_low` nie dotyczą; brak `system_enabled` |
| 16 | `output_state` | Stan | ST | ENUM `Wył.\|Odliczanie\|Stale wł.` | RO | | | tak | | zm | MVP | `OutputState` |
| 17 | `output_on` | Wyjście | ST | BOOL | RO | | | tak | | zm | MVP | `isRelayOn()` |
| 18 | `remaining` | Pozostało | RO | INT | RO | s | | tak | | zm | MVP | `getRemainingMs() / 1000` |
| 19 | `cycle_time` | Czas cyklu | CF | INT | RO | s | | web | | schemat | później | stała `CYCLE_TIME_S` — dziś niekonfigurowalna |
| 20 | `add_time` | Dodaj czas | AC | ACTION | X | | | tak | 0 | | MVP | `/api/relay/add-time` (jak krótki przycisk) |
| 21 | `force_on` | Włącz na stałe | AC | ACTION | X | | | tak | 1 | | MVP | `/api/relay/force-on` |
| 22 | `output_off` | Wyłącz | AC | ACTION | X | | | tak | 0 | | MVP | `/api/relay/off` |

## Alarmy

Brak alarmów w firmware. Kandydat: `PERMANENT_LONG` (P3) — wyjście na stałe dłużej niż N h, jeśli
podłączone urządzenie tego wymaga.

## Do ustalenia

- [ ] Co jest podłączone do wyjścia? Od tego zależy safety `force_on` i potrzeba limitu PERMANENT.
- [ ] Niezacommitowane zmiany w repo (`conf.h`: debounce 80 ms, progi 500 / 1200 ms; `platformio.ini`) —
      uporządkować w tamtym repo. `CLAUDE.md` projektu opisuje stare progi (1500 / 3000 ms).
- [ ] `cycle_time` konfigurowalny (NVS) czy zostaje stałą kompilacji?
