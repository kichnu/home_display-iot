# Panel dotykowy ESP-NOW — ustalenia i stack

Wersja 1: 2026-10-18 · aktualizacja: 2026-11-12 (nawigacja, start od lamp, czas i pamięć panelu)

> **Zmiany 2026-11-12**
> - Nowa sekcja: nawigacja i układ ekranów (ISA-101: per urządzenie, alarmy globalnie).
> - Kolejność wdrożenia zmieniona: start od lamp, przez istniejący protokół ESP-NOW lamp.
>   Szczegóły: `docs/PLAN_WDROZENIA.md`.
> - Panel bez RTC i FRAM jako bloków krytycznych; panel łączy się z routerem jak węzły (NTP, kanał).
>   Broadcast TIME z panelu usunięty z zakresu.

> **Zmiany 2026-10-23**
> - Decyzja: **węzły zostają połączone z routerem** (web GUI, NTP, brama VPS). ESP-NOW pracuje na
>   stałym kanale routera — zmienione założenia, plan kanałów i otwarte kwestie.
> - Wyniki uruchomienia płytki (krok 5, częściowo): RGB, GT911, CH422G działają.
> - Ze schematu: podświetlenie tylko on/off, na 4.3B nie ma USB_SEL (EXIO5 = DI1), odczyt DI koliduje
>   ze sterowaniem podświetleniem, brak wolnych GPIO.
> - Nowa sekcja: wygaszacz i wybudzanie.
> - Test odczytu DI przy wygaszonym ekranie: **bez błysków**, dotyk działa — kontaktron może iść na DI0.

## Cel i zakres

Centralny panel dotykowy w szafce akwariowej zastępuje mikro-displaye na węzłach (w tym planowany
w DOLEWCE). Panel pokazuje wyłącznie elementy sterujące i bieżące wartości parametrów.

- **W zakresie:** przełączniki, nastawy liczbowe, wybór trybu, akcje (np. ręczna dolewka, ręczna
  dawka), bieżące odczyty, statusy węzłów, alarmy.
- **Poza zakresem:** wykresy, historia, harmonogramy i zaawansowana konfiguracja. Zostają w web GUI
  poszczególnych węzłów.
- **Węzły:** DOLEWKA, DOZOWNIK, lampy (ESP-NOW już działa), kolejne w przyszłości.

## Założenia architektoniczne

Ruch panel–węzeł idzie przez ESP-NOW bezpośrednio radio–radio. Odległości między urządzeniami to
1–2 m (szafka i akwarium).

- **Węzły zostają na routerze** (decyzja 2026-10-23). Potrzebują go do web GUI (harmonogramy,
  konfiguracja), NTP i dostępu przez bramę VPS. Lampy już tak działają (`WiFi.begin()` + NTP).
- **Router poza ścieżką danych panelu.** Ramki ESP-NOW nie przechodzą przez router, więc VLAN-y
  i firewall MikroTika ich nie dotyczą. Router wyznacza natomiast **kanał**: urządzenie połączone
  z AP nadaje ESP-NOW na kanale AP.
- **Jeden stały kanał = kanał routera.** Kanał 2,4 GHz MikroTika ustawiony na sztywno (nie `auto`).
  Węzły dostają go z asocjacji, panel ustawia ten sam kanał (`esp_wifi_set_channel()`), bez
  `WiFi.begin()` — albo łączy się z routerem jak węzły (do decyzji, patrz otwarte kwestie).
- **Topologia gwiazdy.** Panel jest hubem, węzły liśćmi. Lampy dodatkowo synchronizują się między
  sobą (istniejący protokół, patrz „Komunikacja radiowa”).
- **Autonomia węzłów.** Harmonogramy, limity dobowe i safety manager działają lokalnie na węźle.
  Utrata panelu oznacza utratę UI, nie sterowania.
- **Czas (2026-11-12).** Panel łączy się z routerem jak węzły i ma NTP. Bez NTP bierze UTC
  z heartbeatów węzłów (`time_q = 2`). Panel nie steruje niczym zależnym od czasu: harmonogramy liczą
  węzły z własnych RTC, a czas na panelu służy tylko do zegara i znaczników alarmów. Dlatego RTC panelu
  (PCF85063) nie jest używany i nie jest blokiem krytycznym, a broadcast TIME z panelu wypada z zakresu.
