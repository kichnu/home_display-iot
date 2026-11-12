# LAMPY — lista punktów

Projekt: `led_lamp_aquarium-iot` (commit `566fe7c`), PopBloom RL90 z XIAO ESP32-C3.
Instancje: `lamp_1` (192.168.10.5), `lamp_2` (192.168.10.6) — ten sam schemat.
Wersja 1: 2026-10-25 — propozycja z ekstrakcji kodu, do przeglądu.

Lampa autonomiczna: 4 kanały PWM (A–D), dobowa krzywa (program) liczona lokalnie, wentylator sterowany
szacowaną mocą. Biblioteka do 24 programów w FRAM. Lampy synchronizują programy i tryb przez ESP-NOW
(`network/espnow_sync.*`, HMAC) — panel musi tę synchronizację zachować (patrz dokument ustaleń).

## Sprzęt

| Element | Rola | Źródło |
|---|---|---|
| 4 × PWM LED (GPIO 3–6), drivery Hi7001 | aktuatory, 500 Hz / 14 bit | `hardware/pwm_output.*` |
| Wentylator PWM (GPIO7) | chłodzenie, % = szacowana moc, start ≥ 20 %, stop < 18 % | `light_engine.*` |
| DS3231, FRAM 32 KB | czas (krzywa w czasie lokalnym), programy, konfiguracja | |
| DS18B20 (GPIO2) | rezerwa, niepodłączony | `USTALENIA.md` |

## Aktuatory — FMEA

| Aktuator | Błąd / skutek | Ochrona na węźle | Safety komend |
|---|---|---|---|
| Kanały LED | za dużo światła (tryb testowy zostawiony na 100 %) | brak timeoutu trybu testowego | tryb: 0, wartości: 0 |
| Kanały LED | zły czas → przesunięta krzywa | RTC + NTP, `time_valid` | — |
| Kanały LED | błysk 100 % przy restarcie | `gpio_hold` przy restarcie planowym; po WDT akceptowany | — |
| Wentylator | przegrzanie drivera | wentylator od mocy, nie od temperatury (brak czujnika) | — |

## Parametry węzła

| ID | key | Etykieta | Kat. | Typ | Dostęp | Jedn. | Zakres / krok | Panel | Safety | Odśw. | Faza | Źródło / uwagi |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0–8 | wspólne | | | | | | | | | | MVP | `system_enabled`, `alarm_mute` — nie dotyczy (brak w firmware) |
| 16 | `mode` | Tryb | MD | ENUM `Program\|Test\|Noc` | RW | | | tak | 0 | zm | MVP | `/api/manual-enter` / `manual-exit`; synchronizowany ESP-NOW |
| 17 | `power` | Moc | RO | FIX1 | RO | % | | tak | | zm | MVP | `power_pct` |
| 18 | `ramp_active` | Rampa | ST | BOOL | RO | | | web | | zm | później | |
| 19 | `fan_pct` | Wentylator | RO | INT | RO | % | | web | | zm | później | |
| 20 | `active_program` | Program | ST | STRING | RO | | | tak | | zm | MVP | `active_name` |
| 21 | `program_select` | Wybór programu | MD | ENUM (dynamiczny) | RW | | | tak | 1 | zm | MVP | lista z `/api/programs`; zmiana listy = nowy `schema_hash` |
| 22 | `mode_all` | Tryb na wszystkich | AC | ACTION | X | | | tak | 1 | | później | `/api/mode-all` — powiela tryb na drugą lampę |
| 23 | `time_valid` | Czas OK | ST | BOOL | RO | | | diag | | zm | MVP | `time_valid`; fałsz = alarm |
| 24 | `fram_ok` | FRAM | DG | BOOL | RO | | | diag | | zm | później | |
| 25 | `program_orphan` | Program osierocony | DG | BOOL | RO | | | diag | | zm | później | `active_orphan` |

## Parametry kanału (blok, `id = 64 + ch × 16 + offset`, ch = 0–3 → A–D)

| Off. | key | Etykieta | Kat. | Typ | Dostęp | Jedn. | Zakres / krok | Panel | Safety | Odśw. | Faza | Źródło / uwagi |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `ch_label` | Kanał | CF | STRING | RO | | ≤ 11 zn. | tak | | schemat | MVP | `ChannelConfig.label` |
| 1 | `ch_out` | Jasność | RO | FIX2 | RO | % | 0–100 | tak | | zm | MVP | `v_out` (setne %) |
| 2 | `ch_target` | Cel programu | RO | FIX2 | RO | % | | web | | zm | później | `v_target` |
| 3 | `ch_manual` | Wartość ręczna | SP | FIX2 | RW | % | 0–100 / 1 | tak | 0 | zm | MVP | `/api/manual-set`, tylko w trybie Test / Noc |
| 4 | `ch_duty` | Duty | DG | INT | RO | | 0–16384 | diag | | zm | później | |

Tylko web GUI: edytor krzywych, biblioteka programów (zapis, usuwanie, nazwy, aktywacja na wszystkich),
konfiguracja kanałów (gamma, `min_duty`, `power_frac`), progi wentylatora, presety trybu nocnego,
liczniki resetów (WDT / panic / brownout).

## Alarmy

| Kod | Warunek | Prio | Skutek / węzeł | Reakcja | Kasowanie | Źródło |
|---|---|---|---|---|---|---|
| `TIME_INVALID` | brak ważnego czasu (RTC i NTP) | P2 | krzywa niepewna | sprawdzić RTC / Wi-Fi | auto | `time_valid` |
| `FRAM_FAIL` | FRAM niezainicjalizowany | P2 | brak programów / konfiguracji | serwis | auto | `fram_ok` |
| `MANUAL_LEFT_ON` | tryb Test / Noc dłużej niż N h ? | P3 | światło poza programem | wrócić do programu | auto | do dodania (panel lub węzeł) |
| `RTC_BATTERY` | wspólny | P3 | | wymienić baterię | auto | |

## Do ustalenia

- [x] ~~Wybór programu na panelu czy tylko web?~~ Na panelu, MVP (2026-11-12); lista z `FT_LIST` + nazwy
      z nagłówka programu (chunk 0), aktywacja na wszystkich lampach (`docs/PLAN_WDROZENIA.md`).
- [x] ~~Osobny kafelek na lampę czy jeden „Lampy”?~~ Jeden kafelek i ekran grupy; sterowanie grupowe,
      lampy jako wiersze ze stanem (2026-11-12, „Nawigacja i układ ekranów”).
- [ ] Timeout trybu testowego (na węźle) albo alarm `MANUAL_LEFT_ON` — który wariant i po ilu godzinach?
- [ ] Migracja z HMAC na wspólną bibliotekę (już na liście otwartych kwestii panelu).
