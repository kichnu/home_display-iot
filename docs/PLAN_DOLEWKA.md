# Plan wdrożenia panelu — etap 2: dolewka

Stan: 2026-11-14, plan zaakceptowany (decyzje w sekcji „Pytania”).

Kontekst: `docs/nodes/dolewka.md` (lista punktów v1), `docs/nodes/dolewka_raport_2026-10-31.md` (raport zwrotny,
firmware `df4a2dc`), `docs/PLAN_WDROZENIA.md` (etap lamp, wzorzec adaptera), `~/dev-knowledge/esp32/espnow-sync.md`.
Projekt węzła: `top_off_water_new-iot` (XIAO ESP32-C3, 192.168.10.2).

## Zakres (MVP dolewki)

| Funkcja | Na panelu | Źródło w węźle |
|---|---|---|
| Status systemu | kolor kafelka L1 + nagłówek L2: OK / Uwaga / Alarm / Awaria / Serwis | `node_status` (nowe, wyliczane w węźle) |
| Dolano w 24 h | `1240 / 3000 ml` (licznik vs limit dobowy) | `getRolling24hVolume()`, `daily_limit_ml` |
| Poziom w zbiorniku RO | pasek z progami rezerwy i alarmu + „zapas: N dolewek, ~X h” | `res_zone`, `res_pct`, `res_portions`, `res_hours_left`, `res_estimated` |
| Auto ⇄ Serwis | przełącznik 2-pozycyjny bez dialogu (pending → stan z ACK), w Serwisie odliczanie do powrotu do Auto | `system_enabled` (idempotentnie, jak `enabled=0\|1`) |
| Porcje ręczne | bargraf przesuwany palcem 1–10 porcji (z ml) + przycisk ON, postęp, STOP | `manualPortionAdd()` (rozszerzone o N), `manualPortionsStop()` |
| Alarmy | pasek alarmów + lista na L2 z przyczyną | bity alarmów w statusie |
| Kasowanie alarmu | „Kasuj alarm” + dialog; bez osobnego wyciszania (alarm ma wymusić działanie) | `resetSystem()` (= przycisk 3 s na węźle) |
| Limit dobowy | podgląd przy licznikach 24 h + zmiana −/+ (krok 50 ml) z dialogiem | `daily_limit_ml`, walidacja 100–10000 ml i ≥ 2 × dawka w węźle |

Poza panelem (zostaje w web GUI): „Uzupełniono zbiornik” (`/api/reserve/refill`), kalibracja pompy, progi EMA, geometria zbiornika, `reserve_pct` / `alarm_pct`,
historia, diagnostyka ToF.

## Kluczowa decyzja: transport

Decyzja 2026-11-14: wariant A — ESP-NOW z dedykowanym protokołem dolewki w stylu lamp (HMAC, bez AEAD i `ParamDesc` po radiu),
w panelu osobny adapter `src/topoff/` obok `src/lamps/`, ekran L2 pisany ręcznie (jak lampy).
Ta sama konfiguracja obowiązuje dla kolejnych węzłów (dozownik, termostat, switch timer): własny protokół
węzła na ESP-NOW, HMAC z kluczem z hasła Wi-Fi i kontekstem per węzeł, nonce dla komend, adapter i ekran w panelu.

| Wariant | Za | Przeciw |
|---|---|---|
| A. ESP-NOW, protokół dolewki jak lampy (rekomendowany) | sprawdzony wzorzec z etapu 1, szybko, router poza ścieżką danych (zgodnie z ustaleniami) | kolejny protokół do migracji na wspólną bibliotekę |
| B. Wspólna biblioteka (AEAD, parowanie, schemat `ParamDesc`) | docelowa architektura | duży nakład, odsuwa działający panel o tygodnie |
| C. HTTP przez router (`/api/status`, POST) | zero zmian radiowych w węźle | sprzeczne z decyzją „router poza ścieżką”, sesje/logowanie dolewki, zależność od routera |

Klucz: wyliczony z hasła Wi-Fi jak w lampach, ale z innym kontekstem (`TOPOFF-ESPNOW-v1:`), żeby ramki lamp
i dolewki nie były wzajemnie ważne.