- **Pamięć trwała panelu (2026-11-12).** Bez FRAM. Panel przechowuje tylko ustawienia UI i cache
  (nazwy programów, schematy) w NVS/LittleFS. Zapisy są rzadkie, bo zapis do flash zakłóca obraz RGB.
  Utrata cache oznacza ponowne pobranie od węzłów, a nie utratę stanu procesu. Protokół lamp nie
  potrzebuje trwałego `seq` (`boot_id` + `seq` w RAM). Przy migracji na AEAD `seq` panelu idzie
  rezerwacją bloków w NVS (patrz „Bezpieczeństwo”).
- **Bloki krytyczne panelu.** Panel nie zależy od RTC ani FRAM, więc reguła alarmu krytycznego nie
  obejmuje tych bloków. Awarię panelu widać po tym, że ekran nie działa, a sterowanie zostaje na węzłach.
  Utrata łączności z węzłem to alarm na panelu (brak heartbeatów).
- **Panel tylko prosi.** Każdą komendę waliduje węzeł; panel nie ma uprawnień do omijania limitów.

```
                 Panel ESP32-S3 4.3B
           UI, czas (NTP od węzłów / RTC), alarm na DO
              │  STATE · SET · ACK   (+ TIME awaryjnie)
     ┌────────┼──────────────┐
  DOLEWKA   DOZOWNIK        Lampy ⇄ Lampy (sync programów)
     └────────┴──────── Wi-Fi STA ──────── Router (stały kanał 2,4 GHz, web GUI, NTP, VPS)
```

## Sprzęt

Wybrana płytka: [Waveshare ESP32-S3-Touch-LCD-4.3B](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B),
najlepiej w wariancie -BOX z obudową.

| Element | Parametr | Zastosowanie w panelu |
|---|---|---|
| Moduł | ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB PSRAM) | LVGL z dwoma framebufferami 800×480 |
| Wyświetlacz | 4,3", 800×480, RGB565 (ST7262), dotyk GT911 (5 pkt) | ok. 217 ppi |
| Podświetlenie | przetwornica AP3032, CTRL = DISP z CH422G EXIO2 | **tylko on/off** (patrz niżej) |
| Zasilanie | USB-C 5 V lub DC 7–36 V, ~450 mA przy 5 V | szyna 12/24 V w szafce, osobna gałąź lub filtr LC |
| RTC | PCF85063 (I2C 0x51, INT na IO6) + ogniwo (tylko ładowalne) | nieużywany (2026-11-12, patrz „Czas”); ogniwa nie trzeba wkładać |
| DI ×2 | izolowane (PC814), 5–36 V, NPN/PNP → CH422G EXIO0/EXIO5 | kontaktron drzwi — **konflikt z CH422G**, patrz niżej |
| DO ×2 | open-drain (PC817), 5–36 V, maks. 450 mA/kanał, CH422G OC0/OC1 | nieużywane (2026-11-13): buzzery zostają lokalnie w węzłach |
| CAN, RS485 | z przełączanymi terminatorami (CAN IO15/16, RS485 IO43/44) | rezerwa: przewodowy fallback dla węzła |
| Gniazdo TF | microSD (SPI IO11/12/13, CS = EXIO4) | opcjonalnie obrazy firmware do OTA węzłów |
| Złącze I2C | IO8/IO9, wspólna szyna z GT911, RTC, CH422G | rozszerzenia (np. czujnik zbliżeniowy) |

**Montaż:** raczej na zewnątrz szafki lub na ścianie (2026-10-23), z dala od sumpa. Lakier konformalny
na PCB poza złączami i taśmą LCD (przy montażu poza szafką mniej krytyczny).

**Odrzucone:** ESP32-P4 (brak własnego radia, ESP-NOW tylko przez koprocesor), wersja 7B 1024×600
(~1,7× większe obciążenie pasma PSRAM), CYD ESP32-2432S028 (brak PSRAM; najwyżej jako panel zapasowy).

### Wyniki uruchomienia płytki (2026-10-23)

Firmware testowy: `~/Dokumenty/My_apps/TOOLS/test_ESP32-S3-Touch-LCD-4.3B/`, wzorzec w bazie:
`~/dev-knowledge/esp32/rgb-lcd-gt911-ch422g-bringup.md`.

- Ekran: `esp_lcd` RGB, 1 framebuffer w PSRAM + bounce buffery, PCLK 16 MHz, HSYNC/VSYNC 4/8/8 —
  stabilny obraz, wzorce kolorów, gradienty, „bit walk”, geometria OK.
- Dotyk GT911: adres 0x5D, fw 0x1060, konfiguracja 800×480, osie zgodne z ekranem (odchyłki ≤ ~25 px
  = dokładność palca), puszczenie zgłaszane przez kontroler, 0 błędów I2C. Przyciski 48 px trafiane
  bez problemu (≥ 80 px z dokumentu zostaje dla mokrych palców).
