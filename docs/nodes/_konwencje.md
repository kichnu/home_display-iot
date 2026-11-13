# Lista punktów węzłów — konwencje

Wersja 1: 2026-10-25

Każdy węzeł ma jeden plik w `docs/nodes/`. Plik to lista punktów (parametrów, aktuatorów, alarmów),
z której powstanie tabela `ParamDesc` węzła (patrz „Model danych UI” w dokumencie ustaleń panelu).
Lista jest źródłem prawdy: zmiana parametru najpierw tu, potem w kodzie.

Węzły w zakresie (= podpięte do VPS, `home_iot-vps/device_config.py`):

| Plik | Projekt | Typ VPS | IP | MCU |
|---|---|---|---|---|
| `dolewka.md` | `top_off_water_new-iot` | `water_system` | 192.168.10.2 | XIAO ESP32-C3 |
| `dozownik.md` | `dosing_system-iot` | `doser_system` | 192.168.10.3 | XIAO ESP32-S3 |
| `termostat.md` | `thermo_control-iot` | `thermo_control` | 192.168.10.4 | ESP32-S3-Pico |
| `lampy.md` | `led_lamp_aquarium-iot` | `lamp_1`, `lamp_2` | 192.168.10.5–6 | XIAO ESP32-C3 |
| `switch_timer.md` | `switch_timer-iot` | `switch_system` | 192.168.10.10 | XIAO ESP32-C3 |

## Metodologia

1. Ekstrakcja z kodu (bottom-up): API web GUI (`/api/status`, endpointy POST), struktury konfiguracji,
   stałe kompilacji, maszyna stanów, alarmy. Kolumna „Źródło” wskazuje miejsce w kodzie.
2. Klasyfikacja (top-down), trzy filtry:
   - ISA-101 (HMI): na panel trafia tylko to, na co reaguję przy szafce. Pytania: czy zmieniam to przy
     akwarium? częściej niż raz w tygodniu? czy wartość mówi, że trzeba coś zrobić?
   - ISA-18.2 (alarmy): alarm musi mieć przyczynę, skutek, wymaganą reakcję i priorytet. Bez reakcji to
     status, nie alarm.
   - FMEA (uproszczone): co się stanie przy złej wartości, duplikacie komendy, utracie łączności. Wynik:
     poziom `safety` i limity egzekwowane przez węzeł.
3. Przegląd przekrojowy: nazwy, jednostki, rozdzielczości, wspólny profil.
4. Fazowanie: MVP (pierwsza wersja panelu) / później.

Stan list: propozycja do przeglądu. Pozycje oznaczone „?” wymagają decyzji. ID zamrażamy dopiero przy
implementacji tabeli `ParamDesc` w danym węźle — wcześniej wolno je przenumerować.

## Kolumny tabeli parametrów

| Kolumna | Wartości | Uwagi |
|---|---|---|
| ID | 0–15 wspólne, 16+ węzła | stabilne, nigdy nie reużywane po zamrożeniu |
| key | `snake_case`, po angielsku | nazwa w kodzie i w protokole |
| Etykieta | po polsku, krótko | tekst w UI (≤ ~16 znaków na panelu) |
| Kat. | patrz niżej | |
| Typ | `BOOL`, `INT`, `FIX1`, `FIX2`, `ENUM`, `ACTION` | `FIXn` = fixed-point z n miejscami (`decimals`) |
| Dostęp | `RO`, `RW`, `X` (akcja) | |
| Jedn. | °C, ml, ml/h, %, s, min, kWh, W, dBm | |
| Zakres / krok | min–max / step | walidacja na węźle |
| Panel | `tak`, `web`, `diag` | `web` = tylko web GUI, `diag` = tylko diagnostyka web |
| Safety | 0, 1, 2 | patrz niżej |
| Odśw. | `zm` (przy zmianie), `Ns` (cyklicznie) | dla STATE; pełny zrzut i tak co ~60 s |
| Faza | MVP / później | |

Kategorie (Kat.):

| Kod | Znaczenie | Typowy widget |
|---|---|---|
| RO | odczyt procesowy (temperatura, poziom) | etykieta z jednostką |
| ST | status / stan maszyny | kolor, ikona |
| SP | nastawa procesowa (zmieniana przy pracy) | spinbox −/+ |
| MD | tryb pracy | button matrix |
| AC | akcja jednorazowa | przycisk |
| CF | konfiguracja (rzadko zmieniana) | zwykle tylko web |
| DG | diagnostyka | tylko web |

## Poziomy safety (komendy)