Różnica względem lamp — ochrona komend przed powtórką: lampy akceptują nowy `boot_id` nadawcy, więc ramkę
z poprzedniego uruchomienia panelu da się odtworzyć. Dla pompy to za mało. Dolewka publikuje w statusie
`cmd_nonce` (losowy przy starcie, zmieniany po każdej wykonanej komendzie); komenda musi go powtórzyć.
Nagrana komenda jest bezużyteczna, bo nonce już się zmienił.

## Ramki (szkic)

Nagłówek i HMAC-SHA256/16 jak w lampach (`magic`, `version`, `type`, `boot_id`, `seq`).

| Ramka | Kierunek | Treść | Uwagi |
|---|---|---|---|
| `TO_STATUS` | dolewka → broadcast, co 5 s + od razu po zmianie (debounce 300 ms) | nazwa, fw, `node_status`, tryb + minuty Serwisu do Auto, `algo_state`, `error_code`, pompa, porcje pozostałe/wykonane, 24 h ml, limit ml, dawka ml, strefa i % zbiornika, porcje i godziny zapasu, flaga szacunku, bity alarmów, przyczyna alarmu zbiornika, `cmd_nonce`, UTC + `time_q` | ~80 B; brak 3 statusów (15 s) = `TOPOFF_OFFLINE` na panelu |
| `TO_CMD` | panel → dolewka (unicast) | `cmd_id`, `cmd_nonce`, komenda, argument | panel ponawia co 250 ms, do 8 razy, z tym samym `cmd_id` |
| `TO_ACK` | dolewka → panel | `cmd_id`, wynik, wartość faktyczna | powtórzony `cmd_id` = ten sam ACK bez ponownego wykonania |

Komendy: `SET_MODE` (0 Serwis / 1 Auto), `PORTIONS` (N), `PUMP_STOP`, `ERROR_RESET`, `SET_DAILY_LIMIT` (ml).
Bez `ALARM_MUTE` i `RESERVE_REFILLED` (decyzja 2026-11-14).

Wyniki ACK: `OK`, `NOT_SERVICE` (porcje poza Serwisem), `CALIBRATION`, `PUMP_BUSY`, `QUEUE_FULL` (z liczbą
przyjętych), `RESET_BLOCKED` (faza zapisu), `RESERVOIR_EMPTY`, `STALE_NONCE`, `OUT_OF_RANGE` (limit dobowy poza zakresem, z wartością dopuszczalną).

## Porcje ręczne — propozycja

- Węzeł: `manualPortionsAdd(n)` dodaje n porcji naraz do kolejki (limit kolejki 20 bez zmian); ACK zwraca liczbę
  przyjętych. Reguły bez zmian: tylko Serwis, bez wliczania do 24 h/EMA, odejmowane z rezerwy, przejście do Auto
  przerywa sesję, odmowa przy pustym zbiorniku.
- Panel (2026-11-14): bargraf 1–10 porcji przesuwany palcem, obok liczba porcji i ml (`n × dawka`); przycisk ON
  wysyła `PORTIONS(n)` od razu, bez okna potwierdzenia (wybór na bargrafie jest świadomy). Przy n > zapas porcji ostrzeżenie (kolor bargrafu ponad zapasem).
- W trakcie sesji: „porcja 2 z 3” na przycisku, duży STOP (safety 0, bez dialogu) w miejscu ON; bargraf pokazuje
  porcje wykonane / bieżącą / pozostałe. Dokładanie porcji do trwającej sesji — nie w MVP (najpierw STOP).
- Pusty zbiornik (`LOW_RESERVOIR`): bargraf i ON zablokowane z podpowiedzią (węzeł i tak odmawia).
- W Auto przycisk wyszarzony z podpowiedzią „tylko w trybie Serwis”.

## Poziom wody w zbiorniku — propozycja widoku

- Pionowy pasek z dwiema kreskami: próg rezerwy (`reserve_pct`) i alarmu (`alarm_pct`); kolor wg strefy
  (OK zielony, Rezerwa żółty, Alarm czerwony, Nieznany szary).
- Pod paskiem: „Zapas: 12 dolewek · ~38 h” (porcje ponad poziomem alarmowym, godziny z tempa parowania).
- Gdy wartość jest szacunkiem (brak odczytu ToF): dopisek „szacunek” i szary kontur paska.
- Na kafelku L1 tylko strefa i porcje/godziny.

