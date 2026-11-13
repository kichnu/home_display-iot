# Plan wdrożenia panelu — etap 1: lampy

Wersja 1: 2026-11-12 — propozycja do akceptacji.

Kontekst: `Panel dotykowy ESP-NOW — ustalenia i stack.md` (architektura, nawigacja), `docs/nodes/lampy.md`
(lista punktów), `~/dev-knowledge/esp32/espnow-sync.md` (protokół lamp).

## Zakres funkcjonalny (MVP lamp)

| Funkcja | Działanie na panelu | Mechanizm w protokole lamp | Safety |
|---|---|---|---|
| Auto | tryb programu na wszystkich lampach | `FT_MODE` mode = Program, „ostatnie wygrywa” | 0 |
| Night light | tryb nocny na wszystkich; każda lampa używa własnego presetu | `FT_MODE` mode = Night | 0 |
| Test light | tryb testowy z wartością domyślną, bez suwaków na panelu | `FT_MODE` mode = Test, `v[]` = 50 % (= `TEST_START_V` lampy) | 0 |
| Zmiana programu | wybór z biblioteki, aktywacja na wszystkich lampach | `FT_ACTIVATE` (id programu) do każdej lampy, ACK / „pobieram” / odrzucony | 1 (dialog) |
| Błędy | pasek alarmów + wiersz lampy na ekranie grupy | heartbeat lamp + wykrywanie na panelu | — |

Ekran (wg „Nawigacja i układ ekranów”): na L1 jeden kafelek „Lampy” z trybem, programem i kolorem
najgorszego stanu w grupie. Na L2 ekran grupy: nagłówek, wiersz na każdą lampę (nazwa, online, tryb,
program, alarmy), a w strefie sterowania przyciski Auto / Noc / Test i przycisk „Program…”, który
otwiera listę programów.

Błędy lamp na panelu:

| Kod | Źródło | Prio |
|---|---|---|
| `LAMP_OFFLINE` | brak heartbeatu > 45 s (3 × 15 s) | P2 |
| `FRAM_FAIL` (RAM ONLY) | nowy bit w `flags` heartbeatu (zmiana w lampie) | P2 |
| `TIME_INVALID` | `time_q = 0` w heartbeacie | P2 |
| `RTC_FAIL` | nowy bit w `flags` heartbeatu (zmiana w lampie, jeśli lampa to wykrywa) | P3 |
| `GROUP_MISMATCH` | lampy online różnią się trybem, programem lub `catalog_hash` dłużej niż ~60 s | P3 |
| `PROGRAM_ORPHAN` | `flags` bit0 | P3 |
| `MANUAL_LEFT_ON` | Test/Noc dłużej niż N h, liczone na panelu od zmiany trybu w heartbeacie | P3, później |

## Kluczowa decyzja: panel mówi protokołem lamp

Lampy mają działający protokół bez mastera: HMAC z hasła Wi-Fi, heartbeat co 15 s z trybem, aktywnym
programem, `catalog_hash`, UTC i `mode_clock`, a do tego `FT_MODE` („ostatnie wygrywa”) i `FT_ACTIVATE`.
Panel staje się kolejnym uczestnikiem tego protokołu, ale nie lampą.

- Za: lampy prawie bez zmian, synchronizacja lampa–lampa nienaruszona, a trzecia lampa dołącza sama
  (limit `ESPNOW_MAX_PEERS = 6` mieści 3 lampy i panel). Działający panel powstaje szybko.
- Przeciw: HMAC bez szyfrowania (jak dziś między lampami) i protokół specyficzny dla lamp. Wspólna
  biblioteka i AEAD przychodzą później. Dlatego w panelu protokół lamp siedzi w osobnym adapterze,
  a UI działa na wspólnym modelu (patrz „Architektura panelu”).

Co panel robi w protokole:

| Ramka | Panel | Uwagi |
|---|---|---|
| `FT_HB` od lamp | odbiera | lista lamp, stan, błędy, `mode_clock`, UTC |
| `FT_LIST_REQ` → `FT_LIST` | wysyła do jednej lampy | lista id programów |
| `FT_PROG_REQ` (chunk 0) → `FT_PROG_DATA` | pobiera tylko chunk 0 | 64 B nagłówka zawiera nazwę; programy są niezmienne (unikalne id), więc cache nazw nie wymaga unieważniania |
| `FT_MODE` (+ `FT_MODE_ACK`) | wysyła unicastem do każdej lampy online, z ponawianiem | klucz sesji `(clock, MAC panelu)`, `clock = max(UTC, max mode_clock + 1)` |
| `FT_ACTIVATE` (+ `FT_ACK`) | wysyła do każdej lampy online | lampa bez programu pobiera go od innej lampy (ACK „pobieram”) |
| heartbeat panelu | nowy typ ramki `FT_CTRL_HB` | patrz zmiany w lampach |

## Zmiany w firmware lamp (`led_lamp_aquarium-iot`)

Małe i wstecznie zgodne. Starszy firmware ignoruje nieznany typ ramki i zerowe bity `flags`.

1. `FT_CTRL_HB` (nowy typ): heartbeat sterownika (nazwa, wersja). Lampa rejestruje nadawcę jako
   sterownik. Dziś `FT_ACTIVATE` wymaga `have_hb`, a zwykły `FT_HB` zrobiłby z panelu „lampę” (lista
   w GUI, `LIST_REQ` do panelu, przejmowanie czasu i licznika `programs_created`). Sterownik:
   - może wysyłać `FT_ACTIVATE` i `FT_MODE`,
   - nie trafia na listę lamp w GUI ani do `in_sync`, nie dostaje `LIST_REQ`, nie jest źródłem czasu,
   - przy pełnej tablicy peerów jest usuwany jako pierwszy, przed lampami.
2. `flags` w `HbBody`: bit1 = FRAM fail (RAM ONLY), bit2 = RTC fail (jeśli lampa to rozpoznaje).
3. Zaległy punkt z reguły bloków krytycznych (tabela w `critical-hardware-alarm.md`): lampa w awarii
   FRAM nie wysyła `FT_ACTIVATE` / `FT_MODE` i nie udostępnia programów (`FT_LIST`, `FT_PROG_DATA`)
   — incydent z programem fabrycznym, a do tego ponowny init FRAM. Robimy to razem, bo panel opiera się na tych samych ramkach.

## Architektura panelu

```
 ESP-NOW rx_cb → kolejka → [adapter: protokół lamp] → model (stan grupy, ParamDesc lokalny) → lv_subject → UI
                                   ↑ komendy (pending / ACK / rejected) ←───────────────── UI
```

- Adapter lamp tłumaczy ramki na model z ID z `docs/nodes/lampy.md` (16 `mode`, 20 `active_program`,
  21 `program_select`, …). Lokalny `ParamDesc` buduje panel, a nie lampa.
- UI renderuje stronę z modelu (strefy L2), a nie z ramek. Gdy węzły zaczną publikować schematy,
  wymienimy adapter, a UI zostanie bez zmian.
- `lv_*` wywołuje tylko task UI. Callback radia jedynie kolejkuje.

## Etapy

Każdy etap kończy się buildem, testem na sprzęcie i commitem.

