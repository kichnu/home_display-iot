# DOZOWNIK — lista punktów

Projekt: `dosing_system-iot` (commit `3584a35`, firmware 4.0.0), XIAO ESP32-S3, 192.168.10.3.
Wersja 1: 2026-10-25 — propozycja z ekstrakcji kodu, do przeglądu.

Dozownik 8 kanałów (pompy perystaltyczne, CH7 = pompa powietrza do fitoplanktonu). Harmonogram: parzyste
godziny 2–22, dni tygodnia, przesunięcie kanałów o 15 min. Zmiany konfiguracji czekają (pending) do
północy. Jedna pompa naraz, walidacja GPIO PRE/RUN/POST, przekaźnik master odcinający zasilanie pomp.

## Sprzęt

| Element | Rola | Źródło |
|---|---|---|
| 8 przekaźników pomp (GPIO 16, 17, 18, 12, 13, 14, 15, 11) | aktuatory | `config/config.h` `PUMPS_PINS` |
| Wejścia walidacji GPIO | potwierdzenie pracy pompy, wykrycie zaklejenia | `hardware/relay_controller.*` |
| Przekaźnik master (GPIO38) | odcięcie wszystkich pomp przy błędzie krytycznym | `hardware/safety_manager.*` |
| Buzzer (GPIO39), przycisk (GPIO40) | alarm; 5 s przytrzymania = reset błędu krytycznego | `safety_manager.*` |
| DS3231, FRAM MB85RC256V | czas, konfiguracja, stany dzienne, błędy krytyczne | |
| UART2 (pump monitor) | placeholder (Edge Impulse), nieaktywny | `/api/pump-monitor-status` |

## Aktuatory — FMEA

| Aktuator | Błąd / skutek | Ochrona na węźle | Safety komend |
|---|---|---|---|
| Pompa kanału | przedawkowanie (duplikat komendy, zaklejony przekaźnik) | jedna pompa naraz, timeout 180 s (CH7 600 s), walidacja POST, master relay, limity dawek | dawka ręczna: 2 |
| Pompa kanału | brak dawki (przewód, pompa) | walidacja PRE/RUN → `events_failed` | — |
| Pompa kanału | praca ciągła „hold” z web GUI | backstop 60 s | tylko web |
| Konfiguracja | zła dawka dobowa | limity: pojedyncza 0,1–50 ml, dobowa ≤ 500 ml, tygodniowa ≤ 3500 ml; per kanał `ChannelParams`; zmiana dopiero o północy | tylko web |

## Parametry węzła

| ID | key | Etykieta | Kat. | Typ | Dostęp | Jedn. | Zakres / krok | Panel | Safety | Odśw. | Faza | Źródło / uwagi |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0–8 | wspólne | | | | | | | | | | MVP | `system_enabled` = scheduler (`/api/scheduler`); `alarm_mute` — brak w firmware |
| 16 | `halted` | Zatrzymany | ST | BOOL | RO | | | tak | | zm | MVP | `systemHalted` (błąd krytyczny aktywny) |
| 17 | `critical_error` | Błąd krytyczny | ST | ENUM `—\|Timeout pompy\|FRAM\|RTC\|Nieznany` | RO | | | tak | | zm | MVP | `CriticalErrorType` |
| 18 | `critical_channel` | Kanał błędu | ST | INT | RO | | 0–7 | tak | | zm | MVP | `CriticalErrorState.channel` |
| 19 | `active_channel` | Dozuje kanał | ST | INT | RO | | −1–7 | tak | | zm | MVP | `activeChannel` |
| 20 | `active_remaining` | Pozostało | RO | INT | RO | s | | tak | | zm | MVP | `activeRemainingMs / 1000` |
| 21 | `pump_stop` | Stop pompy | AC | ACTION | X | | | tak | 0 | | MVP | `/api/pump-stop` |
| 22 | `pending_changes` | Zmiany od północy | ST | BOOL | RO | | | web | | zm | później | `pending_changes_mask ≠ 0` |
| 23 | `apply_pending` | Zastosuj teraz | AC | ACTION | X | | | web | 1 | | później | `/api/apply-pending` |
| 24 | `daily_reset` | Reset doby | AC | ACTION | X | | | web | 2 | | później | `/api/daily-reset` — kasuje wykonane eventy, czyli pozwala dozować ponownie |

## Parametry kanału (blok, `id = 64 + ch × 16 + offset`, ch = 0–7)