## Alarmy na panelu

Z raportu 2026-10-31 (§5), priorytety bez zmian:

| Kod | Prio | Kasowanie z panelu |
|---|---|---|
| `LOW_RESERVOIR` | P1 | reset, gdy zmierzony poziom ≥ alarm + 1 porcja; bez odczytu ToF — „Uzupełniono zbiornik” tylko w web GUI |
| `DAILY_LIMIT` | P1 | reset |
| `RATE_ANOMALY` | P1 | reset (rozbrojenie do 24 h) |
| `NO_TOPOFF` | P1 | reset albo następna dolewka |
| `HW_FAULT` | P1 | reset, gdy blok znów działa |
| `RESERVE_LOW` | P3 | auto |
| `LEVEL_NO_READING` | P3 | auto |
| `LEVEL_SENSOR_FAULT` | P3 | auto |
| `TOPOFF_OFFLINE` (panel) | P2 | auto po powrocie statusu |

Wyciszania buzzera z panelu nie ma (2026-11-14): jedyna droga to usunięcie przyczyny i kasowanie.

Stany przycisków sterowania (Auto/Serwis, ON, STOP, Kasuj): po dotknięciu od razu zmiana podświetlenia
(„wysłano”, pending), pełna zmiana stanu przycisku dopiero ze statusu/ACK dolewki; brak ACK → powrót do stanu
poprzedniego + komunikat.

Po resecie ACK mówi, co zostało (np. alarm zbiornika trwa do uzupełnienia) — panel to pokazuje zamiast
„skasowano”.

## Zmiany w firmware dolewki (`top_off_water_new-iot`)

1. Moduł `src/network/espnow_panel.*`: init po Wi-Fi, `WiFi.setSleep(false)` (dziś brak), klucz z hasła Wi-Fi,
   callback odbioru tylko kopiuje ramkę do kolejki — obsługa w `loop()` (bez I2C/FRAM w callbacku, W4).
2. Status: nowe pola `node_status` i `error_code` (raport §4: dziś tylko `system_error` + tekst).
3. Komendy (w tym limit dobowy z walidacją jak `/api/set-fill-water-max`) przez te same funkcje co handlery HTTP (jedna brama do stanu), `cmd_nonce`, pamięć ostatniego
   `cmd_id` i wyniku.
4. `manualPortionsAdd(n)`.
5. Po teście panelu: usunięcie porcji ręcznych z web GUI (decyzja z raportu, odp. 6.1).

Przed startem w repo dolewki: nieśledzony `docs/ZGODNOSC_REGUL.md` do commita lub decyzji; testy HW z §8 planu
rezerwy w toku — nie blokują, ale ESP-NOW dochodzi do tego samego firmware.

Odłożone (2026-11-14): lampy liczą ramki dolewki (inny `magic`) jako błędne — `rx_bad` w web GUI lamp rośnie
o ~1 co 5 s. Poprawka w lampach (obcy `magic` ignorowany albo liczony osobno) + OTA obu lamp — później,
razem z kolejnym węzłem albo zmianą w lampach.

## Etapy

Każdy etap kończy się buildem, testem na sprzęcie i commitem.

