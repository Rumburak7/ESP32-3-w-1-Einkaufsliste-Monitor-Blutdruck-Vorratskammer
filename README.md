[README_1.md](https://github.com/user-attachments/files/32165298/README_1.md)
# ESP32 3-w-1: Einkaufsliste + Blutdruck-Tagebuch + Vorratskammer

Trzy oddzielne projekty ESP32 połączone w **jeden szkic Arduino**, działający na jednym urządzeniu, z jednym serwerem WWW i jedną kartą SD. Wspólne menu główne pozwala przełączać się między aplikacjami z telefonu lub komputera w tej samej sieci WiFi.

| Moduł | Adres | Opis |
|---|---|---|
| 🏠 Menu główne | `/` | Zegar (zsynchronizowany z NTP) + linki do trzech aplikacji |
| 🛒 Einkaufsliste | `/einkaufsliste` | Listy zakupów (Aldi/Edeka/Penny), budżet miesięczny, rachunki, historia, auto-backup na SD |
| 🩺 Blutdruck-Tagebuch | `/cisnienie` | Dziennik pomiarów ciśnienia krwi, statystyki, wykresy, raport miesięczny (druk/PDF) |
| 📦 Vorratskammer | `/vorratskammer` | Inwentarz domowy (spiżarnia/środki czystości/kosmetyki), ważność produktów, minimalny stan |

## Sprzęt

- ESP32 Dev Module (zwykły, bez PSRAM)
- Czytnik karty SD podłączony po SPI (VSPI), sformatowanej jako **FAT32**

Piny karty SD (zdefiniowane na górze pliku):

| Sygnał | Pin ESP32 |
|---|---|
| CS   | GPIO 5  |
| SCK  | GPIO 18 |
| MOSI | GPIO 23 |
| MISO | GPIO 19 |

## Wymagane oprogramowanie

- [Arduino IDE](https://www.arduino.cc/en/software)
- Pakiet płytek **ESP32 by Espressif Systems** (Narzędzia → Płytka → Menedżer płytek)
- Biblioteki użyte w projekcie są częścią rdzenia ESP32 (nie trzeba nic dodatkowo instalować): `WiFi.h`, `WebServer.h`, `SPI.h`, `SD.h`, `ArduinoOTA.h`, `time.h`, `math.h`, `vector`

## Konfiguracja przed wgraniem

Na górze pliku `ESP32_3w1.ino` ustaw swoje dane WiFi:

```cpp
const char* WIFI_SSID     = "TWOJA_SIEC_WIFI";
const char* WIFI_PASSWORD = "TWOJE_HASLO_WIFI";
```

> ⚠️ **Uwaga bezpieczeństwa:** jeśli wrzucasz ten kod do **publicznego** repozytorium GitHub, nie zostawiaj w nim prawdziwego hasła WiFi — użyj powyższych placeholderów albo przenieś dane do osobnego, niewersjonowanego pliku (np. `secrets.h` dodanego do `.gitignore`).

Karta SD musi być sformatowana jako FAT32 i włożona do modułu przed uruchomieniem.

## Pierwsze wgranie (USB)

1. Podłącz ESP32 kablem USB.
2. Narzędzia → Płytka → **ESP32 Dev Module**.
3. Narzędzia → Port → wybierz port USB urządzenia.
4. Wgraj szkic.
5. Otwórz Monitor Portu Szeregowego (115200 baud) — po połączeniu z WiFi wyświetli się adres IP urządzenia.

## Aktualizacje przez WiFi (OTA)

Po pierwszym wgraniu przez USB kolejne aktualizacje można robić bezprzewodowo:

1. ESP32 i komputer muszą być w tej samej sieci WiFi.
2. Narzędzia → Port → pojawi się port sieciowy `ESP32-3w1 at <adres IP>` — wybierz go zamiast portu USB.
3. Kliknij Wgraj.

Hasło OTA jest domyślnie wyłączone (zakomentowane w kodzie, sekcja `setup()` → `ArduinoOTA.setPassword(...)`). Odkomentuj tę linię i wpisz własne hasło, jeśli chcesz zabezpieczyć wgrywanie przed innymi urządzeniami w tej samej sieci.

Aktualizacja OTA podmienia wyłącznie program — dane na karcie SD (listy zakupów, pomiary, spiżarnia) pozostają nietknięte.

## Dane i kopie zapasowe

Wszystkie trzy moduły zapisują dane na karcie SD pod własnymi, niekolidującymi nazwami plików (m.in. `penny.txt`, `aldi.txt`, `edeka.txt`, `totals.txt`, `history.txt`, `measurements.txt`, `vorratskammer.txt`). Każdy moduł ma własny eksport/import CSV z poziomu strony WWW, a Einkaufsliste dodatkowo robi automatyczny backup raz dziennie do folderu `/backup` na karcie SD.

## Dostosowanie limitów pamięci

Na zwykłym ESP32 (bez PSRAM) statyczne tablice danych muszą zmieścić się w dostępnym RAM. Jeśli po własnych modyfikacjach kompilator zgłosi błąd `DRAM segment data does not fit` / `region dram0_0_seg overflowed`, zmniejsz jedną z tych stałych:

- `MAX_MEASUREMENTS` (sekcja Blutdruck-Tagebuch) — liczba przechowywanych pomiarów ciśnienia
- `MAX_ITEMS` (sekcja Vorratskammer) — liczba produktów w spiżarni

## Licencja

Projekt prywatny/hobbystyczny — brak formalnej licencji. Dodaj plik `LICENSE` (np. MIT), jeśli chcesz publikować repozytorium jako open source.