| Off. | key | Etykieta | Kat. | Typ | Dostęp | Jedn. | Zakres / krok | Panel | Safety | Odśw. | Faza | Źródło / uwagi |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `ch_name` | Nazwa | CF | STRING | RO | | ≤ 19 zn. | tak | | schemat | MVP | `ChannelLabel.name` (np. „Calcium”) |
| 1 | `ch_enabled` | Włączony | CF | BOOL | RO | | | tak | | zm | MVP | zmiana tylko w web (pending) |
| 2 | `ch_state` | Stan | ST | ENUM `Nieaktywny\|Niekompletny\|Błędny\|OK\|Oczekuje` | RO | | | tak | | zm | MVP | `ChannelState` |
| 3 | `ch_today` | Dziś | RO | FIX1 | RO | ml | | tak | | zm | MVP | `today_added_ml` |
| 4 | `ch_daily_dose` | Dawka dobowa | CF | FIX1 | RO | ml | | tak | | zm | MVP | edycja tylko web (`/api/dosing-config`) |
| 5 | `ch_single_dose` | Dawka jednorazowa | RO | FIX2 | RO | ml | | tak | | zm | MVP | `calc.single_dose_ml` |
| 6 | `ch_events_done` | Wykonane | RO | INT | RO | | 0–11 | tak | | zm | MVP | `completedToday` / `activeEvents` |
| 7 | `ch_failed_today` | Nieudane | ST | INT | RO | | | tak | | zm | MVP | `failedToday` |
| 8 | `ch_container_pct` | Pojemnik | RO | INT | RO | % | | tak | | zm | MVP | `remainingPct` |
| 9 | `ch_days_left` | Na ile dni | RO | INT | RO | dni | | tak | | zm | MVP | `getDaysRemaining()` |
| 10 | `ch_container_ml` | Zostało | RO | FIX1 | RO | ml | | web | | zm | później | `remainingMl` |
| 11 | `ch_manual_dose` | Dawka ręczna | AC | ACTION | X | | | tak | 2 | | MVP | `/api/manual-dose`; wymaga włączonego schedulera, dawka = `single_dose` |
| 12 | `ch_refill` | Pojemnik napełniony | AC | ACTION | X | | | tak | 1 | | MVP | `/api/refill`; błędne użycie = fałszywy zapas |
| 13 | `ch_pending` | Oczekuje zmian | ST | BOOL | RO | | | web | | zm | później | `hasPending` |
| 14 | `ch_total_dosed` | Suma od resetu | DG | FIX1 | RO | ml | | web | | zm | później | `DosedTracker` |
| 15 | — | rezerwa | | | | | | | | | | |

Tylko web GUI: harmonogram (godziny, dni), dawka dobowa, kalibracja (`/api/calibrate`, 30 s) i wydajność
`dosing_rate`, praca ciągła pompy (`/api/pump-start` / `stop`), pojemność pojemnika, limity per kanał,
etykiety, notatki, log parametrów i zdarzeń, PIN blokady kanału.

## Alarmy

| Kod | Warunek | Prio | Skutek / węzeł | Reakcja | Kasowanie | Źródło |
|---|---|---|---|---|---|---|
| `CRIT_PUMP_TIMEOUT` | pompa pracuje dłużej niż limit | P1 | master relay off, `halted`, buzzer | sprawdzić przekaźnik / pompę kanału | latch, przycisk 5 s na węźle | `ERROR_PUMP_TIMEOUT` |
| `CRIT_FRAM` | błąd FRAM | P1 | jw. | serwis | latch, przycisk | `ERROR_FRAM_FAILURE` |
| `CRIT_RTC` | błąd RTC | P1 | jw. | serwis | latch, przycisk | `ERROR_RTC_FAILURE` |
| `DOSE_FAILED` (per kanał) | walidacja GPIO PRE/RUN nie potwierdziła dawki | P2 | event `FAILED`, kanał dalej w harmonogramie | sprawdzić przewód / pompę | auto o północy | `events_failed` |
| `CONTAINER_LOW` (per kanał) | zapas < 10 % | P3 | | uzupełnić, potem `ch_refill` | auto po refill | `isLowVolume()` |
| `RTC_BATTERY` | wspólny | P3 | | wymienić baterię | auto | |

Stan kanału `Błędny` / `Niekompletny` to status konfiguracji, nie alarm.

## Do ustalenia

- [ ] Reset błędu krytycznego tylko przyciskiem na węźle (obecnie) czy także z panelu (safety 2)?
      Fizyczny reset wymusza obejrzenie urządzenia — argument, żeby zostawić.
- [ ] Czy dawka ręczna liczy się do `today_added_ml` i limitu dobowego?
- [ ] PIN blokady kanału z web GUI (`/api/verify-pin`) — czy panel ma go respektować (np. dla dawki ręcznej)?
- [ ] CH7 (pompa powietrza, tryb „duration”): osobny wariant bloku bez dawek ml (czas pracy w s zamiast
      `ch_daily_dose` / `ch_single_dose`)?
- [ ] Brak wyciszenia buzzera z web GUI — dodać `alarm_mute` (wspólny profil)?
- [ ] Panel pokazuje 8 kafelków kanałów czy tylko włączone (`ch_enabled`)?