- CH422G: działa jako wyjścia (0x24 = 0x01, 0x38 = stan). Skan I2C pokazuje go pod 0x20–0x27
  i 0x30–0x3F — to nie są osobne urządzenia.
- Zasilanie z USB-C komputera wystarcza.
- Nie testowane: RTC, DO, DI pod napięciem, karta SD, ekran przy działającym radiu.
- Test odczytu DI przy wygaszonym ekranie (2026-10-23, komendy `d` / `f` w firmware testowym):
  każdy odczyt przełącza IO0–7 na wejścia na **~440–460 µs** (maks. 611 µs), odczyt 0x26, powrót do
  wyjść. Wyniki: przy odczycie co 200 ms i co 20 ms **ekran ciemny, bez widocznych błysków**
  (miękki start AP3032 nie zdąża zapalić diod); w trybie wejść wszystkie linie stoją wysoko (`0xFF`),
  więc resety dotyku i LCD pozostają nieaktywne — GT911 działał od razu po obu fazach, 0 błędów I2C
  w ~950 odczytach. Nie sprawdzono jeszcze reakcji DI0 na napięcie (wejście było w spoczynku).

### Ograniczenia sprzętowe ze schematu

- **Podświetlenie tylko on/off.** AP3032 ma wejście CTRL podłączone do DISP (EXIO2 przez 100R,
  podciąganie 4,7k do VIN). Ten sam DISP idzie na pin 31 panelu. PWM ze zwykłego GPIO nie jest
  doprowadzony, a „PWM” zapisami I2C byłby wolny i migałby. Pady przy FB przetwornicy (R11 10k, C11)
  wyglądają na wejście ściemniania analogowego — wymaga przeróbki płytki, niezweryfikowane.
- **Na 4.3B nie ma USB_SEL.** USB idzie wprost na IO19/20. Dema Arduino Waveshare (wspólne z 4.3)
  definiują `USB_SEL = EXIO5`, ale na 4.3B EXIO5 to DI1.
- **DI kontra sterowanie ekranem.** Kierunek IO0–7 w CH422G jest wspólny dla wszystkich linii (bit
  IO_OE). Odczyt DI0/DI1 wymaga chwilowego trybu wejść, w którym DISP i resety idą wysoko. Sprawdzone:
  krótki odczyt (~0,45 ms, co 20–200 ms) nie daje widocznego błysku i nie resetuje dotyku — DI da się
  odpytywać przy wygaszonym ekranie. Odczyt robić w tym samym tasku co inne operacje na I2C.
- **Brak wolnych GPIO.** IO6 = RTC_INT; pozostałe zajmują LCD, I2C, SD, CAN, RS485, USB i PSRAM.
  Dodatkowe wejścia tylko przez złącze I2C (wolne adresy: poza 0x14/0x5D, 0x51, 0x20–0x27, 0x30–0x3F).

## Wygaszacz i wybudzanie

Ograniczenie: podświetlenie ma tylko dwa stany, więc „przyciemnienie” można uzyskać jedynie ciemnym
obrazem (czarne IPS przy włączonym podświetleniu nadal lekko świeci).

**Stany ekranu**

| Stan | Wejście | Ekran |
|---|---|---|
| AKTYWNY | dotyk, alarm, (drzwi) | normalne UI |
| WYGASZACZ | ~60 s bez dotyku | czarne tło, mały zegar + kropki statusu węzłów + licznik alarmów; pozycja przesuwana co minutę |
| WYŁĄCZONY | ~5 min w wygaszaczu (lub od razu nocą) | DISP = 0 (podświetlenie i panel wyłączone), radio i logika działają |

Czasy do ustawienia w UI panelu. Przejście WYGASZACZ → AKTYWNY i WYŁĄCZONY → AKTYWNY zawsze
pokazuje ekran główny lub ostatnią stronę (do decyzji).

**Źródła wybudzenia**

| Źródło | Sprzęt | Uwagi |
|---|---|---|
| Dotyk | brak — **sprawdzone:** GT911 zgłasza dotyk przy wyłączonym podświetleniu | pierwszy dotyk tylko budzi i jest zjadany; potem ~300 ms blokady, żeby nie kliknąć przycisku „w ciemno” |
| Alarm węzła / brak heartbeatów | brak | budzi ekran i pokazuje alarm; dźwięk z buzzera węzła (panel bez buzzera, 2026-11-13) |
| Kontaktron drzwi przez DI0 | kontaktron + źródło 5–36 V (szyna 12/24 V) | odpytywanie przy wygaszonym ekranie sprawdzone (co ~100–200 ms, bez błysków); reakcja na napięcie do sprawdzenia; aktywny stanem niskim |
| Czujnik zbliżeniowy przez złącze I2C | np. VCNL4040 (0x60) | opcja na później: budzi przy zbliżeniu ręki, bez otwierania drzwi |