| # | Etap | Repo | Wynik / kryterium |
|---|---|---|---|
| 0 | Dokumentacja: nawigacja, plan, decyzje (czas, pamięć) | panel | ten plik, sekcje w dokumencie ustaleń |
| 1 | Fundament: LVGL 9 + `esp_lcd` (2 fb, DIRECT, VSYNC), GT911 jako indev, CH422G, font PL, ekran demo | panel | płynny obraz bez migania, dotyk w LVGL, ą/ę/ł |
| 2 | Wi-Fi STA (sekrety przez pre-script z `~/.secrets/iot.env`), `setSleep(false)`, ArduinoOTA, NTP | panel | panel w sieci, czas z NTP, upload OTA |
| 3 | Pasywny nasłuch lamp: weryfikacja HMAC, replay, tablica lamp; log i ekran surowy | panel | obie lampy widoczne, `rx_bad = 0`, obraz stabilny przy pracy radia (otwarte ryzyko) |
| 4 | Zmiany w lampach: `FT_CTRL_HB`, bity `flags`, blokada ESP-NOW w awarii FRAM | lampy | OTA na obie lampy, sync lampa–lampa działa jak wcześniej |
| 4a | Symulacja GUI na komputerze (LVGL, ten sam kod ekranów) przed implementacją na panelu — ustalone 2026-11-13 | panel | układ L1/L2, pasek alarmów, lista programów zaakceptowane na PC |
| 5 | Model + UI odczytu: kafelek L1, ekran grupy L2, alarmy lamp, pasek alarmów, lista programów (cache nazw) | panel | stan zgodny z web GUI lamp, alarm `LAMP_OFFLINE` po odłączeniu lampy |
| 6 | Sterowanie: Auto / Noc / Test, zmiana programu (dialog), stany pending / rejected | panel | macierz testów niżej |
| 7 | Trzecia lampa: provisioning, OTA, dołączenie do grupy | lampy | automatyczne wykrycie na panelu i w lampach, program pobrany |

Później (poza etapem 1): wygaszacz, DO z buzzerem dla P1, `MANUAL_LEFT_ON`, kolejne węzły, wspólna
biblioteka i AEAD z migracją lamp.

Etap 1 można robić równolegle z przeglądem tego planu, bo nie zależy od decyzji o protokole.

## Testy synchronizacji (etap 6–7)

| Scenariusz | Oczekiwane |
|---|---|
| Tryb z panelu, wszystkie lampy online | wszystkie lampy przełączone w < 1 s, ACK od każdej, panel pokazuje stan z heartbeatów |
| Tryb z panelu i inny z web GUI lampy w tej samej sekundzie | wszystkie lampy zbiegają się do jednego trybu (wyższy klucz sesji), panel pokazuje wynik |
| Lampa offline podczas komendy, potem wraca | panel: pending → brak ACK → `LAMP_OFFLINE`; po powrocie `GROUP_MISMATCH`, wyrównanie ponowną komendą |
| Restart lampy w trybie Test/Noc | lampa nadrabia `mode_clock`, panel nie traci spójności |
| Restart panelu | panel odbudowuje stan z heartbeatów w ≤ 15 s, `mode_clock` z heartbeatów, komenda działa od razu |
| Zmiana programu, gdy jedna lampa go nie ma | ACK „pobieram”, lampa pobiera program od innej, panel pokazuje „pobieranie”, potem aktywny |
| Program skasowany w web GUI w trakcie wyboru na panelu | ACK „odrzucony”, komunikat na panelu, odświeżenie listy |
| Restart routera | przerwa w ramkach, po powrocie stan zgodny, bez fałszywych komend |
| Lampa w awarii FRAM | alarm `FRAM_FAIL` na panelu, lampa nie rozsyła programu ani trybu do innych |
| Trzecia lampa | wykryta bez konfiguracji, dostaje bibliotekę programów, reaguje na komendy grupy |

## Do decyzji

- [ ] Test light: stała 50 % na panelu (propozycja) czy domyślne wartości testu w konfiguracji lampy?
- [ ] Tryb na lampach offline: panel tylko ostrzega (propozycja), bez automatycznej wysyłki po powrocie.
      Automatyczne wyrównanie to zmiana zachowania grupy, więc później i świadomie.
- [ ] Czy panel ma mieć sterowanie pojedynczą lampą (L3)? Propozycja: nie w MVP, tylko podgląd.