| Poziom | Panel | Kiedy |
|---|---|---|
| 0 | od razu | akcja bezpieczna lub zatrzymująca (STOP, wyciszenie) |
| 1 | dialog potwierdzenia | zmiana nastawy lub uruchomienie aktuatora z limitem na węźle |
| 2 | ARM → EXEC (≤ 5 s) | dodaje substancję do akwarium, omija lub kasuje limit, kasuje liczniki |

Niezależnie od poziomu węzeł waliduje każdą komendę i egzekwuje limity — panel tylko prosi.

## Priorytety alarmów

| Priorytet | Reakcja | Panel | Przykład |
|---|---|---|---|
| P1 krytyczny | natychmiast, sterowanie zablokowane | budzi ekran, czerwony (buzzer lokalnie w węźle) | pusty zbiornik RO, awaria czujnika temp. |
| P2 wysoki | w ciągu godzin | budzi ekran, czerwony/pomarańczowy | temperatura poza zakresem, limit dobowy |
| P3 niski | przy okazji | żółty kafelek, bez budzenia | niski zapas, bateria RTC |

Każdy węzeł ma tabelę alarmów z kolumnami: kod, warunek, priorytet, skutek / co robi węzeł, reakcja,
kasowanie (auto / latch + reset), źródło.

## Wspólny profil (ID 0–15)

Każdy węzeł publikuje ten sam blok. Tożsamość (`device_id`, wersja firmware, `schema_hash`) jedzie
w HEARTBEAT, nie jako parametry.

| ID | key | Etykieta | Kat. | Typ | Dostęp | Jedn. | Panel | Safety | Odśw. | Uwagi |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `node_status` | Status | ST | ENUM `OK\|Uwaga\|Alarm\|Awaria\|Serwis` | RO | | tak | | zm | kolor kafelka; wyliczany na węźle |
| 1 | `system_enabled` | Automatyka | MD | BOOL | RW | | tak | 1 | zm | Auto / Serwis; brak w switch_timer i lampach |
| 2 | `alarm_mute` | Wycisz alarm | AC | ACTION | X | | tak | 0 | | wycisza buzzer aktywnych alarmów (wzorzec z termostatu) |
| 3 | `time_quality` | Czas | ST | ENUM `NTP\|RTC\|brak` | RO | | diag | | zm | panel używa do wyboru źródła czasu |
| 4 | `rtc_battery_low` | Bateria RTC | ST | BOOL | RO | | diag | | zm | alarm P3 |
| 5 | `rssi` | Wi-Fi | DG | INT | RO | dBm | diag | | 60s | |
| 6 | `uptime` | Uptime | DG | INT | RO | s | diag | | 60s | |
| 7 | `free_heap` | Wolna pamięć | DG | INT | RO | B | diag | | 60s | |
| 8 | `last_reset_reason` | Ostatni reset | DG | ENUM | RO | | diag | | zm | `esp_reset_reason()`, jak w lampach |
| 9–15 | — | rezerwa | | | | | | | | |

## Uwagi do `ParamDesc` (wnioski z inwentarza)

- Rozdzielić `type` od dostępu: obecnie `RO_VALUE` jest typem, a odczyty bywają BOOL, ENUM, FIX1. Proponuję
  `type` ∈ {BOOL, INT, ENUM, ACTION} + `decimals` + flaga `rw`.
- Dozownik (8 kanałów) i lampy (4 kanały) mają powtarzalne bloki parametrów. Do decyzji: pole `instance`
  w `ParamDesc` albo stała numeracja blokowa (`id = baza + kanał × 16 + offset`). Listy używają numeracji
  blokowej.
- Brakuje typu dla tekstu (nazwa kanału dozownika, nazwa programu lampy). Propozycja: `STRING` tylko RO,
  krótki (≤ 20 B), wysyłany w schemacie lub rzadkim STATE.
- Zależności walidacji między parametrami (termostat: `cool_full < alarm_delta_high` itd.) egzekwuje węzeł;
  panel pokazuje odrzucenie (`rejected` + komunikat z ACK).

## Do sprawdzenia przy pracy z danymi węzłów

- Nazwa węzła na panelu = nazwa z provisioningu (ustalone 2026-11-13 dla lamp: `HbBody.name` =
  `getDeviceID()`, bez aliasów na panelu; walidator dopuszcza 3–32 znaki `[A-Za-z0-9_-]`). Nie każdy węzeł
  pokazuje tę nazwę w top-barze swojego web GUI — sprawdzić w każdym projekcie (dolewka, dozownik,
  termostat, switch timer) i ujednolicić, tak żeby web GUI, panel i VPS pokazywały tę samą nazwę.