**Rekomendacja (panel na zewnątrz szafki / na ścianie):** dotyk + alarmy wystarczą. Kontaktron
drzwi szafki tylko jako opcja, gdyby panel trafił do szafki.

Wariant z panelem w szafce: dotyk + alarmy + kontaktron drzwi szafki na DI0 (odpytywany co ~100–200 ms).
Dotyk wystarcza do wybudzenia; kontaktron to dodatek — zapala ekran już przy otwieraniu drzwi
i pozwala wygasić go od razu po ich zamknięciu. Do zrobienia: test reakcji DI0 (patrz niżej).

**Kontaktron drzwi szafki:** magnetyczny czujnik otwarcia (jak w alarmach) — czujnik na ramie szafki,
magnes na drzwiach. Wersja NC (zwarty przy zamkniętych drzwiach): zerwany przewód = „drzwi otwarte” =
ekran się zapala, awaria od razu widoczna.

**Podłączenie DI0 (ze schematu, do potwierdzenia pomiarem):** listwa P1 — `DI_COM` (12), `GND` (13),
`DIN0` (14), `DIN1` (15). Prąd wejścia: `DI_COM` → 3,6 kΩ → transoptor PC814 (dwie diody przeciwsobnie)
→ `DIN0`. `DI_COM` jest zasilane od środka z 5 V płytki przez D4 (1N5822) i R79 220 Ω.

| Wariant | Podłączenie | Uwagi |
|---|---|---|
| Najprostszy | kontaktron między `DIN0` a `GND`, `DI_COM` wolne | wewnętrzne 5 V, ~0,9 mA — może być na granicy CTR transoptora |
| Z szyny 12 V (zalecany) | `+12 V` → `DI_COM`, kontaktron między `DIN0` a `GND` (masa wspólna z płytką) | ~3 mA, pewne zadziałanie; D4 odcina 12 V od wewnętrznego 5 V |
| Unikać | `DI_COM` do GND, `+12 V` na `DIN0` | zwiera wewnętrzne 5 V przez 220 Ω (~20 mA, grzanie R79) |

Samo `+12 V` na `DIN0` przy wolnym `DI_COM` nie zadziała (brak drogi powrotnej prądu).
Plan testu (komenda `d`): 1) zworka `DIN0`–`GND`, `DI_COM` wolne; 2) `+12 V` na `DI_COM`, zworka
`DIN0`–`GND`; 3) kontaktron zamiast zworki. W logu oczekiwane `DI0=0 (ACTIVE)` przy zwarciu.

## Komunikacja radiowa

Wszystkie urządzenia pracują na jednym, sztywno ustawionym kanale — kanale routera. Router wpływa
na ESP-NOW tylko tym, że wyznacza kanał, i jako źródło ruchu w paśmie 2,4 GHz.

| Ustawienie | Wartość |
|---|---|
| Kanał ESP-NOW | = kanał 2,4 GHz routera; węzły przez asocjację, panel `esp_wifi_set_channel()` (tryb `WIFI_STA`) |
| MikroTik 2,4 GHz | sztywny kanał (nie `auto`), wybrany po skanie sąsiednich sieci (1, 6 lub 11) |
| Klienci domowi | w miarę możliwości na 5 GHz |
| Uśpienie modemu | `WiFi.setSleep(false)` na węzłach — modem sleep gubi ramki ESP-NOW |
| Moc TX | ok. 8 dBm: `esp_wifi_set_max_tx_power(32)`; RSSI na 1–2 m ok. −50 dBm. Węzły muszą nadal mieć zasięg do AP |
| Prędkość | domyślnie 1 Mbps (~2 ms na 250 B); opcjonalnie 6–24 Mbps OFDM |
| Wersja ESP-NOW | v1 (250 B), dopóki wszystkie węzły nie są na ESP-IDF ≥ 5.4 |

- Unicast ma ACK i retransmisje na poziomie MAC. Broadcast ich nie ma, więc lampy liczą fade lokalnie
  z własnego harmonogramu, a utrata pojedynczej ramki czasu jest bez znaczenia.
- Węzeł przy ponownym łączeniu z AP skanuje kanały i przez ten czas gubi ramki. Przy stałym kanale
  AP to krótka przerwa; panel i tak ponawia SET do ACK, a heartbeat co ~5 s pokaże przerwę.