| # | Etap | Repo | Kryterium |
|---|---|---|---|
| D0 | Plan, odpowiedzi na pytania, aktualizacja `docs/nodes/dolewka.md` | panel | ten plik zaakceptowany — ✅ 2026-11-14 (lista punktów v2) |
| D1 | `TO_STATUS` w dolewce (tylko nadawanie), `setSleep(false)`, `node_status`, `error_code` | dolewka | OTA, ramki widoczne w logu panelu, `rx_bad = 0`, dolewka działa jak wcześniej — ✅ 2026-11-14 (dolewka FW 1.1.0, panel 0.6.1: status co 5 s, bad HMAC 0, wartości zgodne z web GUI, zmiana Auto/Serwis od razu) |
| D2 | Ekrany w symulatorze: kafelek L1, ekran L2 (odczyty, sterowanie wyszarzone), alarmy | panel | układ zaakceptowany na PC — ✅ 2026-11-14 (pasek zbiornika w strefach kolorów, bargraf porcji + ON/STOP, okno limitu, kasowanie alarmu) |
| D3 | Adapter `src/topoff/` + model + UI odczytu na panelu, `TOPOFF_OFFLINE` | panel | stan zgodny z web GUI dolewki, alarm po odłączeniu — ✅ 2026-11-15 (panel 0.7.0: kafelek i ekran L2 z danymi na żywo, sterowanie wyszarzone do D5; dane zgodne z web GUI, po wyłączeniu dolewki alarm P2 „brak łączności”) |
| D4 | `TO_CMD` / `TO_ACK` w dolewce, nonce, `manualPortionsAdd(n)`, `SET_DAILY_LIMIT` | dolewka | komendy z narzędzia testowego/panelu, powtórka odrzucona — ✅ 2026-11-15 (dolewka FW 1.2.0, panel 0.7.1, testy klawiszami Serial panelu: porcje, STOP, `NOT_SERVICE`, ten sam cmd_id bez ponownego wykonania, `STALE_NONCE`, limit + `OUT_OF_RANGE`, `ERROR_RESET`; test OTA dolewki po późnym Wi-Fi do zrobienia) |
| D5 | Sterowanie na panelu: Auto/Serwis, porcje (bargraf + ON) + STOP, kasowanie alarmu, limit dobowy | panel | macierz testów niżej |
| D6 | Usunięcie porcji ręcznych z web GUI dolewki | dolewka | GUI bez przycisku, panel jedyną drogą |

## Testy

| Scenariusz | Oczekiwane | Wynik |
|---|---|---|
| Auto → Serwis → Auto z panelu | tryb zmieniony w < 1 s, web GUI dolewki zgodne, odliczanie Serwisu widoczne | |
| Porcje: 3 z panelu w Serwisie | 3 dawki z pauzą 5 s, postęp na panelu, 24 h bez zmian, zapas maleje | ◐ 2026-11-15 (D4, Serial: 3 dawki, 24 h bez zmian, zapas maleje; postęp na ekranie w D5) |
| STOP w trakcie sesji | bieżąca porcja kończy się, kolejka pusta | ◐ 2026-11-15 (D4, Serial: porcja dokończona, kolejka pusta; z ekranu w D5) |
| Porcje w Auto | odmowa `NOT_SERVICE`, komunikat na panelu | ◐ 2026-11-15 (D4: `NOT_SERVICE`; komunikat na ekranie w D5) |
| Utracony ACK (ponowienie) | porcje dodane raz | ✅ 2026-11-15 (D4: ten sam cmd_id → ACK OK, kolejka 3, nie 6) |
| Powtórka nagranej komendy | `STALE_NONCE`, brak działania | ✅ 2026-11-15 (D4) |
| Alarm limitu dobowego → kasowanie z panelu | alarm na pasku, reset działa, buzzer cichnie | |
| Kasowanie przy pustym zbiorniku | ACK: alarm zbiornika trwa, panel to pokazuje | |
| Zmiana limitu dobowego z panelu | nowa wartość w web GUI, wartość poza zakresem odrzucona z komunikatem | ◐ 2026-11-15 (D4: 4050 przyjęte, 50 → `OUT_OF_RANGE` z min. 500; okno na ekranie w D5) |
| Dolewka offline | `TOPOFF_OFFLINE` po ≤ 15 s, sterowanie zablokowane | ✅ 2026-11-15 (D3: alarm P2; blokada sterowania do sprawdzenia w D5) |
| Restart dolewki w Serwisie | po starcie stan z FRAM / zasady węzła, panel bez fałszywych komend | |

## Pytania

1. ~~Transport~~ (2026-11-14): wariant A, także dla pozostałych węzłów IoT.
2. ~~Porcje~~ (2026-11-14): bargraf 1–10 przesuwany palcem + ON, bez okna potwierdzenia.
3. ~~Auto/Serwis~~ (2026-11-14): bez dialogu; podświetlenie po dotknięciu, pełny stan po ACK.
4. ~~Wyciszenie~~ (2026-11-14): nie — tylko kasowanie, alarm ma wymusić działanie.
5. ~~„Uzupełniono zbiornik”~~ (2026-11-14): tylko web GUI.
6. ~~Limit dobowy~~ (2026-11-14): podgląd i zmiana z panelu.