- **Lampy** (`led_lamp_aquarium-iot`): mają już własny protokół ESP-NOW — HMAC-SHA256 z klucza
  wyliczonego z hasła Wi-Fi, heartbeat co ~15 s, synchronizacja programów i polecenia „ostatnie wygrywa”
  między lampami (`~/dev-knowledge/esp32/espnow-sync.md`). Przejście na wspólną bibliotekę panelu musi
  zachować lub przenieść tę synchronizację lampa–lampa.

## Bezpieczeństwo

Ramki szyfruje warstwa aplikacji (AES-GCM, mbedTLS z akceleracją sprzętową S3), a nie natywne
szyfrowanie ESP-NOW. Obniżona moc TX ogranicza ekspozycję poza budynkiem, ale nie zastępuje szyfrowania.

- **Dlaczego AEAD w aplikacji:** natywne szyfrowanie nie obsługuje broadcastu, ma niski limit
  szyfrowanych peerów, a jego zmiana wymaga sdkconfig.
- **Klucze:** parowy dla każdej pary węzeł–panel. Przechowywane jak w DOLEWCE/DOZOWNIKU: FRAM, AES-256
  (lampy bez FRAM: NVS).
- **Parowanie (2026-11-12, propozycja techniczna).** Inicjowane i potwierdzane wyłącznie na ekranie
  panelu, bez przycisków na węzłach. Panel pokazuje wykryte, niesparowane węzły, a dotknięcie „Paruj”
  uruchamia wymianę ECDH (X25519, mbedTLS). Ramki wymiany uwierzytelnia klucz grupowy z hasła Wi-Fi
  (jak dziś HMAC lamp), więc sparować może tylko urządzenie znające hasło sieci. Wynik to klucz parowy
  zapisany na obu stronach. Usunięcie parowania odbywa się na panelu, a węzeł ma reset parowania w web
  GUI. Lampy w etapie 1 nie wymagają parowania, bo panel używa ich istniejącego klucza grupowego.
- **Nonce = `src‖seq`, nigdy powtórzony.** Na węzłach z FRAM `seq` zapisywany przy każdej ramce
  (wytrzymałość ~10¹⁴ cykli). Bez FRAM (lampy) rezerwacja bloków w NVS, np. `seq += 1024` przy boocie.
- **Replay:** odbiorca odrzuca `seq ≤ last_seq[src]`. Po restarcie panelu challenge-response: losowy
  nonce od panelu, węzeł odpowiada aktualnym `seq` z tagiem.
- **Jamming:** nie da się go zablokować. Odpowiedź to autonomia węzłów i alarm na wyjściu DO panelu
  po utracie heartbeatów.
- Lampy dziś: tylko HMAC (bez szyfrowania), klucz z hasła Wi-Fi — do migracji na AEAD razem ze
  wspólną biblioteką.

## Protokół

Każda wiadomość to jedna ramka `ver(1) | type(1) | src(1) | seq(4) | payload | tag(8–16)`, co przy
ESP-NOW v1 daje ok. 220 B na payload.

| Typ | Kierunek | Treść | Uwagi |
|---|---|---|---|
| HEARTBEAT | węzeł → panel, co ~5 s | status, `schema_hash`, UTC + jakość czasu | 3 braki = węzeł nieaktualny, alarm |
| SCHEMA_REQ / SCHEMA_CHUNK | panel ↔ węzeł | schemat parametrów | tylko gdy hash różni się od cache |
| STATE | węzeł → panel | TLV zmienionych parametrów | pełny zrzut co ~60 s |
| SET | panel → węzeł | `cmd_id`, `param_id`, `value` | idempotentne po `cmd_id` |
| ACK | węzeł → panel | `cmd_id`, status, `actual_value` | węzeł może przyciąć wartość |
| ARM / EXEC | panel → węzeł | komendy z safety 2 | EXEC ważny ≤ 5 s od ARM |
| ALARM | węzeł → panel | kod, priorytet | budzi ekran panelu |
| ~~TIME~~ | — | — | usunięte 2026-11-12: panel bez RTC, węzły mają własne RTC i NTP |

- Setter z walidacją na węźle jest jedyną bramą do stanu; panel i web GUI nie mają osobnych ścieżek.
- Limity dobowe (np. dozowania) egzekwuje węzeł, nie panel.
- `param_id` nigdy nie zmienia znaczenia; usunięty parametr zostawia lukę w numeracji.
- Wspólna biblioteka (ramka, AEAD, seq, typy) jest współdzielona przez panel, DOLEWKĘ, DOZOWNIK i lampy.

## Model danych UI

Panel nie ma ręcznie pisanych ekranów dla poszczególnych węzłów. Renderuje UI generycznie
z deskryptora parametrów, który publikuje każdy węzeł. Ta sama tabela zasila web GUI węzła, więc oba
interfejsy są funkcjonalnie zgodne z definicji.

```cpp
struct ParamDesc {
  uint8_t  id;          // stabilne ID, nigdy nie reużywane
  uint8_t  type;        // BOOL, INT, FLOAT, ENUM, ACTION, RO_VALUE
  uint8_t  group;       // zakładka na stronie węzła
  uint8_t  safety;      // 0 wolne, 1 dialog, 2 ARM→EXEC
  bool     panel;       // false = tylko web GUI (konfiguracja zaawansowana)
  int32_t  min, max, step;
  uint8_t  decimals;    // fixed-point
  const char* unit;
  const char* label;    // po polsku
  const char* enumOpts; // "Auto|Ręczny|Off"
  getter_t get; setter_t set;
};
```

- **Listy punktów węzłów:** `docs/nodes/` (konwencje w `_konwencje.md`, po pliku na węzeł) — źródło
  tabel `ParamDesc`.
- **Flaga `panel`:** do schematu panelu trafiają tylko parametry sterujące i bieżące odczyty.
  Harmonogramy i konfiguracja zaawansowana zostają w web GUI.
- **Discovery:** panel trzyma schematy w LittleFS, kluczem jest węzeł + `schema_hash`. Przy zawężonym
  zakresie schemat to zwykle poniżej 1–2 KB, czyli kilka–kilkanaście ramek.

| Typ | Widget LVGL | Uwagi |
|---|---|---|
| BOOL | `lv_switch` | |
| INT / FLOAT (rw) | `lv_spinbox` + duże przyciski −/+ | suwak tylko dla wartości orientacyjnych |
| ENUM | `lv_buttonmatrix` (≤ 4 opcje) lub `lv_roller` | |
| RO_VALUE | `lv_label` z jednostką | |
| ACTION | `lv_button` | safety 1: `lv_msgbox`; safety 2: uzbrój → 5 s → wykonaj |

- **Wiązanie:** `lv_subject_t` per parametr (LVGL 9). Task UI po odebraniu STATE wywołuje tylko
  `lv_subject_set_int()`.
- **Stany:** *pending* (SET bez ACK, widget przyciemniony), *stale* (węzeł bez heartbeatu, strona
  szara i zablokowana), *rejected* (powrót do `actual_value` + komunikat).
- **Nawigacja:** ekran główny z kafelkiem na węzeł (kolor statusu, 2–3 kluczowe wartości) → strona
  węzła z `lv_tabview` po `group`. Strony tworzone przy wejściu i usuwane przy wyjściu; subjecty
  zostają w pamięci.

## Nawigacja i układ ekranów

Ustalenie 2026-11-12. Nawigacja idzie per urządzenie (węzeł lub grupa węzłów). Podział funkcjonalny
(odczyt / sterowanie / błędy) obowiązuje wewnątrz ekranu węzła jako stałe strefy układu, a nie jako
osobne ekrany. Alarmy to jedyna warstwa globalna.

Dlaczego nie ekrany per funkcja: sterowanie bez kontekstu (zmiana nastawy bez widocznego skutku),
więcej przejść „widzę problem → reaguję”, ryzyko pomyłki węzła na wspólnej liście nastaw.
Dlaczego nie czysto per urządzenie: alarm P1 jednego węzła musi być widoczny z każdego ekranu (ISA-18.2).

| Poziom | Ekran | Podział |
|---|---|---|
| L1 | Przegląd: kafelek na węzeł lub grupę (kolor `node_status`, 1–2 kluczowe wartości, licznik alarmów) | per urządzenie |
| L2 | Ekran węzła / grupy | per urządzenie, wewnątrz strefy wg kategorii |
| L3 | Szczegół instancji (kanał dozownika, pojedyncza lampa) | per instancja |
| Globalnie | Pasek alarmów (zawsze widoczny) + lista alarmów P1→P3; wpis prowadzi do ekranu węzła | per funkcja |
| L4 | Diagnostyka (wspólny profil ID 0–15) | web; ewentualnie jedna tabela wszystkich węzłów |

Strefy ekranu L2, w tym samym miejscu dla każdego węzła:

| Strefa | Kategorie | Zawartość |
|---|---|---|
| Nagłówek | ST | nazwa, `node_status`, stan łączności, alarmy tego węzła |
| Odczyty | RO, ST | bieżące wartości procesowe |
| Sterowanie | MD, SP, AC | tryby, nastawy, akcje (safety wg `_konwencje.md`) |

Zasady:

- Z L1 do każdej nastawy najwyżej 2 dotknięcia, z alarmu do jego węzła 1 dotknięcie.
- Węzły synchronizowane między sobą (lampy) mają jeden kafelek i jeden ekran grupy. Sterowanie działa
  na całej grupie, a stan poszczególnych członków jest widoczny jako wiersze. Rozjazd stanu to ostrzeżenie.
- Akcje obejmujące kilka węzłów (np. przyszły „tryb karmienia / serwis”) to sceny: przycisk na L1, nie
  w ekranie jednego z węzłów.

## Stack technologiczny

Kluczowe ustawienia RGB i PSRAM wymagają własnego sdkconfig.

| Warstwa | Wybór | Uwagi |
|---|---|---|
| Build | PlatformIO + pioarduino, `framework = arduino` z `custom_sdkconfig` (alternatywa: `framework = espidf, arduino`) | własny sdkconfig: `SPIRAM_FETCH_INSTRUCTIONS`, `SPIRAM_RODATA`; `custom_sdkconfig` jeszcze nie testowany |
| Wyświetlacz | `esp_lcd` RGB + `esp_lvgl_port` | 2 framebuffery w PSRAM (~750 KB każdy), bounce buffer, `LV_DISPLAY_RENDER_MODE_DIRECT`, synchronizacja z VSYNC |
| Dotyk | `esp_lcd_touch` GT911 (lub własny sterownik z testu) | punkt 1 od rejestru 0x814F |
| Ekspander | CH422G | reset LCD i dotyku, podświetlenie on/off, DI (konflikt kierunku) |
| GUI | LVGL 9.x | `lv_subject`; dema Waveshare (LVGL 8) tylko do inicjalizacji sprzętu |
| Edytor | EEZ Studio (open source), opcjonalnie | ekran główny i style; strony węzłów generowane w kodzie |
| Fonty | Noto Sans TTF we flashu + LVGL TinyTTF (2026-11-12) | pełne polskie znaki bez konwertera (uwaga: Ó/ó to U+00D3/U+00F3, poza Latin Extended-A); ikony `LV_SYMBOL_*` z wbudowanego Montserrat jako fallback |
| Radio | ESP-NOW v1 | kanał routera, obniżona moc TX |
| Kryptografia | mbedTLS AES-GCM | akceleracja sprzętowa S3 |
| Pamięć | NVS, LittleFS | cache schematów, klucze, `last_seq` |
| Rozwój UI | symulator LVGL SDL2 na Linux Mint, env `native` | mockowe schematy i strumienie STATE z pliku |
| Wspólna biblioteka | ramka, AEAD, seq, deskryptor | współdzielona przez panel, DOLEWKĘ, DOZOWNIK i lampy |

## Znane pułapki i mitygacje

Największe ryzyka to zakłócenia obrazu RGB przy zapisie do flash oraz duplikaty komend dozowania.

| Problem | Przyczyna | Mitygacja |
|---|---|---|
| Obraz pływa lub miga przy zapisie NVS/LittleFS/OTA | zapis wyłącza cache, framebuffer czytany z PSRAM | bounce buffer w SRAM, kod i rodata z PSRAM, rzadkie zapisy |
| Miganie elementów przy częstym przerysowaniu | rysowanie wprost w wyświetlanym framebufferze | LVGL z 2 fb / DIRECT; bez LVGL bufor poza ekranem + kopia (sprawdzone w teście) |
| Zawieszenia lub crash UI | wywołania `lv_*` z callbacku ESP-NOW (task Wi-Fi) | `recv_cb` → `xQueueSend` → task UI |
| Puste kwadraty zamiast ą, ę, ł | fonty Montserrat bez Latin Extended-A | TinyTTF z Noto Sans (cały zestaw znaków fontu) |
| Brak ściemniania | podświetlenie przez CH422G → AP3032 CTRL, tylko on/off (potwierdzone na schemacie) | ciemny wygaszacz + wyłączenie; ewentualnie przeróbka (pady FB) |
| Błysk podświetlenia / reset dotyku przy odczycie DI | wspólny kierunek IO0–7 w CH422G | krótki odczyt (~0,45 ms) — sprawdzone: bez błysku i bez resetu |
| Fałszywe dotknięcia | mokre palce, zakłócenia zasilaczy pomp | debounce gestów, blokada ekranu, filtr zasilania |
| Reset brownout | rozruch pomp na wspólnej szynie | osobna gałąź zasilania lub filtr LC |
| Utrata ramek ESP-NOW | węzeł skanuje kanały przy ponownym łączeniu z AP | sztywny kanał na MikroTiku, ponawianie SET, heartbeat |
| Obraz pływa przy pracy radia | radio i RGB dzielą pasmo PSRAM | bounce buffer; do sprawdzenia na płytce z ESP-NOW |
| Niezgodność ramek | różne wersje firmware/IDF | bajt `ver`, ESP-NOW v1 (250 B) |
| Za małe cele dotyku | ~217 ppi | min. ~80 px (9–10 mm), 4–5 wierszy na stronę |
| Duplikat komendy dozowania | retry po utracie ACK | `cmd_id`, ARM/EXEC, limity na węźle |
| Korozja | aerozol solny w szafce | obudowa -BOX, lakier, montaż wysoko |

## Kolejność wdrożenia

Zmienione 2026-11-12: start od lamp przez istniejący protokół ESP-NOW lamp, a wspólna biblioteka
(AEAD, `ParamDesc` po radiu) dopiero po działającym panelu. Plan etapów, kryteria i testy:
`docs/PLAN_WDROZENIA.md`.

Ustalone 2026-11-14 (dolewka, `docs/PLAN_DOLEWKA.md`): kolejne węzły (dolewka, dozownik, termostat, switch
timer) podłączamy tak samo jak lampy — własny protokół węzła na ESP-NOW, HMAC-SHA256 z kluczem z hasła Wi-Fi
i osobnym kontekstem per węzeł, `boot_id` + `seq`, a komendy dodatkowo z jednorazowym `cmd_nonce` z ostatniego
statusu węzła (ochrona przed odtworzeniem nagranej ramki). W panelu adapter per węzeł i ekran pisany ręcznie.
Wspólna biblioteka (AEAD, parowanie, `ParamDesc` po radiu) zostaje celem docelowym, bez terminu.

Poprzednia kolejność (2026-10-23), do wglądu: wspólna biblioteka → plan kanałów → `ParamDesc`
w DOLEWCE → generator stron w symulatorze → bring-up płytki → integracja DOLEWKA, DOZOWNIK, lampy.

## Otwarte kwestie

- [x] ~~Czy węzły zachowują połączenie z routerem?~~ Tak (2026-10-23) — kanał ESP-NOW = kanał AP.
- [x] ~~Czy obecny firmware lamp wywołuje `WiFi.begin()`?~~ Tak (Wi-Fi STA + NTP + VPS) i zostaje.
- [x] ~~Czy podświetlenie 4.3B obsługuje PWM?~~ Nie — tylko on/off przez CH422G (schemat: AP3032 CTRL).
- [x] ~~Panel: tylko ustawiony kanał czy połączenie z routerem?~~ Połączenie z routerem jak węzły
      (2026-11-12): kanał z asocjacji, NTP, ArduinoOTA przy rozwoju. Hasło Wi-Fi i tak potrzebne do klucza HMAC lamp.
- [x] ~~Źródło czasu?~~ NTP panelu, rezerwowo UTC z heartbeatów węzłów; RTC panelu nieużywany (2026-11-12).
- [ ] Docelowy kanał 2,4 GHz po skanie otoczenia.
- [x] ~~Provisioning kluczy: wpisywane przy buildzie czy parowanie z panelu?~~ Parowanie z panelu,
      bez przycisków na węzłach (2026-11-12) — patrz „Bezpieczeństwo” → „Parowanie”.
- [ ] OTA węzłów: przez Wi-Fi z routera (jak dziś ArduinoOTA) czy z karty SD panelu?
- [x] ~~Kontaktron drzwi: DI0 czy złącze I2C?~~ DI0 — krótki odczyt nie błyska (2026-10-23). Przy
      panelu poza szafką kontaktron opcjonalny; wybudzanie dotykiem i alarmem.
- [ ] Migracja lamp z HMAC na wspólną bibliotekę z zachowaniem synchronizacji lampa–lampa.

**Źródła:** [Waveshare ESP32-S3-Touch-LCD-4.3B — dokumentacja](https://docs.waveshare.com/ESP32-S3-Touch-LCD-4.3B),
schemat `ESP32-S3-Touch-LCD-4.3B-Sch.pdf`, paczka demo `ESP32-S3-Touch-LCD-4.3B-BOX-Demo.zip`,
`~/dev-knowledge/esp32/espnow-sync.md`, `~/dev-knowledge/esp32/rgb-lcd-gt911-ch422g-bringup.md`.
