/*
  ============================================================
  ESP32 3-w-1: Einkaufsliste + Monitor Blutdruck + Vorratskammer
  ============================================================
  Ten plik łączy Twoje 3 oddzielne programy w JEDEN szkic (sketch)
  dla zwykłego ESP32 Dev Module, z jednym wspólnym serwerem WWW
  na porcie 80 i jedną kartą SD używaną przez wszystkie trzy.

  Strona główna (http://<IP-ESP32>/) to nowe MENU z linkami do:
    - 🛒 Einkaufsliste   -> http://<IP-ESP32>/einkaufsliste
    - 🩺 Ciśnienie       -> http://<IP-ESP32>/cisnienie
    - 📦 Vorratskammer   -> http://<IP-ESP32>/vorratskammer

  CO ZOSTAŁO ZMIENIONE WZGLĘDEM ORYGINAŁÓW (żeby dało się je połączyć):
  - WIFI_SSID / WIFI_PASSWORD, piny karty SD, obiekt WebServer i flaga
    timeSynced są zdefiniowane TYLKO RAZ (na górze pliku) i współdzielone.
  - Einkaufsliste miała własne (puste) WIFI_SSID/WIFI_PASSWORD - użyłem
    tych samych danych WiFi co w pozostałych dwóch programach
    ("FRITZ!Box 7590 QC"). Jeśli lista zakupów miała być w INNEJ sieci
    WiFi niż reszta - zmień WIFI_SSID/WIFI_PASSWORD poniżej.
  - Strona główna Einkaufsliste (dawniej pod "/") jest teraz pod
    "/einkaufsliste" (dodałem przycisk 🏠 MENU, żeby wrócić).
  - W Cisnienie zmienne czasu (currentYear/Month/Day/Hour/Minute/Sec)
    zmieniłem na cisnienieYear/Month/Day/Hour/Minute/Sec - Einkaufsliste
    już używała nazw currentYear/currentMonth do własnego budżetu
    miesięcznego, więc byłaby kolizja nazw.
  - Dodałem przycisk 🏠 (powrót do menu) na stronie Vorratskammer.
  - Wszystkie trzy programy dalej zapisują dane pod SWOIMI oryginalnymi
    nazwami plików na karcie SD (penny.txt, aldi.txt, edeka.txt,
    totals.txt, history.txt, measurements.txt, vorratskammer.txt, itd.)
    - żadnych konfliktów nazw plików nie ma, więc dane się NIE nadpiszą.

  UWAGA O PAMIĘCI RAM (ESP32 bez PSRAM):
  W komentarzach oryginalnych plików było już wspomniane, że duże
  tablice (MAX_MEASUREMENTS=3000 w Cisnienie, MAX_ITEMS=300 w
  Vorratskammer) zajmują sporo statycznej pamięci RAM. Po połączeniu
  WSZYSTKICH trzech programów w jeden, te tablice sumują się
  (Cisnienie ~48 KB + Vorratskammer ~26 KB + Einkaufsliste ~14 KB =
  ~88 KB). To powinno się zmieścić na zwykłym ESP32 Dev Module, ale
  jeśli kompilacja zgłosi błąd typu "DRAM segment data does not fit"
  / "region dram0_0_seg overflowed" - zmniejsz MAX_MEASUREMENTS
  (linijka w sekcji CIŚNIENIE) i/lub MAX_ITEMS (sekcja VORRATSKAMMER).

  CO ZMIENIĆ PONIŻEJ (linijki oznaczone <<<):
  - WIFI_SSID / WIFI_PASSWORD

  PO WGRANIU: Monitor Portu Szeregowego (115200 baud) pokaże adres IP.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <math.h>
#include <vector>

// ================= WSPÓLNE USTAWIENIA (WiFi + SD) =================
const char* WIFI_SSID     = "HIER_WLAN_NAME_EINGEBEN";     // <<<
const char* WIFI_PASSWORD = "PASSWORT_HIER_EINGEBEN";  // <<<
WLAN-Passwort hier eingeben
// Piny karty SD (VSPI) - te same, których używały wszystkie 3 programy
#define SD_CS   5
#define SD_SCK  18
#define SD_MOSI 23
#define SD_MISO 19

WebServer server(80);
bool timeSynced = false; // czy udało się pobrać czas NTP (próba w tle w loop())


// ============================================================
// CZĘŚĆ 1: EINKAUFSLISTE (routes: /einkaufsliste, /rechnung, ...)
// ============================================================

// Data ostatniego automatycznego nocnego restartu ("YYYYMMDD"),
// zeby restart o 4:00 zdarzyl sie tylko RAZ danej nocy (przetrwa
// restart ESP32 - trzymane na karcie SD).
String lastAutoRestartDate = "";

// ============================================================
// PRODUKTE
// ============================================================
struct Item {
  String name;
  int quantity;
  bool bought;
};

Item pennyItems[200];
Item aldiItems[200];
Item edekaItems[200];

int pennyCount = 0;
int aldiCount = 0;
int edekaCount = 0;

// ============================================================
// RECHNUNG (Paragon Summen)
// ============================================================
float pennyTotal = 0;
float aldiTotal = 0;
float edekaTotal = 0;
float backereiTotal = 0;
float sonstigeTotal = 0;
float grandTotal = 0;

float monthlyBudget = 700.00;
float remainingBudget = 700.00;

std::vector<float> pennyHistory;
std::vector<float> aldiHistory;
std::vector<float> edekaHistory;
std::vector<float> backereiHistory;
std::vector<float> sonstigeHistory;

int currentMonth = -1;
int currentYear = -1;

// ============================================================
// AUTOMATYCZNY BACKUP (cala lista zakupow + Monatliches Budget)
// ============================================================
String lastBackupDate = ""; // "YYYYMMDD" ostatniego zrobionego backupu

struct MonthlyBill {
  int month;
  int year;
  float penny;
  float aldi;
  float edeka;
  float backerei;
  float sonstige;
  float total;
};
MonthlyBill billHistory[12];
int billCount = 0;

// ============================================================
// SPEICHERN / LADEN
// ============================================================
void saveData() {
  File f = SD.open("/penny.txt", "w");
  if (f) {
    f.println(pennyCount);
    for (int i = 0; i < pennyCount; i++) {
      f.println(pennyItems[i].name);
      f.println(pennyItems[i].quantity);
      f.println(pennyItems[i].bought ? 1 : 0);
    }
    f.close();
  }
  f = SD.open("/aldi.txt", "w");
  if (f) {
    f.println(aldiCount);
    for (int i = 0; i < aldiCount; i++) {
      f.println(aldiItems[i].name);
      f.println(aldiItems[i].quantity);
      f.println(aldiItems[i].bought ? 1 : 0);
    }
    f.close();
  }
  f = SD.open("/edeka.txt", "w");
  if (f) {
    f.println(edekaCount);
    for (int i = 0; i < edekaCount; i++) {
      f.println(edekaItems[i].name);
      f.println(edekaItems[i].quantity);
      f.println(edekaItems[i].bought ? 1 : 0);
    }
    f.close();
  }
}

void loadData() {
  if (SD.exists("/penny.txt")) {
    File f = SD.open("/penny.txt", "r");
    if (f) {
      pennyCount = f.parseInt(); f.readStringUntil('\n');
      for (int i = 0; i < pennyCount && i < 200; i++) {
        pennyItems[i].name = f.readStringUntil('\n'); pennyItems[i].name.trim();
        pennyItems[i].quantity = f.parseInt(); f.readStringUntil('\n');
        pennyItems[i].bought = f.parseInt() == 1; f.readStringUntil('\n');
      }
      f.close();
    }
  }
  if (SD.exists("/aldi.txt")) {
    File f = SD.open("/aldi.txt", "r");
    if (f) {
      aldiCount = f.parseInt(); f.readStringUntil('\n');
      for (int i = 0; i < aldiCount && i < 200; i++) {
        aldiItems[i].name = f.readStringUntil('\n'); aldiItems[i].name.trim();
        aldiItems[i].quantity = f.parseInt(); f.readStringUntil('\n');
        aldiItems[i].bought = f.parseInt() == 1; f.readStringUntil('\n');
      }
      f.close();
    }
  }
  if (SD.exists("/edeka.txt")) {
    File f = SD.open("/edeka.txt", "r");
    if (f) {
      edekaCount = f.parseInt(); f.readStringUntil('\n');
      for (int i = 0; i < edekaCount && i < 200; i++) {
        edekaItems[i].name = f.readStringUntil('\n'); edekaItems[i].name.trim();
        edekaItems[i].quantity = f.parseInt(); f.readStringUntil('\n');
        edekaItems[i].bought = f.parseInt() == 1; f.readStringUntil('\n');
      }
      f.close();
    }
  }
}

void saveTotals() {
  File f = SD.open("/totals.txt", "w");
  if (f) {
    f.println(currentMonth);
    f.println(currentYear);
    f.println(pennyTotal);
    f.println(aldiTotal);
    f.println(edekaTotal);
    f.println(backereiTotal);
    f.println(sonstigeTotal);
    f.println(grandTotal);
    f.println(monthlyBudget);
    f.println(remainingBudget);
    f.close();
  }
}

void loadTotals() {
  if (SD.exists("/totals.txt")) {
    File f = SD.open("/totals.txt", "r");
    if (f) {
      currentMonth = f.parseInt(); f.readStringUntil('\n');
      currentYear = f.parseInt(); f.readStringUntil('\n');
      pennyTotal = f.parseFloat(); f.readStringUntil('\n');
      aldiTotal = f.parseFloat(); f.readStringUntil('\n');
      edekaTotal = f.parseFloat(); f.readStringUntil('\n');
      backereiTotal = f.parseFloat(); f.readStringUntil('\n');
      sonstigeTotal = f.parseFloat(); f.readStringUntil('\n');
      grandTotal = f.parseFloat(); f.readStringUntil('\n');
      monthlyBudget = f.parseFloat(); f.readStringUntil('\n');
      remainingBudget = f.parseFloat(); f.readStringUntil('\n');
      f.close();
    }
  } else {
    remainingBudget = monthlyBudget;
  }
}

// Zapisuje aktualna zawartosc billHistory[0..billCount-1] na SD.
// Wydzielone z saveMonthlyBill, zeby moc wywolac to samo tez po imporcie CSV.
void writeHistoryFile() {
  File f = SD.open("/history.txt", "w");
  if (f) {
    f.println(billCount);
    for (int i = 0; i < billCount; i++) {
      f.println(billHistory[i].month);
      f.println(billHistory[i].year);
      f.println(billHistory[i].penny);
      f.println(billHistory[i].aldi);
      f.println(billHistory[i].edeka);
      f.println(billHistory[i].backerei);
      f.println(billHistory[i].sonstige);
      f.println(billHistory[i].total);
    }
    f.close();
  }
}

void saveMonthlyBill(int month, int year, float penny, float aldi, float edeka, float backerei, float sonstige, float total) {
  for (int i = 11; i > 0; i--) billHistory[i] = billHistory[i-1];
  billHistory[0] = {month, year, penny, aldi, edeka, backerei, sonstige, total};
  if (billCount < 12) billCount++;
  writeHistoryFile();
}

void loadHistory() {
  if (SD.exists("/history.txt")) {
    File f = SD.open("/history.txt", "r");
    if (f) {
      billCount = f.parseInt(); f.readStringUntil('\n');
      if (billCount > 12) billCount = 12;
      for (int i = 0; i < billCount; i++) {
        billHistory[i].month = f.parseInt(); f.readStringUntil('\n');
        billHistory[i].year = f.parseInt(); f.readStringUntil('\n');
        billHistory[i].penny = f.parseFloat(); f.readStringUntil('\n');
        billHistory[i].aldi = f.parseFloat(); f.readStringUntil('\n');
        billHistory[i].edeka = f.parseFloat(); f.readStringUntil('\n');
        billHistory[i].backerei = f.parseFloat(); f.readStringUntil('\n');
        billHistory[i].sonstige = f.parseFloat(); f.readStringUntil('\n');
        billHistory[i].total = f.parseFloat(); f.readStringUntil('\n');
      }
      f.close();
    }
  }
}

void calculateTotals() { 
  grandTotal = pennyTotal + aldiTotal + edekaTotal + backereiTotal + sonstigeTotal;
  remainingBudget = monthlyBudget - grandTotal;
}

void checkNewMonth() {
  time_t now = time(nullptr);
  
  // Zabezpieczenie przed rokiem 1970 (czas niezsynchronizowany)
  if (now < 1000000000) {  // około roku 2001
    Serial.println("⏳ Czas niezsynchronizowany - pomijam checkNewMonth()");
    return;
  }
  
  struct tm* tm_now = localtime(&now);
  int month = tm_now->tm_mon + 1;
  int year = tm_now->tm_year + 1900;
  
  if (currentMonth == -1) {
    currentMonth = month;
    currentYear = year;
    loadTotals();
    loadHistory();
    calculateTotals();
  } else if (month != currentMonth) {
    saveMonthlyBill(currentMonth, currentYear, pennyTotal, aldiTotal, edekaTotal, backereiTotal, sonstigeTotal, grandTotal);
    pennyHistory.clear();
    aldiHistory.clear();
    aldiHistory.clear();
    edekaHistory.clear();
    backereiHistory.clear();
    sonstigeHistory.clear();
    pennyTotal = 0;
    aldiTotal = 0;
    edekaTotal = 0;
    backereiTotal = 0;
    sonstigeTotal = 0;
    grandTotal = 0;
    remainingBudget = monthlyBudget;
    currentMonth = month;
    currentYear = year;
    saveTotals();
  }
}

// ============================================================
// AUTOMATYCZNY BACKUP - implementacja
// ============================================================

// Zwraca dzisiejsza date jako "YYYYMMDD" (8 znakow - celowo bez
// myslnikow, zeby nazwa pliku zmiescila sie w formacie 8.3,
// ktorego niektore karty SD/FAT wymagaja), albo "" jesli czas
// nie jest jeszcze zsynchronizowany z NTP.
String todayDateString() {
  time_t now = time(nullptr);
  if (now < 1000000000) return "";
  struct tm* tm_now = localtime(&now);
  char buf[9];
  snprintf(buf, sizeof(buf), "%04d%02d%02d", tm_now->tm_year + 1900, tm_now->tm_mon + 1, tm_now->tm_mday);
  return String(buf);
}

// Wczytuje date ostatniego backupu (przetrwa restart ESP32)
// Nazwa pliku celowo krotka (8.3-kompatybilna).
void loadBackupMeta() {
  if (SD.exists("/bkupmeta.txt")) {
    File f = SD.open("/bkupmeta.txt", "r");
    if (f) {
      lastBackupDate = f.readStringUntil('\n');
      lastBackupDate.trim();
      f.close();
    }
  }
}

void saveBackupMeta() {
  File f = SD.open("/bkupmeta.txt", "w");
  if (f) {
    f.println(lastBackupDate);
    f.close();
  }
}

// Wczytuje/zapisuje date ostatniego nocnego auto-restartu (przetrwa
// restart ESP32 - inaczej restart o 4:00 moglby sie zapetlic).
void loadRestartMeta() {
  if (SD.exists("/rstmeta.txt")) {
    File f = SD.open("/rstmeta.txt", "r");
    if (f) {
      lastAutoRestartDate = f.readStringUntil('\n');
      lastAutoRestartDate.trim();
      f.close();
    }
  }
}

void saveRestartMeta() {
  File f = SD.open("/rstmeta.txt", "w");
  if (f) {
    f.println(lastAutoRestartDate);
    f.close();
  }
}

// Zwraca kolejny numer dla backupu, gdy czas NTP nie jest jeszcze
// zsynchronizowany (zeby recznemu backupowi NIE przeszkadzal brak
// czasu - liczony licznik trzyma sie w osobnym pliku).
String nextFallbackBackupName() {
  unsigned long counter = 0;
  if (SD.exists("/bkupcnt.txt")) {
    File cf = SD.open("/bkupcnt.txt", "r");
    if (cf) { counter = cf.parseInt(); cf.close(); }
  }
  counter++;
  File cf = SD.open("/bkupcnt.txt", "w");
  if (cf) { cf.println(counter); cf.close(); }
  char buf[9];
  snprintf(buf, sizeof(buf), "B%07lu", counter % 10000000UL);
  return String(buf);
}

// Buduje CAlA tresc backupu (3 sekcje: listy zakupow, biezacy
// Monatliches Budget, historia) jako jeden String. Uzywane zarowno
// do zapisu na SD, jak i do pobrania pliku wprost w przegladarce.
String buildFullBackupCsv() {
  calculateTotals();
  String csv = "";

  csv += "=== LISTY ZAKUPOW ===\n";
  csv += "Sklep;Nazwa;Ilosc;Kupione\n";
  for (int i = 0; i < aldiCount; i++) {
    csv += "Aldi;" + aldiItems[i].name + ";" + String(aldiItems[i].quantity) + ";" + (aldiItems[i].bought ? "1" : "0") + "\n";
  }
  for (int i = 0; i < edekaCount; i++) {
    csv += "Edeka;" + edekaItems[i].name + ";" + String(edekaItems[i].quantity) + ";" + (edekaItems[i].bought ? "1" : "0") + "\n";
  }
  for (int i = 0; i < pennyCount; i++) {
    csv += "Penny;" + pennyItems[i].name + ";" + String(pennyItems[i].quantity) + ";" + (pennyItems[i].bought ? "1" : "0") + "\n";
  }

  csv += "\n=== MONATLICHES BUDGET (biezacy miesiac) ===\n";
  csv += "Monat;Jahr;Aldi (€);Edeka (€);Penny (€);Bäckerei (€);Sonstige (€);Gesamt (€);Budget (€)\n";
  csv += String(currentMonth) + ";" + String(currentYear) + ";" +
         String(aldiTotal, 2) + ";" + String(edekaTotal, 2) + ";" + String(pennyTotal, 2) + ";" +
         String(backereiTotal, 2) + ";" + String(sonstigeTotal, 2) + ";" + String(grandTotal, 2) + ";" +
         String(monthlyBudget, 2) + "\n";

  csv += "\n=== HISTORIA POPRZEDNICH MIESIECY ===\n";
  csv += "Monat;Jahr;Aldi (€);Edeka (€);Penny (€);Bäckerei (€);Sonstige (€);Gesamt (€)\n";
  for (int i = 0; i < billCount; i++) {
    csv += String(billHistory[i].month) + ";" + String(billHistory[i].year) + ";" +
           String(billHistory[i].aldi, 2) + ";" + String(billHistory[i].edeka, 2) + ";" +
           String(billHistory[i].penny, 2) + ";" + String(billHistory[i].backerei, 2) + ";" +
           String(billHistory[i].sonstige, 2) + ";" + String(billHistory[i].total, 2) + "\n";
  }

  return csv;
}

// Tworzy JEDEN plik CSV na karcie SD z kompletem danych: wszystkie
// 3 listy zakupow + biezacy Monatliches Budget + cala historia.
// Zwraca true/false - zeby przycisk BACKUP mogl pokazac PRAWDZIWY
// wynik, a nie zawsze "sukces" (tak jak wczesniej).
bool createFullBackup() {
  String date = todayDateString();
  // Recznemu backupowi brak synchronizacji NTP nie powinien
  // przeszkadzac - wtedy uzywamy numerka zamiast daty w nazwie.
  String filename = (date != "") ? (date + ".csv") : (nextFallbackBackupName() + ".csv");

  if (!SD.exists("/backup")) {
    if (!SD.mkdir("/backup")) {
      Serial.println("BLAD: nie udalo sie utworzyc folderu /backup na karcie SD!");
      return false;
    }
  }

  String path = "/backup/" + filename; // np. /backup/20260912.csv albo /backup/B0000001.csv
  File f = SD.open(path.c_str(), "w");
  if (!f) {
    Serial.println("BLAD: nie udalo sie utworzyc pliku backupu: " + path);
    return false;
  }

  f.print(buildFullBackupCsv());
  f.close();

  if (date != "") {
    lastBackupDate = date;
    saveBackupMeta();
  }
  Serial.println("Backup utworzony: " + path);
  return true;
}

// Sprawdza raz dziennie (albo od razu po starcie, jesli dzis
// jeszcze nie bylo backupu), czy trzeba zrobic nowy backup.
// (To jest backup AUTOMATYCZNY - tu czekanie na NTP ma sens,
// zeby nie robic kilku backupow dziennie przez pomylke).
void checkAutoBackup() {
  String date = todayDateString();
  if (date == "") return; // czas jeszcze nie zsynchronizowany
  if (date != lastBackupDate) {
    createFullBackup();
  }
}

// ============================================================
// STRONA GŁÓWNA (lista zakupów)
// ============================================================
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
 <meta charset="UTF-8">
 <link rel="icon" type="image/png" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Ctext x='0' y='20' font-size='20' fill='white'%3E🐒%3C/text%3E%3C/svg%3E">
 <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
 <meta name="apple-mobile-web-app-capable" content="yes">
 <title>🛒 Einkaufsliste</title>
 <style>
 .drag-handle {
  display: none !important;
}
*{margin:0;padding:0;box-sizing:border-box}
:root{--bg:linear-gradient(135deg,#667eea,#764ba2);--card:#fff;--text:#333}
body.dark{--bg:linear-gradient(135deg,#1a1a2e,#16213e);--card:#2d2d3f;--text:#eee}
body{background:var(--bg);font-family:'Segoe UI',Arial;padding:15px;transition:all 0.3s}
.container{max-width:600px;margin:0 auto;min-height:100vh}
.title-row{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px}
.title-left{display:flex;align-items:center;gap:10px}
h1{color:white;font-size:1.5em;margin:0}
.sd-dot{font-size:1.1em;color:#888;transition:color 0.3s}
.sd-dot.ok{color:#4CAF50;text-shadow:0 0 6px #4CAF50}
.sd-dot.bad{color:#ff4757;text-shadow:0 0 6px #ff4757}
.theme-btn{background:rgba(255,255,255,0.2);border:none;width:44px;height:44px;border-radius:50%;font-size:1.3em;cursor:pointer}
.buttons{display:flex;gap:10px;margin-bottom:20px;justify-content:center}
.bill-btn{background:rgba(255,255,255,0.2);border:none;padding:12px 20px;border-radius:30px;background:#FF9800;font-size:1em;color:white;cursor:pointer}
.shops{display:flex;gap:10px;margin-bottom:20px}
.shop{flex:1;background:rgba(255,255,255,0.2);border-radius:15px;padding:12px;text-align:center;cursor:pointer;color:white;font-weight:bold}
.shop.active{background:rgba(255,255,255,0.4);transform:scale(1.02)}
.categories{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin-bottom:20px}
.cat-btn{background:rgba(255,255,255,0.2);border:none;border-radius:16px;padding:8px 4px;text-align:center;cursor:pointer;color:white;font-size:0.7em}
.cat-btn span{font-size:1.3em;display:block}
.add-section{background:rgba(255,255,255,0.15);border-radius:20px;padding:15px;margin-bottom:20px}
.add-row{display:flex;gap:10px}
.add-row input{flex:1;padding:12px;border-radius:30px;border:none}
.add-row button{background:#fd79a8;color:white;border:none;padding:12px 20px;border-radius:30px;cursor:pointer;font-weight:bold}
.product-list{overflow-y:visible}
.product{background:var(--card);border-radius:15px;padding:8px 10px;margin-bottom:8px;display:flex;align-items:center;justify-content:space-between;flex-wrap:wrap;cursor:grab}
.product:active{cursor:grabbing}
.product.bought{background:#2196f3;color:white}
.product-left{display:flex;align-items:center;gap:8px;flex:2}
.drag-handle{font-size:1.8em;cursor:grab;color:#aaa;padding:4px}
.drag-handle:active{cursor:grabbing}
.check{width:32px;height:32px;border-radius:50%;border:3px solid #00b894;display:flex;align-items:center;justify-content:center;cursor:pointer;background:white;flex-shrink:0}
.check.checked{background:#00b894;color:white}
.product-info{display:flex;flex-direction:column}
.name{font-size:1.1em;font-weight:bold;cursor:pointer;color:var(--text)}
.product.bought .name{color:white}
.quantity-num{font-size:0.8em;color:var(--text);margin-top:3px}
.product.bought .quantity-num{color:white}
.product-right{display:flex;align-items:center;gap:12px}
.quantity-control{display:flex;align-items:center;gap:4px;background:rgba(0,0,0,0.08);border-radius:20px;padding:3px 8px}
.quantity-btn{background:#667eea;color:white;border:none;width:24px;height:24px;border-radius:50%;cursor:pointer;font-size:0.9em;line-height:1}
.quantity-btn.minus{background:#ff4757}
.quantity-num-big{font-weight:bold;min-width:25px;text-align:center;font-size:0.9em;color:var(--text)}
.product.bought .quantity-num-big{color:white}
.delete{background:#ff4757;color:white;border:none;width:36px;height:36px;border-radius:50%;cursor:pointer;font-size:1.1em}
.modal{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.5);backdrop-filter:blur(8px);justify-content:center;align-items:center}
.modal-content{background:var(--card);border-radius:30px;padding:25px;width:90%;max-width:400px}
.modal-content h3{color:var(--text);text-align:center}
.modal-content button{background:#4CAF50;color:white;border:none;padding:12px;border-radius:25px;width:100%;margin-top:8px}
.product-list-modal{max-height:300px;overflow-y:auto}
.product-item{padding:12px;border-bottom:1px solid rgba(0,0,0,0.1);cursor:pointer;color:var(--text)}
.footer{text-align:center;color:white;margin-top:20px;font-size:0.7em}
</style>
</head>
<body>
<div class="container">
<div class="title-row">
<div class="title-left">
<h1>🛒 Einkaufsliste</h1>
<span id="sdDot" class="sd-dot" title="Karta SD: sprawdzam...">●</span>
</div>
<a href="/" class="bill-btn" style="background:#9C27B0;text-decoration:none;display:inline-block">🏠 MENU</a>
<button class="theme-btn" onclick="toggleTheme()">🌙</button>
</div>
<div class="buttons">
<button class="bill-btn" onclick="window.open('/rechnung', '_blank')">🧾 RECHNUNG</button>
<button class="bill-btn" onclick="exportShoppingList()" style="background:#2196F3">📥 EXPORT</button>
<button class="bill-btn" onclick="importShoppingList()" style="background:#4CAF50">📤 IMPORT</button>
<input type="file" id="importFile" accept=".csv" style="display:none" onchange="doImport()">
</div>
<div class="shops">
<div class="shop" id="shopAldi" onclick="selectShop('aldi')">🔵 Aldi</div>
<div class="shop" id="shopEdeka" onclick="selectShop('edeka')">🟢 Edeka</div>
<div class="shop" id="shopPenny" onclick="selectShop('penny')">🟡 Penny</div>
</div>
<div class="categories">
<div class="cat-btn" onclick="showCategory('gemuse')"><span>🥬</span>Gemüse</div>
<div class="cat-btn" onclick="showCategory('obst')"><span>🍎</span>Obst</div>
<div class="cat-btn" onclick="showCategory('brot')"><span>🥖</span>Brot</div>
<div class="cat-btn" onclick="showCategory('milch')"><span>🥛</span>Milch</div>
<div class="cat-btn" onclick="showCategory('fleisch')"><span>🥩</span>Fleisch</div>
<div class="cat-btn" onclick="showCategory('marmelade')"><span>🧉</span>Marmelade</div>
<div class="cat-btn" onclick="showCategory('kekse')"><span>🍪</span>Kekse</div>
<div class="cat-btn" onclick="showCategory('nudeln')"><span>🍜</span>Nudeln</div>
<div class="cat-btn" onclick="showCategory('hülsenfrüchte')"><span>🫘</span>Hülsenfrüchte</div>
<div class="cat-btn" onclick="showCategory('fertig')"><span>🥰</span>Fertiges Essen</div>
<div class="cat-btn" onclick="showCategory('pulver')"><span>🥗</span>Gewürze</div>
<div class="cat-btn" onclick="showCategory('haus')"><span>🧴</span>Haushalt</div>
<div class="cat-btn" onclick="showCategory('kase')"><span>🧀</span>Käse</div>
<div class="cat-btn" onclick="showCategory('vegan')"><span>🌱</span>Vegan</div>
<div class="cat-btn" onclick="showCategory('sonstige')"><span>💫</span>sonstige</div>
</div>
<div class="add-section">
<div class="add-row">
<input type="text" id="itemName" placeholder="Produktname" autocomplete="off">
<button onclick="addToCurrentShop()">➕</button>
</div>
</div>
<div class="product-list" id="list"></div>
<div class="footer">✨ Tippe auf Produkt = gekauft | ☰ = ziehen zum Sortieren ✨</div>
</div>
<div id="productModal" class="modal" onclick="closeModal()">
<div class="modal-content" onclick="event.stopPropagation()">
<h3 id="modalTitle">Produkte</h3>
<div id="modalList" class="product-list-modal"></div>
<button onclick="closeModal()">Schließen</button>
</div>
</div>
<script>
let currentShop = "penny";
let isDark = localStorage.getItem('theme') === 'dark';   
let products = [];
let dragSrc = null;

const produkte = {
  gemuse: [{name:"Gurke",emoji:"🥒"},{name:"Tomate",emoji:"🍅"},{name:"Grüne Bohnen, gefroren",emoji:"🫛"},{name:"Salat",emoji:"🥬"},{name:"Karotte",emoji:"🥕"},{name:"Kartoffel",emoji:"🥔"},{name:"Zwiebel",emoji:"🧅"},{name:"Suppengrün",emoji:"🥬"},{name:"Grüne Zwiebel",emoji:"🪴"},{name:"Paprika",emoji:"🫑"},{name:"Brokkoli",emoji:"🥦"},{name:"Blumenkohl",emoji:"🥦"},{name:"Kohl",emoji:"🥬"},{name:"Zucchini",emoji:"🥒"},{name:"Minitomaten",emoji:"🍅"}],
  obst: [{name:"Apfel",emoji:"🍎"},{name:"Banane",emoji:"🍌"},{name:"Erdbeere",emoji:"🍓"},{name:"Orange",emoji:"🍊"},{name:"Zitrone",emoji:"🍋"},{name:"Trauben",emoji:"🍇"},{name:"Wassermelone",emoji:"🍉"},{name:"Kiwi",emoji:"🥝"},{name:"Birne",emoji:"🍐"},{name:"Rosinen",emoji:"🍇"},{name:"Kirschen",emoji:"🍒"}],
  brot: [{name:"Weißbrot",emoji:"🍞"},{name:"Roggenbrot",emoji:"🍞"},{name:"Brötchen",emoji:"🥖"},{name:"Baguette",emoji:"🥖"},{name:"Hörnchen",emoji:"🥐"},{name:"Brezel",emoji:"🥨"},{name:"Toast",emoji:"🍞"},{name:"Knäckebrot",emoji:"🍞"}],
  milch: [{name:"Milch",emoji:"🥛"},{name:"Milchreis",emoji:"🍚"},{name:"Hafermilch",emoji:"🥛"},{name:"Joghurt",emoji:"🥤"},{name:"Quark",emoji:"🥄"},{name:"Butter",emoji:"🧈"},{name:"Sahne",emoji:"🥛"},{name:"Kefir",emoji:"🥛"},{name:"Buttermilch",emoji:"🥛"},{name:"Eier",emoji:"🥚"},{name:"Sojasahne",emoji:"🥛"},{name:"Körniger Käse",emoji:"🍛"},{name:"Frischkäse",emoji:"🍚"}],
  fleisch: [{name:"Schweinebraten",emoji:"🥩"},{name:"Kotelett",emoji:"🥩"},{name:"Schulter",emoji:"🥩"},{name:"Gulasch",emoji:"🥩"},{name:"Hähnchenbrust",emoji:"🍗"},{name:"Hähnchen",emoji:"🍗"},{name:"Rindfleisch",emoji:"🥩"},{name:"Hackfleisch",emoji:"🥩"},{name:"Wurst",emoji:"🌭"},{name:"Schinken",emoji:"🥩"},{name:"Speck",emoji:"🥓"},{name:"Frikadellen",emoji:"🫓"},{name:"Vegi Wurst",emoji:"🥩"}],
  marmelade: [{name:"Erdnussbutte",emoji:"🥜"},{name:"Erdbeer marmelade",emoji:"🍓"},{name:"Heidelbeeren",emoji:"🫐"},{name:"Himbeermarmelade",emoji:"🍓"},{name:"Aprikosenmarmelade",emoji:"🍑"}],
  kekse: [{name:"Lemon Kekse",emoji:"🥮"},{name:"Cornflakes",emoji:"🥣"},{name:"Haferflocken",emoji:"🌾"}],
  nudeln: [{name:"Spaghetti",emoji:"🍝"},{name:"Nudeln",emoji:"🍜"},{name:"Reis",emoji:"🍚"},{name:"Lasagneblat",emoji:"📁"}],
  hülsenfrüchte: [{name:"Bonnen",emoji:"🫘"},{name:"Chiasamen",emoji:"𓇢"},{name:"Erbsen",emoji:"🫛"},{name:"Kichererbsen",emoji:"🫛"},{name:"Meis",emoji:"🌽"},{name:"Linsen",emoji:"🍲"}],
  fertig: [{name:"Pizza",emoji:"🍕"},{name:"Frikadelen",emoji:"🫓"},{name:"Chicken Nuggets",emoji:"🍗"},{name:"Fisch",emoji:"🐟"},{name:"Schnitzel",emoji:"🥩"},{name:"Leberkäs",emoji:"🧇"}],
  pulver: [{name:"Ketchup",emoji:"🥫🍅"},{name:"Gebratene nudeln Tüte",emoji:"🍝"},{name:"Yam yam Gewürz",emoji:"🥗"},{name:"Chili Tüte",emoji:"🌶"},{name:"Mayonnaise",emoji:"🍶"},{name:"Pfeffer",emoji:"🧂"},{name:"Tomatensoße (die 3 kleinen Packungen)",emoji:"🍅"},{name:"Salz",emoji:"🧂"},{name:"Zucker",emoji:"⬜"},{name:"Puderzucker",emoji:"💭"},{name:"Paprika",emoji:"🌶️"},{name:"Senft",emoji:"🌭"},{name:"Petersilie",emoji:"🌿"},{name:"Dill",emoji:"🪴"},{name:"Italia Kräuter",emoji:"🤌"},{name:"Zimt",emoji:"🪵"},{name:"Pomes Salz",emoji:"🧂"},{name:"Gemüse Brühe",emoji:"♨️"},{name:"Knoblauch",emoji:"🧄"},{name:"Maggi",emoji:"🪄"}],
  haus: [{name:"Toaletenpapier",emoji:"🧻"},{name:"Mundwasser",emoji:"💧"},{name:"Feuchte Tücher(Toilette)",emoji:"🌬️"}, {name:"Lenor",emoji:"🧴"},{name:"Papiertücher",emoji:"🧻"},{name:"Seife",emoji:"🧼"},{name:"Shampoo",emoji:"🧴"},{name:"Spülmittel",emoji:"🧴"},{name:"Waschpulver",emoji:"🧴"},{name:"Müllbeutel",emoji:"🗑️"}],
  kase: [{name:"Käse",emoji:"🧀"},{name:"Käse Gouda",emoji:"🧀"},{name:"Käse Block",emoji:"🧀"},{name:"Frischkäse",emoji:"🧀"},{name:"Käse geriben",emoji:"🧀"},{name:"Cheddar",emoji:"🧀"},{name:"Feta",emoji:"🧀"},{name:"Mozzarella",emoji:"🧀"},{name:"Parmesan",emoji:"🧀"},{name:"Schmelzkäse",emoji:"🧀"}],
  vegan: [{name:"Tofu",emoji:"🌱"},{name:"Vegi Wurst",emoji:"🌱"},{name:"Chunks",emoji:"🌱"},{name:"Tofu",emoji:"🧈"},{name:"Tortillas groß (Burito groß)",emoji:"🌮"},{name:"Hack (Vegetarisch)",emoji:"🌱"},{name:"Sojamilch",emoji:"🌱"},{name:"Seitan",emoji:"🌱"},{name:"Falaffel",emoji:"🌱"},{name:"Tempeh",emoji:"🌱"},{name:"Sojasoße",emoji:"🌱"},{name:"Sojajoghurt",emoji:"🥛"},{name:"Tahini",emoji:"🥙"},{name:"Noriblätter (Sushi)",emoji:"🥢"},{name:"Veganer Käse",emoji:"🌱"},{name:"Veganer Joghurt",emoji:"🌱"}],
  sonstige: [{name:"Taback",emoji:"🚬"},{name:"Adam Tee",emoji:"😎"},{name:"Steam",emoji:"💲"},{name:"Kaffee",emoji:"🍵"}]
};

function setTheme() { if(isDark) document.body.classList.add('dark'); else document.body.classList.remove('dark'); }
function toggleTheme() { isDark = !isDark; localStorage.setItem('theme', isDark ? 'dark' : 'light'); setTheme(); }
setTheme();

function selectShop(shop) {
  currentShop = shop;
  document.getElementById('shopPenny').classList.remove('active');
  document.getElementById('shopAldi').classList.remove('active');
  document.getElementById('shopEdeka').classList.remove('active');
  let id = 'shop' + shop.charAt(0).toUpperCase() + shop.slice(1);
  document.getElementById(id).classList.add('active');
  loadProducts();
}

function showCategory(category) {
  let productList = produkte[category];
  let title = "";
  switch(category) {
    case "gemuse": title = "🥬 Gemüse 🥬"; break;
    case "obst": title = "🍎 Obst 🍎"; break;
    case "brot": title = "🥖 Brot 🥖"; break;
    case "milch": title = "🥛 Milch 🥛"; break;
    case "fleisch": title = "🥩 Fleisch 🥩"; break;
    case "marmelade": title = "🧉 Marmelade 🧉"; break;
    case "kekse": title = "🍪 Kekse 🍪"; break;
    case "nudeln": title = "🍜 Nudeln 🍜"; break;
    case "hülsenfrüchte": title = "🫘 Hülsenfrüchte 🫘"; break;
    case "fertig": title = "🥰 Fertiges Essen 🥰"; break;
    case "pulver": title = "🥗 Gewürze 🥗"; break;
    case "haus": title = "🧴 Haushalt 🧴"; break;
    case "kase": title = "🧀 Käse 🧀"; break;
    case "vegan": title = "🌱 Vegan 🌱"; break;
    case "sonstige": title = "💫 sonstige 💫"; break;
    default: title = category;
  }
  let modalList = document.getElementById('modalList');
  modalList.innerHTML = '';
  productList.forEach(p => { modalList.innerHTML += `<div class="product-item" onclick="addProductFromModal('${p.emoji} ${p.name}')">${p.emoji} ${p.name}</div>`; });
  document.getElementById('modalTitle').innerHTML = title;
  document.getElementById('productModal').style.display = 'flex';
}

function addProductFromModal(productName) {
  let input = document.getElementById('itemName');
  if(input.value) input.value = productName + ', ' + input.value;
  else input.value = productName;
  closeModal();
  input.focus();
}
function closeModal() { document.getElementById('productModal').style.display = 'none'; }

function loadProducts() { 
  fetch('/list?shop=' + currentShop)
    .then(r => r.json())
    .then(data => { products = data; renderList(); });
}

function dragStart(e) {
  dragSrc = this;
  e.dataTransfer.setData('text/plain', this.getAttribute('data-id'));
  this.style.opacity = '0.5';
}
function dragEnd(e) { this.style.opacity = ''; }
function dragOver(e) { e.preventDefault(); }
function dragDrop(e) {
  e.preventDefault();
  let fromId = parseInt(dragSrc.getAttribute('data-id'));
  let toId = parseInt(this.getAttribute('data-id'));
  if(fromId !== toId) {
    fetch('/move?shop=' + currentShop + '&from=' + fromId + '&to=' + toId)
      .then(() => loadProducts());
  }
  this.style.opacity = '';
}

function renderList() {
  const container = document.getElementById('list');
  container.innerHTML = '';
  if(products.length === 0) {
    container.innerHTML = '<div style="text-align:center;color:white;padding:30px">📭 Keine Produkte</div>';
    return;

  }
  for(let i = 0; i < products.length; i++) {
    let p = products[i];
    let checkClass = p.bought ? 'checked' : '';
    let checkMark = p.bought ? '✓' : '';
    let boughtClass = p.bought ? 'bought' : '';
    
    let div = document.createElement('div');
    div.className = `product ${boughtClass}`;
    div.setAttribute('draggable', 'true');
    div.setAttribute('data-id', p.id);
    div.setAttribute('data-index', i);
    
    div.innerHTML = `
  <div class="product-left">
    <div class="drag-handle" draggable="false">☰</div>
    <div class="check ${checkClass}" onclick="toggleBought(${p.id})">${checkMark}</div>
    <div class="product-info">
      <div class="name" onclick="toggleBought(${p.id})">${escapeHtml(p.name)}</div>
      <div class="quantity-num">${p.qty} St.</div>
    </div>
  </div>
  <div class="product-right">
    <div class="quantity-control">
      <button class="quantity-btn minus" onclick="event.stopPropagation(); changeQuantity(${p.id}, -1)">-</button>
      <span class="quantity-num-big">${p.qty}</span>
      <button class="quantity-btn" onclick="event.stopPropagation(); changeQuantity(${p.id}, 1)">+</button>
    </div>
    <button class="delete" onclick="event.stopPropagation(); deleteProduct(${p.id})">🗑️</button>
  </div>
`;
    div.addEventListener('dragstart', dragStart);
    div.addEventListener('dragend', dragEnd);
    div.addEventListener('dragover', dragOver);
    div.addEventListener('drop', dragDrop);
    container.appendChild(div);
  }
}

function changeQuantity(id, delta) { fetch('/quantity?shop=' + currentShop + '&id=' + id + '&delta=' + delta).then(() => loadProducts()); }
function toggleBought(id) { fetch('/toggle?shop=' + currentShop + '&id=' + id).then(() => loadProducts()); }
function addToCurrentShop() {
  let name = document.getElementById('itemName').value;
  if(!name) { alert('Bitte Name eingeben!'); return; }
  fetch('/add?shop=' + currentShop + '&name=' + encodeURIComponent(name)).then(() => { loadProducts(); document.getElementById('itemName').value = ''; });
}
function deleteProduct(id) { if(confirm('Löschen?')) fetch('/delete?shop=' + currentShop + '&id=' + id).then(() => loadProducts()); }
function escapeHtml(text) { if(!text) return ''; return text.replace(/[&<>]/g, function(m) { if(m === '&') return '&amp;'; if(m === '<') return '&lt;'; if(m === '>') return '&gt;'; return m; }); }

selectShop('penny');

// Sprawdza co jakis czas, czy karta SD faktycznie odpowiada, i
// pokazuje to jako zielona/czerwona kropke obok tytulu.
function checkSdStatus() {
  fetch('/sdstatus').then(r => r.json()).then(d => {
    const dot = document.getElementById('sdDot');
    if (d.ok) {
      dot.classList.add('ok'); dot.classList.remove('bad');
      dot.title = 'Karta SD: OK';
    } else {
      dot.classList.add('bad'); dot.classList.remove('ok');
      dot.title = 'Karta SD: BŁĄD - sprawdz karte!';
    }
  }).catch(() => {
    const dot = document.getElementById('sdDot');
    dot.classList.add('bad'); dot.classList.remove('ok');
    dot.title = 'Karta SD: brak polaczenia z ESP32';
  });
}
checkSdStatus();
setInterval(checkSdStatus, 10000);

function exportShoppingList() { let shop = currentShop; fetch('/exportshop?shop=' + shop).then(r => r.text()).then(data => { const blob = new Blob([data], {type: 'text/csv'}); const link = document.createElement('a'); link.href = URL.createObjectURL(blob); link.download = shop + '_einkaufsliste.csv'; link.click(); alert('✅ Eksportowano ' + shop); }); } function importShoppingList() { document.getElementById('importFile').click(); } function doImport() { let file = document.getElementById('importFile').files[0]; if (!file) return; let reader = new FileReader(); reader.onload = function(e) { let data = e.target.result; fetch('/importshop?shop=' + currentShop, { method: 'POST', headers: {'Content-Type': 'text/csv'}, body: data }).then(() => { alert('✅ Importowano!'); loadProducts(); }); }; reader.readAsText(file); }
</script>
</body>
</html>
  )rawliteral";
  server.send(200, "text/html", html);
}

// ============================================================
// STRONA RECHNUNG (z budżetem i sonstige)
// ============================================================
void handleRechnung() {
  calculateTotals();
  
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>🧾 Monatlich</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{--bg:linear-gradient(135deg,#667eea,#764ba2);--card:#fff;--text:#333}
body.dark{--bg:linear-gradient(135deg,#1a1a2e,#16213e);--card:#2d2d3f;--text:#eee}
body{background:var(--bg);font-family:'Segoe UI',Arial;padding:20px;min-height:100vh;transition:all 0.3s}
.container{max-width:500px;margin:0 auto}
.header{display:flex;justify-content:center;align-items:center;margin-bottom:20px}
h1{color:white;font-size:1.5em}
.btn-group{display:flex;gap:10px;flex-wrap:wrap;justify-content:center;width:100%}
.theme-btn,.export-btn,.import-btn{background:rgba(255,255,255,0.2);border:none;width:44px;height:44px;border-radius:50%;font-size:1.3em;cursor:pointer}
.export-btn{width:auto;padding:0 15px;border-radius:30px;background:#2196F3;font-size:0.9em;color:white}
.import-btn{width:auto;padding:0 15px;border-radius:30px;background:#4CAF50;font-size:0.9em;color:white}
.budget-card{background:rgba(255,255,255,0.15);border-radius:20px;padding:20px;margin-bottom:20px;text-align:center}
.budget-title{font-size:1.2em;font-weight:bold;margin-bottom:15px;color:#FF9800}
.budget-row{display:flex;gap:10px;justify-content:center;align-items:center;flex-wrap:wrap;margin-bottom:10px}
.budget-row input{width:120px;padding:12px;border-radius:30px;font-size:1em;border:none;background:white;color:#333;text-align:center}
.budget-row button{background:#FF9800;color:white;border:none;padding:12px 20px;border-radius:30px;cursor:pointer;font-weight:bold}
.budget-info{font-size:1.1em;margin-top:10px;color:#ddd}
.budget-info strong{color:white}
.budget-remaining{font-size:1.5em;font-weight:bold;color:#4CAF50}
.budget-remaining.negative{color:#ff4757}
.paragon-card{background:rgba(255,255,255,0.15);border-radius:20px;padding:20px;margin-bottom:20px}
.paragon-title{font-size:1.2em;font-weight:bold;margin-bottom:15px;color:white}
.paragon-row{display:flex;gap:10px;flex-wrap:wrap}
.paragon-row input{flex:2;padding:12px;border-radius:30px;font-size:1em;border:none;background:white;color:#333}
.paragon-row button{background:#4CAF50;color:white;border:none;padding:12px 20px;border-radius:30px;cursor:pointer;font-weight:bold}
.paragon-total{display:flex;justify-content:space-between;align-items:center;margin-top:10px;color:white;font-weight:bold;flex-wrap:wrap}
.paragon-total span span{color:#FF9800;font-size:1.3em}
.undo-btn{background:#ff9800;color:white;border:none;padding:6px 12px;border-radius:30px;cursor:pointer;font-weight:bold;font-size:0.8em}
.summary-card{background:rgba(255,255,255,0.15);border-radius:20px;padding:20px;margin-bottom:20px}
.summary-item{display:flex;justify-content:space-between;padding:10px 0;border-bottom:1px solid rgba(255,255,255,0.2);color:white;font-size:1.1em}
.summary-total{display:flex;justify-content:space-between;padding:15px 0;font-weight:bold;font-size:1.3em;color:#FF9800}
.summary-item .amount,.summary-total .amount{display:inline-block;min-width:100px;text-align:right}
.history-card{background:rgba(255,255,255,0.15);border-radius:20px;padding:20px}
.history-title{font-size:1.1em;font-weight:bold;margin-bottom:15px;color:white}
.history-item{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid rgba(255,255,255,0.1);color:white}
.backup-list-card{background:rgba(255,255,255,0.15);border-radius:20px;padding:20px;margin-top:20px}
.backup-status-line{color:#ddd;margin-bottom:12px;font-size:0.9em}
.backup-item{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:8px 0;border-bottom:1px solid rgba(255,255,255,0.1);color:white}
.backup-item button{background:#ff4757;color:white;border:none;width:34px;height:34px;border-radius:50%;cursor:pointer;font-size:0.9em;flex-shrink:0}
.back-btn{display:block;text-align:center;margin-top:20px;color:white;text-decoration:none;background:#2196F3;padding:12px;border-radius:30px}
body.dark .paragon-row input{background:#333;color:white}
body.dark .budget-row input{background:#333;color:white}
</style>
</head>
<body>
<div class="container">
<div class="header">
<div class="btn-group">
<button class="export-btn" onclick="exportData()">📥 EXPORT</button>
<button class="import-btn" onclick="importData()">📤 IMPORT</button>
<button class="import-btn" style="background:#9c27b0" onclick="backupNow()">💾 BACKUP</button>
<button class="import-btn" style="background:#009688" onclick="downloadBackup()">⬇️ POBIERZ</button>
<button class="import-btn" style="background:#e91e63" onclick="restoreBackup()">♻️ PRZYWRÓĆ</button>
<input type="file" id="restoreFile" accept=".csv" style="display:none" onchange="doRestoreBackup()">
<button class="theme-btn" onclick="toggleTheme()">🌙</button>
</div>
</div>

<!-- BUDGET CARD -->
<div class="budget-card">
<div class="budget-title">💰 MONATLICHES BUDGET</div>
<div class="budget-row">
<input type="number" id="budgetAmount" step="10" placeholder="Budget (€)">
<button onclick="setBudget()">💾 SPEICHERN</button>
</div>
<div class="budget-info">
<div>Monatsbudget: <strong id="budgetValue">0.00</strong> €</div>
<div>Ausgegeben: <strong id="spentValue">0.00</strong> €</div>
<div class="budget-remaining" id="remainingValue">0.00 € übrig</div>
</div>
</div>

<div class="paragon-card">
<div class="paragon-title">🔵 ALDI </div>
<div class="paragon-row">
<input type="number" id="aldiAmount" step="0.01" placeholder="Summe (€)">
<button onclick="addParagon('aldi')">➕ HINZUFÜGEN</button>
</div>
<div class="paragon-total">
<span>Aktuell: <span id="aldiTotal">0.00</span> €</span>
<button class="undo-btn" onclick="undoParagon('aldi')">↩️ Cofnij</button>
</div>
</div>

<div class="paragon-card">
<div class="paragon-title">🟢 EDEKA </div>
<div class="paragon-row">
<input type="number" id="edekaAmount" step="0.01" placeholder="Summe (€)">
<button onclick="addParagon('edeka')">➕ HINZUFÜGEN</button>
</div>
<div class="paragon-total">
<span>Aktuell: <span id="edekaTotal">0.00</span> €</span>
<button class="undo-btn" onclick="undoParagon('edeka')">↩️ Cofnij</button>
</div>
</div>

<div class="paragon-card">
<div class="paragon-title">🟡 PENNY </div>
<div class="paragon-row">
<input type="number" id="pennyAmount" step="0.01" placeholder="Summe (€)">
<button onclick="addParagon('penny')">➕ HINZUFÜGEN</button>
</div>
<div class="paragon-total">
<span>Aktuell: <span id="pennyTotal">0.00</span> €</span>
<button class="undo-btn" onclick="undoParagon('penny')">↩️ Cofnij</button>
</div>
</div>

<div class="paragon-card">
<div class="paragon-title">🥨 BÄCKEREI </div>
<div class="paragon-row">
<input type="number" id="backereiAmount" step="0.01" placeholder="Summe (€)">
<button onclick="addParagon('backerei')">➕ HINZUFÜGEN</button>
</div>
<div class="paragon-total">
<span>Aktuell: <span id="backereiTotal">0.00</span> €</span>
<button class="undo-btn" onclick="undoParagon('backerei')">↩️ Cofnij</button>
</div>
</div>

<div class="paragon-card">
<div class="paragon-title">🟤 NORMA-NETTO </div>
<div class="paragon-row">
<input type="number" id="sonstigeAmount" step="0.01" placeholder="Summe (€)">
<button onclick="addParagon('sonstige')">➕ HINZUFÜGEN</button>
</div>
<div class="paragon-total">
<span>Aktuell: <span id="sonstigeTotal">0.00</span> €</span>
<button class="undo-btn" onclick="undoParagon('sonstige')">↩️ Cofnij</button>
</div>
</div>

<div class="summary-card">
<div class="summary-item"><span>🔵 Aldi</span><span class="amount"><span id="sumAldi">0.00</span> €</span></div>
<div class="summary-item"><span>🟢 Edeka</span><span class="amount"><span id="sumEdeka">0.00</span> €</span></div>
<div class="summary-item"><span>🟡 Penny</span><span class="amount"><span id="sumPenny">0.00</span> €</span></div>
<div class="summary-item"><span>🥨 Bäckerei</span><span class="amount"><span id="sumBackerei">0.00</span> €</span></div>
<div class="summary-item"><span>🟤 Norma-Netto</span><span class="amount"><span id="sumSonstige">0.00</span> €</span></div>
<div class="summary-total"><span>💰 GESAMT</span><span class="amount"><span id="sumTotal">0.00</span> €</span></div>
</div>

<div class="history-card">
<div class="history-title">📜 VORHERIGE MONATE</div>
<div id="historyList"></div>
</div>

<div class="backup-list-card">
<div class="history-title">📦 KOPIE ZAPASOWE NA KARCIE SD</div>
<div class="backup-status-line" id="backupStatusLine">Sprawdzam...</div>
<div id="backupList"></div>
</div>

<a href="/einkaufsliste" class="back-btn">← ZURÜCK ZUR LISTE</a>
</div>

<script>
let isDark = localStorage.getItem('theme') === 'dark';
function setTheme() { if(isDark) document.body.classList.add('dark'); else document.body.classList.remove('dark'); }
function toggleTheme() { isDark = !isDark; localStorage.setItem('theme', isDark ? 'dark' : 'light'); setTheme(); }
setTheme();

function addParagon(shop) {
  let amount = parseFloat(document.getElementById(shop + 'Amount').value);
  if(isNaN(amount) || amount <= 0) { alert('Bitte gültige Summe eingeben!'); return; }
  fetch('/paragon?shop=' + shop + '&amount=' + amount).then(() => { 
    document.getElementById(shop + 'Amount').value = ''; 
    loadAll(); 
  });
}

function undoParagon(shop) {
  fetch('/undo?shop=' + shop).then(() => { loadAll(); });
}

function setBudget() {
  let amount = parseFloat(document.getElementById('budgetAmount').value);
  if(isNaN(amount) || amount <= 0) { alert('Bitte gültigen Betrag eingeben!'); return; }
  fetch('/budget?amount=' + amount).then(() => { 
    document.getElementById('budgetAmount').value = ''; 
    loadAll(); 
  });
}

function backupNow() {
  fetch('/backupnow')
    .then(r => {
      if (r.ok) { alert('✅ Backup zapisany na karcie SD!'); loadBackupList(); }
      else alert('❌ Backup NIE powiodl sie - sprawdz karte SD (pelna/wyjeta?) i Serial Monitor');
    })
    .catch(() => alert('❌ Nie udalo sie polaczyc z ESP32 - backup NIE zostal zrobiony'));
}

// Zamienia nazwe pliku backupu na czytelna forme, np.
// "20260913.csv" -> "13.09.2026", a "B0000001.csv" (backup zrobiony
// zanim ESP32 zdazyl zsynchronizowac czas) zostawia jak jest.
function formatBackupName(name) {
  let base = name.replace('.csv', '');
  if (/^\d{8}$/.test(base)) {
    return base.substring(6, 8) + '.' + base.substring(4, 6) + '.' + base.substring(0, 4);
  }
  return base + ' (bez daty)';
}

// Pokazuje liste wszystkich backupow zapisanych na karcie SD, wraz
// z data ostatniego, i pozwala skasowac kazdy z osobna.
function loadBackupList() {
  fetch('/backuplist').then(r => r.json()).then(list => {
    const el = document.getElementById('backupList');
    const statusLine = document.getElementById('backupStatusLine');
    if (!list || list.length === 0) {
      el.innerHTML = '';
      statusLine.innerHTML = '⚠️ Jeszcze nie zrobiono żadnego backupu';
      return;
    }
    list.sort((a, b) => a.name < b.name ? 1 : -1);
    statusLine.innerHTML = '✅ Ostatni backup: <strong>' + formatBackupName(list[0].name) + '</strong> (' + list.length + ' kopii na karcie)';
    let html = '';
    list.forEach(b => {
      html += `<div class="backup-item"><span>🗂️ ${formatBackupName(b.name)}</span><button onclick="deleteBackup('${b.name}')" title="Usuń ten backup">🗑️</button></div>`;
    });
    el.innerHTML = html;
  }).catch(() => {
    document.getElementById('backupStatusLine').innerHTML = '❌ Nie można odczytać listy backupów z karty SD';
  });
}

function deleteBackup(name) {
  if (!confirm('Usunąć backup ' + formatBackupName(name) + '?')) return;
  fetch('/deletebackup?name=' + encodeURIComponent(name))
    .then(r => { if (r.ok) loadBackupList(); else alert('❌ Nie udało się usunąć tego backupu'); })
    .catch(() => alert('❌ Nie udalo sie polaczyc z ESP32'));
}

// Pobiera aktualny backup jako plik NA TELEFON/KOMPUTER (poza karta SD).
// To warto robic od czasu do czasu - gdyby zgubila/uszkodzila sie
// sama karta SD, kopia na SD by nic nie dala.
function downloadBackup() {
  fetch('/downloadbackup')
    .then(r => r.text())
    .then(data => {
      const blob = new Blob([data], {type: 'text/csv'});
      const link = document.createElement('a');
      link.href = URL.createObjectURL(blob);
      link.download = 'einkaufsliste_backup.csv';
      link.click();
      alert('✅ Backup pobrany! Zapisz go gdzies bezpiecznie (np. Dysk Google/komputer).');
    });
}

// Przywraca WSZYSTKO (listy + budzet + historia) z wczesniej
// pobranego pliku backupu - np. po wymianie ESP32 i/lub karty SD.
function restoreBackup() {
  if (!confirm('To NADPISZE wszystkie obecne listy zakupow, budzet i historie danymi z wybranego pliku backupu. Kontynuowac?')) return;
  document.getElementById('restoreFile').click();
}
function doRestoreBackup() {
  let file = document.getElementById('restoreFile').files[0];
  if (!file) return;
  let reader = new FileReader();
  reader.onload = function(e) {
    fetch('/restorebackup', {
      method: 'POST',
      headers: {'Content-Type': 'text/csv'},
      body: e.target.result
    }).then(r => {
      if (r.ok) { alert('✅ Przywrocono z backupu!'); loadAll(); }
      else { alert('❌ Nie udalo sie przywrocic - czy to na pewno plik backupu z tego programu?'); }
    });
    document.getElementById('restoreFile').value = '';
  };
  reader.readAsText(file);
}

function exportData() {
  fetch('/exportdata')
    .then(r => r.text())
    .then(data => {
      const blob = new Blob([data], {type: 'text/csv'});
      const link = document.createElement('a');
      link.href = URL.createObjectURL(blob);
      link.download = 'backup.csv';
      link.click();
      alert('✅ Daten exportiert!');
    });
}

function importData() {
  let input = document.createElement('input');
  input.type = 'file';
  input.accept = '.csv';
  input.onchange = e => {
    let file = e.target.files[0];
    let reader = new FileReader();
    reader.onload = event => {
      let data = event.target.result;
      fetch('/importdata', {
        method: 'POST',
        headers: {'Content-Type': 'text/csv'},
        body: data
      }).then(() => {
        alert('✅ Daten importiert!');
        loadAll();
      });
    };
    reader.readAsText(file);
  };
  input.click();
}

function loadAll() {
  fetch('/totals').then(r => r.json()).then(data => {
    document.getElementById('aldiTotal').innerHTML = data.aldi.toFixed(2);
    document.getElementById('edekaTotal').innerHTML = data.edeka.toFixed(2);
    document.getElementById('pennyTotal').innerHTML = data.penny.toFixed(2);
    document.getElementById('backereiTotal').innerHTML = data.backerei.toFixed(2);
    document.getElementById('sonstigeTotal').innerHTML = data.sonstige.toFixed(2);
    document.getElementById('sumAldi').innerHTML = data.aldi.toFixed(2);
    document.getElementById('sumEdeka').innerHTML = data.edeka.toFixed(2);
    document.getElementById('sumPenny').innerHTML = data.penny.toFixed(2);
    document.getElementById('sumBackerei').innerHTML = data.backerei.toFixed(2);
    document.getElementById('sumSonstige').innerHTML = data.sonstige.toFixed(2);
    document.getElementById('sumTotal').innerHTML = data.total.toFixed(2);
    document.getElementById('budgetValue').innerHTML = data.budget.toFixed(2);
    document.getElementById('spentValue').innerHTML = data.total.toFixed(2);
    let remaining = data.budget - data.total;
    let remainingElem = document.getElementById('remainingValue');
    remainingElem.innerHTML = remaining.toFixed(2) + ' € übrig';
    if(remaining < 0) {
      remainingElem.style.color = '#ff4757';
      remainingElem.innerHTML = Math.abs(remaining).toFixed(2) + ' € über dem Budget';
    } else {
      remainingElem.style.color = '#4CAF50';
    }
  });
  fetch('/bill').then(r => r.json()).then(data => {
    let html = '';
    if(data.history && data.history.length > 0) {
      data.history.forEach(h => {
        html += `<div class="history-item"><span>${h.month}.${h.year}</span><span>${h.total.toFixed(2)} €</span></div>`;
      });
    } else {
      html = '<div class="history-item">Keine Vormonate</div>';
    }
    document.getElementById('historyList').innerHTML = html;
  });
}
loadAll();
loadBackupList();
setInterval(loadAll, 5000);
setInterval(loadBackupList, 20000);
</script>
</body>
</html>
  )rawliteral";
  server.send(200, "text/html", html);
}

// ============================================================
// API
// ============================================================

// Sprawdza, czy karta SD naprawde odpowiada (nie tylko czy sie
// kiedys zainicjowala przy starcie) - probuje otworzyc katalog
// glowny. Uzywane przez zielona/czerwona kropke na stronie glownej.
bool isSdOk() {
  File root = SD.open("/");
  if (!root) return false;
  bool ok = root.isDirectory();
  root.close();
  return ok;
}

void handleSdStatus() {
  bool ok = isSdOk();
  server.send(200, "application/json", String("{\"ok\":") + (ok ? "true" : "false") + "}");
}

// Zwraca liste plikow backupu z folderu /backup (nazwa + rozmiar w
// bajtach), zeby moc je pokazac na stronie Rechnung i pozwolic
// skasowac stare.
void handleBackupList() {
  String json = "[";
  bool first = true;
  File dir = SD.open("/backup");
  if (dir && dir.isDirectory()) {
    File entry = dir.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        String name = String(entry.name());
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (!first) json += ",";
        first = false;
        json += "{\"name\":\"" + name + "\",\"size\":" + String(entry.size()) + "}";
      }
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();
  }
  json += "]";
  server.send(200, "application/json", json);
}

// Kasuje POJEDYNCZY plik backupu z karty SD (np. stary, juz
// niepotrzebny). Zabezpieczone przed wyjsciem poza folder /backup.
void handleDeleteBackup() {
  if (!server.hasArg("name")) { server.send(400, "text/plain", "ERROR"); return; }
  String name = server.arg("name");
  if (name.length() == 0 || name.indexOf('/') >= 0 || name.indexOf("..") >= 0) {
    server.send(400, "text/plain", "ERROR: zla nazwa pliku");
    return;
  }
  String path = "/backup/" + name;
  if (SD.exists(path.c_str()) && SD.remove(path.c_str())) {
    server.send(200, "text/plain", "OK");
  } else {
    server.send(500, "text/plain", "ERROR");
  }
}

void handleBudget() {
  if (server.hasArg("amount")) {
    monthlyBudget = server.arg("amount").toFloat();
    calculateTotals();
    saveTotals();
  }
  server.send(200, "text/plain", "OK");
}

void handleParagon() {
  if (server.hasArg("shop") && server.hasArg("amount")) {
    String shop = server.arg("shop");
    float amount = server.arg("amount").toFloat();
    
    if (shop == "penny") { 
      pennyHistory.push_back(amount);
      pennyTotal += amount; 
    }
    else if (shop == "aldi") { 
      aldiHistory.push_back(amount);
      aldiTotal += amount; 
    }
    else if (shop == "edeka") { 
      edekaHistory.push_back(amount);
      edekaTotal += amount; 
    }
    else if (shop == "backerei") { 
      backereiHistory.push_back(amount);
      backereiTotal += amount; 
    }
    else if (shop == "sonstige") { 
      sonstigeHistory.push_back(amount);
      sonstigeTotal += amount; 
    }
    calculateTotals();
    saveTotals();
  }
  server.send(200, "text/plain", "OK");
}

void handleUndo() {
  if (server.hasArg("shop")) {
    String shop = server.arg("shop");
    
    if (shop == "penny" && pennyHistory.size() > 0) {
      float lastAmount = pennyHistory.back();
      pennyHistory.pop_back();
      pennyTotal -= lastAmount;
    }
    else if (shop == "aldi" && aldiHistory.size() > 0) {
      float lastAmount = aldiHistory.back();
      aldiHistory.pop_back();
      aldiTotal -= lastAmount;
    }
    else if (shop == "edeka" && edekaHistory.size() > 0) {
      float lastAmount = edekaHistory.back();
      edekaHistory.pop_back();
      edekaTotal -= lastAmount;
    }
    else if (shop == "backerei" && backereiHistory.size() > 0) {
      float lastAmount = backereiHistory.back();
      backereiHistory.pop_back();
      backereiTotal -= lastAmount;
    }
    else if (shop == "sonstige" && sonstigeHistory.size() > 0) {
      float lastAmount = sonstigeHistory.back();
      sonstigeHistory.pop_back();
      sonstigeTotal -= lastAmount;
    }
    calculateTotals();
    saveTotals();
  }
  server.send(200, "text/plain", "OK");
}

void handleExportData() {
  String csv = "Monat;Jahr;Aldi (€);Edeka (€);Penny (€);Bäckerei (€);Sonstige (€);Gesamt (€);Budget (€)\n";
  csv += String(currentMonth) + ";" + String(currentYear) + ";";
  csv += String(aldiTotal, 2) + ";" + String(edekaTotal, 2) + ";" + String(pennyTotal, 2) + ";" + String(backereiTotal, 2) + ";" + String(sonstigeTotal, 2) + ";" + String(grandTotal, 2) + ";" + String(monthlyBudget, 2) + "\n";
  
  for (int i = 0; i < billCount; i++) {
    csv += String(billHistory[i].month) + ";" + String(billHistory[i].year) + ";";
    csv += String(billHistory[i].aldi, 2) + ";" + String(billHistory[i].edeka, 2) + ";" + String(billHistory[i].penny, 2) + ";" + String(billHistory[i].backerei, 2) + ";" + String(billHistory[i].sonstige, 2) + ";" + String(billHistory[i].total, 2) + ";" + "\n";
  }
  server.send(200, "text/csv", csv);
}

// Parsuje jeden wiersz historii z CSV (Miesiac;Rok;Aldi;Edeka;Penny;Backerei;Sonstige;Suma;)
// do struktury MonthlyBill. Zwraca false, jesli wiersz jest niepoprawny.
bool parseHistoryLine(String line, MonthlyBill &out) {
  line.trim();
  if (line.length() == 0) return false;
  int s1 = line.indexOf(';');
  int s2 = line.indexOf(';', s1 + 1);
  int s3 = line.indexOf(';', s2 + 1);
  int s4 = line.indexOf(';', s3 + 1);
  int s5 = line.indexOf(';', s4 + 1);
  int s6 = line.indexOf(';', s5 + 1);
  int s7 = line.indexOf(';', s6 + 1);
  if (s1 < 0 || s2 < 0 || s3 < 0 || s4 < 0 || s5 < 0 || s6 < 0 || s7 < 0) return false;

  out.month     = line.substring(0, s1).toInt();
  out.year      = line.substring(s1 + 1, s2).toInt();
  float aldi     = line.substring(s2 + 1, s3).toFloat();
  float edeka    = line.substring(s3 + 1, s4).toFloat();
  float penny    = line.substring(s4 + 1, s5).toFloat();
  out.backerei  = line.substring(s5 + 1, s6).toFloat();
  out.sonstige  = line.substring(s6 + 1, s7).toFloat();
  out.total     = line.substring(s7 + 1).toFloat(); // ewentualny "koncowy ;" nie przeszkadza
  out.aldi = aldi;
  out.edeka = edeka;
  out.penny = penny;
  return true;
}

void handleImportData() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "ERROR");
    return;
  }
  String csv = server.arg("plain");

  // Linia 1 = naglowek, pomijamy
  int headerEnd = csv.indexOf('\n');
  if (headerEnd <= 0) { server.send(400, "text/plain", "ERROR"); return; }
  String rest = csv.substring(headerEnd + 1);

  // Linia 2 = biezacy miesiac
  int line2End = rest.indexOf('\n');
  String currentLine = (line2End >= 0) ? rest.substring(0, line2End) : rest;
  currentLine.trim();

  MonthlyBill current;
  if (!parseHistoryLine(currentLine, current)) {
    server.send(400, "text/plain", "ERROR");
    return;
  }
  aldiTotal = current.aldi;
  edekaTotal = current.edeka;
  pennyTotal = current.penny;
  backereiTotal = current.backerei;
  sonstigeTotal = current.sonstige;
  currentMonth = current.month;
  currentYear = current.year;
  calculateTotals();
  saveTotals();

  // Kolejne linie (jesli sa) = "VORHERIGE MONATE" / historia
  billCount = 0;
  if (line2End >= 0) {
    String historyPart = rest.substring(line2End + 1);
    int pos = 0;
    while (pos < (int)historyPart.length() && billCount < 12) {
      int eol = historyPart.indexOf('\n', pos);
      String line = (eol >= 0) ? historyPart.substring(pos, eol) : historyPart.substring(pos);
      pos = (eol >= 0) ? eol + 1 : historyPart.length();

      MonthlyBill entry;
      if (parseHistoryLine(line, entry)) {
        billHistory[billCount] = entry;
        billCount++;
      }
    }
  }
  writeHistoryFile();

  server.send(200, "text/plain", "OK");
}

void handleTotals() {
  String json = "{\"penny\":" + String(pennyTotal, 2) + ",\"aldi\":" + String(aldiTotal, 2) + ",\"edeka\":" + String(edekaTotal, 2) + ",\"backerei\":" + String(backereiTotal, 2) + ",\"sonstige\":" + String(sonstigeTotal, 2) + ",\"total\":" + String(grandTotal, 2) + ",\"budget\":" + String(monthlyBudget, 2) + "}";
  server.send(200, "application/json", json);
}

void handleBill() {
  calculateTotals();
  String json = "{\"penny\":" + String(pennyTotal, 2) + ",\"aldi\":" + String(aldiTotal, 2) + ",\"edeka\":" + String(edekaTotal, 2) + ",\"backerei\":" + String(backereiTotal, 2) + ",\"sonstige\":" + String(sonstigeTotal, 2) + ",\"total\":" + String(grandTotal, 2) + ",\"history\":[";
  for (int i = 0; i < billCount; i++) {
    if (i > 0) json += ",";
    json += "{\"month\":" + String(billHistory[i].month) + ",\"year\":" + String(billHistory[i].year) + ",\"total\":" + String(billHistory[i].total, 2) + "}";
  }
  json += "]}";
  server.send(200, "application/json", json);
}

void handleList() {
  String shop = server.arg("shop");
  String json = "[";
  if (shop == "penny") {
    for (int i = 0; i < pennyCount; i++) {
      if (i > 0) json += ",";
      json += "{\"id\":" + String(i) + ",\"name\":\"" + pennyItems[i].name + "\",\"qty\":" + String(pennyItems[i].quantity) + ",\"bought\":" + (pennyItems[i].bought ? "true" : "false") + "}";
    }
  } else if (shop == "aldi") {
    for (int i = 0; i < aldiCount; i++) {
      if (i > 0) json += ",";
      json += "{\"id\":" + String(i) + ",\"name\":\"" + aldiItems[i].name + "\",\"qty\":" + String(aldiItems[i].quantity) + ",\"bought\":" + (aldiItems[i].bought ? "true" : "false") + "}";
    }
  } else if (shop == "edeka") {
    for (int i = 0; i < edekaCount; i++) {
      if (i > 0) json += ",";
      json += "{\"id\":" + String(i) + ",\"name\":\"" + edekaItems[i].name + "\",\"qty\":" + String(edekaItems[i].quantity) + ",\"bought\":" + (edekaItems[i].bought ? "true" : "false") + "}";
    }
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleAdd() {
  if (server.hasArg("shop") && server.hasArg("name")) {
    String shop = server.arg("shop");
    String name = server.arg("name");
    if (shop == "penny" && pennyCount < 200) {
      pennyItems[pennyCount].name = name;
      pennyItems[pennyCount].quantity = 1;
      pennyItems[pennyCount].bought = false;
      pennyCount++;
      saveData();
    }
    else if (shop == "aldi" && aldiCount < 200) {
      aldiItems[aldiCount].name = name;
      aldiItems[aldiCount].quantity = 1;
      aldiItems[aldiCount].bought = false;
      aldiCount++;
      saveData();
    }
    else if (shop == "edeka" && edekaCount < 200) {
      edekaItems[edekaCount].name = name;
      edekaItems[edekaCount].quantity = 1;
      edekaItems[edekaCount].bought = false;
      edekaCount++;
      saveData();
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleQuantity() {
  if (server.hasArg("shop") && server.hasArg("id") && server.hasArg("delta")) {
    String shop = server.arg("shop");
    int id = server.arg("id").toInt();
    int delta = server.arg("delta").toInt();
    
    if (shop == "penny" && id < pennyCount) {
      int newQty = pennyItems[id].quantity + delta;
      if (newQty >= 1 && newQty <= 99) {
        pennyItems[id].quantity = newQty;
        saveData();
      }
    }
    else if (shop == "aldi" && id < aldiCount) {
      int newQty = aldiItems[id].quantity + delta;
      if (newQty >= 1 && newQty <= 99) {
        aldiItems[id].quantity = newQty;
        saveData();
      }
    }
    else if (shop == "edeka" && id < edekaCount) {
      int newQty = edekaItems[id].quantity + delta;
      if (newQty >= 1 && newQty <= 99) {
        edekaItems[id].quantity = newQty;
        saveData();
      }
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleToggle() {
  if (server.hasArg("shop") && server.hasArg("id")) {
    String shop = server.arg("shop");
    int id = server.arg("id").toInt();
    
    if (shop == "penny" && id < pennyCount) {
      pennyItems[id].bought = !pennyItems[id].bought;
      saveData();
    }
    else if (shop == "aldi" && id < aldiCount) {
      aldiItems[id].bought = !aldiItems[id].bought;
      saveData();
    }
    else if (shop == "edeka" && id < edekaCount) {
      edekaItems[id].bought = !edekaItems[id].bought;
      saveData();
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleDelete() {
  if (server.hasArg("shop") && server.hasArg("id")) {
    String shop = server.arg("shop");
    int id = server.arg("id").toInt();
    
    if (shop == "penny" && id < pennyCount) {
      for (int i = id; i < pennyCount - 1; i++) pennyItems[i] = pennyItems[i + 1];
      pennyCount--;
      saveData();
    }
    else if (shop == "aldi" && id < aldiCount) {
      for (int i = id; i < aldiCount - 1; i++) aldiItems[i] = aldiItems[i + 1];
      aldiCount--;
      saveData();
    }
    else if (shop == "edeka" && id < edekaCount) {
      for (int i = id; i < edekaCount - 1; i++) edekaItems[i] = edekaItems[i + 1];
      edekaCount--;
      saveData();
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleClearHistory() {
  billCount = 0;
  if (SD.exists("/history.txt")) {
    SD.remove("/history.txt");
  }
  server.send(200, "text/html", "<html><body style='background:#1a1a2e;color:white;text-align:center;padding:50px'><h1>✅ Historia wyczyszczona!</h1><a href='/rechnung' style='color:#4CAF50'>← Wróć do rechnung</a></body></html>");
}

// Recznie wywolany backup (przycisk na stronie Rechnung)
void handleBackupNow() {
  bool ok = createFullBackup();
  if (ok) {
    server.send(200, "text/plain", "OK");
  } else {
    server.send(500, "text/plain", "ERROR");
  }
}

// Pozwala pobrac aktualny backup wprost do telefonu/komputera
// (niezalezna kopia POZA karta SD - przyda sie, gdyby kiedys
// zgubila sie/uszkodzila sie karta SD, a nie tylko sam ESP32).
void handleDownloadBackup() {
  server.send(200, "text/csv", buildFullBackupCsv());
}

// Przywraca WSZYSTKO (3 listy zakupow + Monatliches Budget +
// historia) z pliku backupu utworzonego przez ten sam program
// (przez BACKUP albo POBIERZ BACKUP). Uzywane np. po wymianie
// ESP32 i/lub karty SD.
void handleRestoreBackup() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "ERROR: brak danych");
    return;
  }
  String data = server.arg("plain");

  int idxLists = data.indexOf("=== LISTY ZAKUPOW ===");
  int idxBudget = data.indexOf("=== MONATLICHES BUDGET");
  int idxHistory = data.indexOf("=== HISTORIA POPRZEDNICH MIESIECY ===");
  if (idxLists < 0 || idxBudget < 0 || idxHistory < 0) {
    server.send(400, "text/plain", "ERROR: to nie jest plik backupu z tego programu");
    return;
  }

  String listsSection = data.substring(idxLists, idxBudget);
  String budgetSection = data.substring(idxBudget, idxHistory);
  String historySection = data.substring(idxHistory);

  // --- 1. LISTY ZAKUPOW ---
  aldiCount = 0; edekaCount = 0; pennyCount = 0;
  {
    int pos = listsSection.indexOf('\n');       // koniec linii "=== LISTY ZAKUPOW ==="
    pos = listsSection.indexOf('\n', pos + 1);  // koniec linii naglowka kolumn
    while (pos >= 0) {
      int eol = listsSection.indexOf('\n', pos + 1);
      String line = (eol >= 0) ? listsSection.substring(pos + 1, eol) : listsSection.substring(pos + 1);
      pos = eol;
      line.trim();
      if (line.length() == 0) continue;

      int s1 = line.indexOf(';');
      int s2 = line.indexOf(';', s1 + 1);
      int s3 = line.indexOf(';', s2 + 1);
      if (s1 < 0 || s2 < 0 || s3 < 0) continue;

      String shop = line.substring(0, s1);
      String name = line.substring(s1 + 1, s2);
      int qty = line.substring(s2 + 1, s3).toInt();
      bool bought = (line.substring(s3 + 1).toInt() == 1);

      if (shop.equalsIgnoreCase("Aldi") && aldiCount < 200) {
        aldiItems[aldiCount] = { name, qty, bought }; aldiCount++;
      } else if (shop.equalsIgnoreCase("Edeka") && edekaCount < 200) {
        edekaItems[edekaCount] = { name, qty, bought }; edekaCount++;
      } else if (shop.equalsIgnoreCase("Penny") && pennyCount < 200) {
        pennyItems[pennyCount] = { name, qty, bought }; pennyCount++;
      }
    }
  }
  saveData();

  // --- 2. MONATLICHES BUDGET ---
  {
    int pos = budgetSection.indexOf('\n');      // koniec linii "=== MONATLICHES BUDGET ==="
    pos = budgetSection.indexOf('\n', pos + 1); // koniec linii naglowka kolumn
    if (pos >= 0) {
      int eol = budgetSection.indexOf('\n', pos + 1);
      String line = (eol >= 0) ? budgetSection.substring(pos + 1, eol) : budgetSection.substring(pos + 1);
      line.trim();

      int s1 = line.indexOf(';');
      int s2 = line.indexOf(';', s1 + 1);
      int s3 = line.indexOf(';', s2 + 1);
      int s4 = line.indexOf(';', s3 + 1);
      int s5 = line.indexOf(';', s4 + 1);
      int s6 = line.indexOf(';', s5 + 1);
      int s7 = line.indexOf(';', s6 + 1);
      int s8 = line.indexOf(';', s7 + 1);
      if (s1 > 0 && s2 > 0 && s3 > 0 && s4 > 0 && s5 > 0 && s6 > 0 && s7 > 0 && s8 > 0) {
        currentMonth   = line.substring(0, s1).toInt();
        currentYear    = line.substring(s1 + 1, s2).toInt();
        aldiTotal      = line.substring(s2 + 1, s3).toFloat();
        edekaTotal     = line.substring(s3 + 1, s4).toFloat();
        pennyTotal     = line.substring(s4 + 1, s5).toFloat();
        backereiTotal  = line.substring(s5 + 1, s6).toFloat();
        sonstigeTotal  = line.substring(s6 + 1, s7).toFloat();
        // s7+1..s8 = Gesamt - pomijamy, przeliczamy sami nizej
        monthlyBudget  = line.substring(s8 + 1).toFloat();
        calculateTotals();
        saveTotals();
      }
    }
  }

  // --- 3. HISTORIA POPRZEDNICH MIESIECY ---
  {
    billCount = 0;
    int pos = historySection.indexOf('\n');       // koniec linii "=== HISTORIA ... ==="
    pos = historySection.indexOf('\n', pos + 1);  // koniec linii naglowka kolumn
    while (pos >= 0 && billCount < 12) {
      int eol = historySection.indexOf('\n', pos + 1);
      String line = (eol >= 0) ? historySection.substring(pos + 1, eol) : historySection.substring(pos + 1);
      pos = eol;

      MonthlyBill entry;
      if (parseHistoryLine(line, entry)) {
        billHistory[billCount] = entry;
        billCount++;
      }
    }
    writeHistoryFile();
  }

  server.send(200, "text/plain", "OK");
}

void handleExportShop() { String shop = server.arg("shop"); String csv = "Nazwa;Ilosc;Kupione\n"; if (shop == "penny") { for (int i = 0; i < pennyCount; i++) { csv += pennyItems[i].name + ";" + String(pennyItems[i].quantity) + ";" + (pennyItems[i].bought ? "1" : "0") + "\n"; } } else if (shop == "aldi") { for (int i = 0; i < aldiCount; i++) { csv += aldiItems[i].name + ";" + String(aldiItems[i].quantity) + ";" + (aldiItems[i].bought ? "1" : "0") + "\n"; } } else if (shop == "edeka") { for (int i = 0; i < edekaCount; i++) { csv += edekaItems[i].name + ";" + String(edekaItems[i].quantity) + ";" + (edekaItems[i].bought ? "1" : "0") + "\n"; } } server.send(200, "text/csv", csv); } void handleImportShop() { if (server.hasArg("plain")) { String shop = server.arg("shop"); String csv = server.arg("plain"); int firstNewline = csv.indexOf('\n'); if (firstNewline > 0) { csv = csv.substring(firstNewline + 1); } Item tempItems[200]; int tempCount = 0; int start = 0; int end = csv.indexOf('\n'); while (end > 0 && tempCount < 200) { String line = csv.substring(start, end); line.trim(); start = end + 1; end = csv.indexOf('\n', start); if (line.length() > 0) { int sem1 = line.indexOf(';'); int sem2 = line.indexOf(';', sem1 + 1); if (sem1 > 0) { String name = line.substring(0, sem1); int qty = 1; bool bought = false; if (sem2 > 0) { qty = line.substring(sem1 + 1, sem2).toInt(); bought = (line.substring(sem2 + 1).toInt() == 1); } else { qty = line.substring(sem1 + 1).toInt(); } tempItems[tempCount].name = name; tempItems[tempCount].quantity = qty; tempItems[tempCount].bought = bought; tempCount++; } } } if (shop == "penny") { for (int i = 0; i < tempCount; i++) pennyItems[i] = tempItems[i]; pennyCount = tempCount; } else if (shop == "aldi") { for (int i = 0; i < tempCount; i++) aldiItems[i] = tempItems[i]; aldiCount = tempCount; } else if (shop == "edeka") { for (int i = 0; i < tempCount; i++) edekaItems[i] = tempItems[i]; edekaCount = tempCount; } saveData(); server.send(200, "text/plain", "OK"); } else { server.send(400, "text/plain", "ERROR"); } }
void handleMove() {
  if (server.hasArg("shop") && server.hasArg("from") && server.hasArg("to")) {
    String shop = server.arg("shop");
    int from = server.arg("from").toInt();
    int to = server.arg("to").toInt();
    
    if (shop == "penny" && from != to && from >= 0 && from < pennyCount && to >= 0 && to < pennyCount) {
      Item temp = pennyItems[from];
      if (from < to) {
        for (int i = from; i < to; i++) pennyItems[i] = pennyItems[i + 1];
      } else {
        for (int i = from; i > to; i--) pennyItems[i] = pennyItems[i - 1];
      }
      pennyItems[to] = temp;
      saveData();
    }
    else if (shop == "aldi" && from != to && from >= 0 && from < aldiCount && to >= 0 && to < aldiCount) {
      Item temp = aldiItems[from];
      if (from < to) {
        for (int i = from; i < to; i++) aldiItems[i] = aldiItems[i + 1];
      } else {
        for (int i = from; i > to; i--) aldiItems[i] = aldiItems[i - 1];
      }
      aldiItems[to] = temp;
      saveData();
    }
    else if (shop == "edeka" && from != to && from >= 0 && from < edekaCount && to >= 0 && to < edekaCount) {
      Item temp = edekaItems[from];
      if (from < to) {
        for (int i = from; i < to; i++) edekaItems[i] = edekaItems[i + 1];
      } else {
        for (int i = from; i > to; i--) edekaItems[i] = edekaItems[i - 1];
      }
      edekaItems[to] = temp;
      saveData();
    }
  }
  server.send(200, "text/plain", "OK");
}

// ============================================================
// STABILNOSC - AUTOMATYCZNY RESTART I PONOWNE LACZENIE Z WIFI
// ============================================================

// Sprawdza co jakis czas, czy WiFi jest nadal polaczone - jesli nie
// (np. router sie zresetowal albo sygnal na chwile zanikl), probuje
// polaczyc sie ponownie samo, bez potrzeby recznego resetu ESP32.
void checkWifiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi rozlaczone - probuje polaczyc ponownie...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

// Restartuje ESP32 automatycznie RAZ dziennie w nocy o godzinie 4:00
// (a nie w losowym momencie) - dla stabilnosci dlugo dzialajacego
// urzadzenia, bez przeszkadzania w trakcie robienia zakupow w ciagu
// dnia. Wszystkie dane sa juz na biezaco zapisywane na karcie SD,
// wiec restart niczego nie gubi. Data ostatniego auto-restartu jest
// zapamietana na SD, zeby restart nie powtorzyl sie kilka razy pod
// rzad w tej samej godzinie.
void checkDailyRestart() {
  time_t now = time(nullptr);
  if (now < 1000000000) return; // czas jeszcze nie zsynchronizowany
  struct tm* tm_now = localtime(&now);
  String today = todayDateString();
  if (tm_now->tm_hour == 4 && today != lastAutoRestartDate) {
    Serial.println("🔄 Godzina 4:00 - automatyczny nocny restart ESP32...");
    lastAutoRestartDate = today;
    saveRestartMeta();
    delay(200);
    ESP.restart();
  }
}

// ============================================================
// SETUP
// ============================================================

// ============================================================
// CZĘŚĆ 2: MONITOR CIŚNIENIA (routes: /cisnienie, /cisnienie/...)
// ============================================================
// ==================== PROGRAM 1: CIŚNIENIE ====================
// NAPRAWA: typy pol zmniejszone do najmniejszych, ktore wystarcza na
// realne zakresy (systolic/diastolic/pulse/month/day/hour/minute/second
// zawsze miesza sie w 0-255). Przy MAX_MEASUREMENTS=3000 oryginalna
// struktura (same "int", czyli 40 bajtow/rekord) potrzebowala 3000*40 =
// 120 000 bajtow pamieci RAM (DRAM) - na "zwyklym" ESP32 Dev Module
// (bez PSRAM) to za duzo i kompilacja konczyla sie bledem linkera
// "DRAM segment data does not fit" / "region dram0_0_seg overflowed".
// Nowa struktura to 16 bajtow/rekord -> 3000*16 = 48 000 bajtow, czyli
// ok. 72 KB mniej, co z zapasem mieści sie w dostepnej pamieci.
struct Measurement {
  uint32_t id;
  uint16_t year;
  uint8_t systolic;
  uint8_t diastolic;
  uint8_t pulse;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};
// NAPRAWA (po polaczeniu 3 programow w 1): zmniejszone z 3000 do 1600, zeby
// zrobic miejsce w RAM dla pozostalych 2 programow (Einkaufsliste + Vorratskammer)
// dzialajacych teraz w tym samym szkicu - inaczej linker zglasza blad
// "DRAM segment data does not fit" / "region dram0_0_seg overflowed".
// 1600 rekordow to nadal ok. 2 lata pomiarow przy 2 pomiarach dziennie.
const int MAX_MEASUREMENTS = 1600;
Measurement measurements[MAX_MEASUREMENTS];
int measureCount = 0;
int nextMeasureId = 1; // NAPRAWA: osobny, stale rosnacy licznik ID (zapobiega duplikatom po usunieciu wpisu)
int cisnienieYear, cisnienieMonth, cisnienieDay;
int cisnienieHour, cisnienieMinute, cisnienieSec;

void saveMeasurements() {
  File file = SD.open("/measurements.txt", FILE_WRITE);
  if (!file) return;
  file.println(measureCount);
  for (int i = 0; i < measureCount; i++) {
    file.println(measurements[i].id);
    file.println(measurements[i].systolic);
    file.println(measurements[i].diastolic);
    file.println(measurements[i].pulse);
    file.println(measurements[i].year);
    file.println(measurements[i].month);
    file.println(measurements[i].day);
    file.println(measurements[i].hour);
    file.println(measurements[i].minute);
    file.println(measurements[i].second);
  }
  file.close();
}

void loadMeasurements() {
  if (!SD.exists("/measurements.txt")) { measureCount = 0; nextMeasureId = 1; return; }
  File file = SD.open("/measurements.txt", FILE_READ);
  if (!file) return;
  measureCount = file.parseInt();
  file.readStringUntil('\n');
  if (measureCount > MAX_MEASUREMENTS) measureCount = MAX_MEASUREMENTS;
  for (int i = 0; i < measureCount; i++) {
    measurements[i].id = file.parseInt(); file.readStringUntil('\n');
    measurements[i].systolic = file.parseInt(); file.readStringUntil('\n');
    measurements[i].diastolic = file.parseInt(); file.readStringUntil('\n');
    measurements[i].pulse = file.parseInt(); file.readStringUntil('\n');
    measurements[i].year = file.parseInt(); file.readStringUntil('\n');
    measurements[i].month = file.parseInt(); file.readStringUntil('\n');
    measurements[i].day = file.parseInt(); file.readStringUntil('\n');
    measurements[i].hour = file.parseInt(); file.readStringUntil('\n');
    measurements[i].minute = file.parseInt(); file.readStringUntil('\n');
    measurements[i].second = file.parseInt(); file.readStringUntil('\n');
  }
  file.close();
  // NAPRAWA: policz nastepne wolne ID na podstawie wczytanych danych,
  // zeby nowe wpisy (dodawanie i import) nie powtarzaly ID po usunieciu wpisu
  nextMeasureId = 1;
  for (int i = 0; i < measureCount; i++) {
    if (measurements[i].id >= nextMeasureId) nextMeasureId = measurements[i].id + 1;
  }
}

void updateTime() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 2000)) {
    cisnienieYear = timeinfo.tm_year + 1900;
    cisnienieMonth = timeinfo.tm_mon + 1;
    cisnienieDay = timeinfo.tm_mday;
    cisnienieHour = timeinfo.tm_hour;
    cisnienieMinute = timeinfo.tm_min;
    cisnienieSec = timeinfo.tm_sec;
  }
}

String getColor(int sys, int dia) {
  if (sys < 90 && dia < 60) return "#8B4513";
  if (sys >= 180 || dia >= 110) return "#1a1a2e";
  if (sys >= 160 || dia >= 100) return "#F44336";
  if (sys >= 140 || dia >= 90) return "#FF9800";
  if (sys >= 130 || dia >= 85) return "#FFC107";
  if (sys >= 120 || dia >= 80) return "#2196F3";
  return "#4CAF50";
}

String getCalendarColor(int sys, int dia) {
  // NAPRAWA: ta sama kolejnosc warunkow co w getColor()/getCategoryText(),
  // zeby zadna kombinacja sys/dia nie wpadala w domyslny szary "niesklasyfikowany"
  // kolor (np. sys=95, dia=70 wczesniej nie lapal sie w zaden warunek).
  if (sys < 90 && dia < 60) return "#8B4513";
  if (sys >= 160 || dia >= 100) return "#F44336";
  if (sys >= 140 || dia >= 90) return "#FF9800";
  if (sys >= 130 || dia >= 85) return "#FFC107";
  if (sys >= 120 || dia >= 80) return "#2196F3";
  return "#4CAF50";
}

String getCategoryText(int sys, int dia) {
  if (sys < 90 && dia < 60) return "HYPOTONIE";
  if (sys >= 180 || dia >= 110) return "HYPERTONIE 3";
  if (sys >= 160 || dia >= 100) return "HYPERTONIE 2";
  if (sys >= 140 || dia >= 90) return "HYPERTONIE 1";
  if (sys >= 130 || dia >= 85) return "HOCH NORMAL";
  if (sys >= 120 || dia >= 80) return "NORMAL";
  return "OPTIMAL";
}

void getStats(float &avgSys, float &avgDia, float &avgPulse, int &minSys, int &maxSys, int &minDia, int &maxDia) {
  if (measureCount == 0) { avgSys = avgDia = avgPulse = 0; minSys = maxSys = minDia = maxDia = 0; return; }
  float sumSys = 0, sumDia = 0, sumPulse = 0;
  minSys = 300; maxSys = 0; minDia = 300; maxDia = 0;
  for (int i = 0; i < measureCount; i++) {
    sumSys += measurements[i].systolic;
    sumDia += measurements[i].diastolic;
    sumPulse += measurements[i].pulse;
    if (measurements[i].systolic < minSys) minSys = measurements[i].systolic;
    if (measurements[i].systolic > maxSys) maxSys = measurements[i].systolic;
    if (measurements[i].diastolic < minDia) minDia = measurements[i].diastolic;
    if (measurements[i].diastolic > maxDia) maxDia = measurements[i].diastolic;
  }
  avgSys = sumSys / measureCount;
  avgDia = sumDia / measureCount;
  avgPulse = sumPulse / measureCount;
}
void handleCisnienie();
void handleCisnienieList();
void handleCisnienieAdd();
void handleCisnienieDelete();
void handleCisnienieExport();
void handleCisnienieImport();

// ==================== CIŚNIENIE HANDLERY ====================
void handleCisnienieExport() {
  // NAPRAWA: strumieniowanie po jednym wierszu (server.sendContent) zamiast
  // sklejania calego CSV w jednym Stringu w RAM - przy MAX_MEASUREMENTS=3000
  // budowanie jednego wielkiego Stringa moglo zuzyc kilkaset KB pamieci
  // i doprowadzic do fragmentacji sterty / awarii ESP32.
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  server.sendContent("Data;Godzina;Systolisch;Diastolisch;Puls;Bewertung\n");
  for (int i = 0; i < measureCount; i++) {
    char dateStr[20], timeStr[20];
    sprintf(dateStr, "%02d.%02d.%04d", measurements[i].day, measurements[i].month, measurements[i].year);
    sprintf(timeStr, "%02d:%02d:%02d", measurements[i].hour, measurements[i].minute, measurements[i].second);
    String line = String(dateStr) + ";" + String(timeStr) + ";" + String(measurements[i].systolic) + ";" + String(measurements[i].diastolic) + ";" + String(measurements[i].pulse) + ";" + getCategoryText(measurements[i].systolic, measurements[i].diastolic) + "\n";
    server.sendContent(line);
  }
}

void handleCisnienieImport() {
  if (server.hasArg("data")) {
    String csvData = server.arg("data");
    int lines = 0;
    int startIdx = 0;
    int endIdx = csvData.indexOf('\n');
    if (endIdx > 0) {
      startIdx = endIdx + 1;
      endIdx = csvData.indexOf('\n', startIdx);
    }
    while (endIdx > 0 && lines < MAX_MEASUREMENTS) {
      String line = csvData.substring(startIdx, endIdx);
      line.trim();
      startIdx = endIdx + 1;
      endIdx = csvData.indexOf('\n', startIdx);
      if (line.length() > 0) {
        int sem1 = line.indexOf(';');
        int sem2 = line.indexOf(';', sem1 + 1);
        int sem3 = line.indexOf(';', sem2 + 1);
        int sem4 = line.indexOf(';', sem3 + 1);
        int sem5 = line.indexOf(';', sem4 + 1);
        if (sem1 > 0 && sem2 > 0 && sem3 > 0 && sem4 > 0) {
          String dateStr = line.substring(0, sem1);
          String timeStr = line.substring(sem1 + 1, sem2);
          int sys = line.substring(sem2 + 1, sem3).toInt();
          int dia = line.substring(sem3 + 1, sem4).toInt();
          int pulse = line.substring(sem4 + 1, sem5).toInt();
          int day = dateStr.substring(0, 2).toInt();
          int month = dateStr.substring(3, 5).toInt();
          int year = dateStr.substring(6, 10).toInt();
          int hour = timeStr.substring(0, 2).toInt();
          int minute = timeStr.substring(3, 5).toInt();
          int second = timeStr.substring(6, 8).toInt();
          if (measureCount < MAX_MEASUREMENTS && sys > 0 && dia > 0) {
            measurements[measureCount].id = nextMeasureId++; // NAPRAWA: bylo measureCount+1
            measurements[measureCount].systolic = sys;
            measurements[measureCount].diastolic = dia;
            measurements[measureCount].pulse = pulse;
            measurements[measureCount].year = year;
            measurements[measureCount].month = month;
            measurements[measureCount].day = day;
            measurements[measureCount].hour = hour;
            measurements[measureCount].minute = minute;
            measurements[measureCount].second = second;
            measureCount++;
            lines++;
          }
        }
      }
    }
    saveMeasurements();
    server.send(200, "text/plain", "OK Importowano " + String(lines) + " wpisów");
  } else {
    server.send(400, "text/plain", "Brak danych");
  }
}

void handleCisnienieList() {
  // NAPRAWA: strumieniowanie JSON po jednym rekordzie zamiast budowania
  // calej tablicy w jednym Stringu - ten endpoint jest wolany co 30s oraz po
  // kazdym dodaniu/usunieciu, wiec przy duzej liczbie pomiarow (limit 3000)
  // pojedynczy String moglby urosnac do kilkuset KB i wyczerpac RAM ESP32.
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent("[");
  for (int i = 0; i < measureCount; i++) {
    if (i > 0) server.sendContent(",");
    char dateStr[20], timeStr[20];
    sprintf(dateStr, "%02d.%02d.%04d", measurements[i].day, measurements[i].month, measurements[i].year);
    sprintf(timeStr, "%02d:%02d:%02d", measurements[i].hour, measurements[i].minute, measurements[i].second);
    String chunk = "{\"id\":" + String(measurements[i].id) + ",\"sys\":" + String(measurements[i].systolic) + ",\"dia\":" + String(measurements[i].diastolic) + ",\"pulse\":" + String(measurements[i].pulse) +
                   ",\"date\":\"" + String(dateStr) + "\",\"time\":\"" + String(timeStr) + "\",\"year\":" + String(measurements[i].year) + ",\"month\":" + String(measurements[i].month) + ",\"day\":" + String(measurements[i].day) + "}";
    server.sendContent(chunk);
  }
  server.sendContent("]");
}

void handleCisnienieAdd() {
  if (!server.hasArg("sys") || !server.hasArg("dia") || !server.hasArg("pulse")) {
    server.send(400, "text/plain", "Brak danych pomiaru");
    return;
  }

  // NAPRAWA: walidacja zakresow po stronie serwera (wczesniej sprawdzane
  // tylko w JS, wiec bezposrednie zapytanie do /cisnienie/add moglo zapisac
  // dowolne, bezsensowne wartosci).
  int sys = server.arg("sys").toInt();
  int dia = server.arg("dia").toInt();
  int pulse = server.arg("pulse").toInt();
  if (sys < 50 || sys > 250 || dia < 30 || dia > 150 || pulse < 30 || pulse > 200) {
    server.send(400, "text/plain", "Nieprawidlowe wartosci pomiaru");
    return;
  }

  updateTime();
  // NAPRAWA: jesli czas NTP jeszcze sie nie zsynchronizowal, cisnienieYear itp.
  // maja wartosc 0 (niezainicjalizowane globalne), wiec pomiar zapisalby sie
  // z data "0.0.0000". Zamiast tego odrzucamy zapis i informujemy uzytkownika.
  if (cisnienieYear < 2000) {
    server.send(503, "text/plain", "Czas nie jest jeszcze zsynchronizowany (NTP) - sprobuj za kilka sekund");
    return;
  }

  if (measureCount >= MAX_MEASUREMENTS) {
    server.send(507, "text/plain", "Osiagnieto maksymalna liczbe pomiarow (" + String(MAX_MEASUREMENTS) + ")");
    return;
  }

  measurements[measureCount].id = nextMeasureId++; // NAPRAWA: bylo measureCount+1
  measurements[measureCount].systolic = sys;
  measurements[measureCount].diastolic = dia;
  measurements[measureCount].pulse = pulse;
  measurements[measureCount].year = cisnienieYear;
  measurements[measureCount].month = cisnienieMonth;
  measurements[measureCount].day = cisnienieDay;
  measurements[measureCount].hour = cisnienieHour;
  measurements[measureCount].minute = cisnienieMinute;
  measurements[measureCount].second = cisnienieSec;
  measureCount++;
  saveMeasurements();
  server.send(200, "text/plain", "OK");
}

void handleCisnienieDelete() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    for (int i = 0; i < measureCount; i++) {
      if (measurements[i].id == id) {
        for (int j = i; j < measureCount - 1; j++) measurements[j] = measurements[j + 1];
        measureCount--;
        saveMeasurements();
        break;
      }
    }
  }
  server.send(200, "text/plain", "OK");
}
void handleCisnienie() {
  float avgSys, avgDia, avgPulse;
  int minSys, maxSys, minDia, maxDia;
  getStats(avgSys, avgDia, avgPulse, minSys, maxSys, minDia, maxDia);
  String avgColor = getColor((int)avgSys, (int)avgDia);
  String avgText = getCategoryText((int)avgSys, (int)avgDia);
  
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><link rel='icon' type='image/svg+xml' href='data:image/svg+xml,%3Csvg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"%3E%3Ctext x=\"50\" y=\"70\" text-anchor=\"middle\" fill=\"white\" font-size=\"70\"%3E🩺%3C/text%3E%3C/svg%3E'><meta name='viewport' content='width=device-width, initial-scale=1.0, viewport-fit=cover'>";
  html += "<title>🩺 Blutdruck-Tagebuch</title>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js'></script>";
  html += "<style>";
  html += "*{margin:0;padding:0;box-sizing:border-box}";
  html += "body{background:linear-gradient(135deg,#1a1a2e,#16213e);color:#eee;font-family:'Segoe UI',Arial;padding:20px;min-height:100vh}";
  html += ".container{max-width:700px;margin:0 auto}";
  html += "h1{text-align:center;margin-bottom:10px}";
  html += ".back-btn{background:#2196F3;color:white;padding:10px 20px;border-radius:30px;text-decoration:none;display:inline-block;margin-bottom:20px}";
  html += ".card{background:rgba(255,255,255,0.1);border-radius:20px;padding:20px;margin-bottom:20px}";
  html += ".card-title{font-size:14px;color:#aaa;margin-bottom:15px}";
  html += ".stats-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:15px;margin-bottom:20px}";
  html += ".stat-box{background:rgba(255,255,255,0.08);border-radius:15px;padding:12px;text-align:center}";
  html += ".stat-label{font-size:11px;color:#aaa}";
  html += ".stat-value{font-size:24px;font-weight:bold}";
  html += ".input-group{display:flex;gap:10px;margin-bottom:15px;flex-wrap:wrap}";
  html += ".input-group input{flex:1;padding:15px;border-radius:30px;border:none;background:rgba(255,255,255,0.2);color:#eee;font-size:18px;text-align:center}";
  html += ".input-group input:first-child{background:rgba(244,67,54,0.3);border-left:4px solid #F44336}";
  html += ".input-group input:nth-child(2){background:rgba(33,150,243,0.3);border-left:4px solid #2196F3}";
  html += ".input-group input:nth-child(3){background:rgba(76,175,80,0.3);border-left:4px solid #4CAF50}";
  html += "button{background:#4CAF50;color:white;border:none;padding:15px;border-radius:30px;cursor:pointer;font-size:16px;font-weight:bold;width:100%}";
  html += "button.delete-btn{background:#ff4757;width:auto;padding:8px 15px;font-size:12px;margin-left:10px}";
  html += ".measurement-list{max-height:400px;overflow-y:auto}";
  html += ".measurement-item{background:rgba(255,255,255,0.08);border-radius:15px;padding:15px;margin-bottom:10px;display:flex;justify-content:space-between;align-items:center}";
  html += ".measurement-values{font-size:18px;font-weight:bold}";
  html += ".measurement-date{font-size:11px;color:#aaa;margin-top:5px}";
  html += ".legend{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;margin-bottom:15px}";
  html += ".legend-item{display:inline-block;padding:4px 10px;border-radius:20px;font-size:10px}";
  html += "canvas{max-height:200px;margin:20px 0}";
  html += ".calendar{display:grid;grid-template-columns:repeat(7,1fr);gap:5px;margin-top:10px}";
  html += ".cal-day{border-radius:10px;padding:8px;text-align:center;cursor:pointer;color:white;font-weight:bold}";
  html += ".cal-day.empty{background:rgba(255,255,255,0.05);cursor:default}";
  html += ".cal-weekday{text-align:center;font-size:12px;color:#aaa;padding:5px}";
  html += ".pagination{display:flex;gap:10px;justify-content:center;margin-top:15px;flex-wrap:wrap}";
  html += ".pagination button{padding:8px 15px;width:auto;margin:0}";
  html += ".page-info{text-align:center;margin-top:10px;font-size:12px;color:#aaa}";
  html += ".footer{text-align:center;font-size:11px;color:#555;margin-top:20px}";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<a href='/' class='back-btn'>← MENU GŁÓWNE</a>";
  html += "<h1>🩺 Blutdruck-Tagebuch</h1>";
  
  html += "<div class='card'><div class='card-title'>🎨 FARBEN</div><div class='legend'>";
  html += "<div class='legend-item' style='background:#4CAF50'>OPTIMAL</div>";
  html += "<div class='legend-item' style='background:#2196F3'>NORMAL</div>";
  html += "<div class='legend-item' style='background:#FFC107'>HOCH NORMAL</div>";
  html += "<div class='legend-item' style='background:#FF9800'>HYPERTONIE 1</div>";
  html += "<div class='legend-item' style='background:#F44336'>HYPERTONIE 2-3</div>";
  html += "<div class='legend-item' style='background:#8B4513'>HYPOTONIE</div>";
  html += "</div></div>";
  
  html += "<div class='card'><div class='card-title'>📊 STATISTIK</div>";
  html += "<div class='stats-grid'>";
  html += "<div class='stat-box'><div class='stat-label'>SYSTOLISCH</div><div class='stat-value' id='avgSys'>0</div></div>";
  html += "<div class='stat-box'><div class='stat-label'>DIASTOLISCH</div><div class='stat-value' id='avgDia'>0</div></div>";
  html += "<div class='stat-box'><div class='stat-label'>PULS</div><div class='stat-value' id='avgPulse'>0</div></div>";
  html += "</div><div class='stats-grid'>";
  html += "<div class='stat-box'><div class='stat-label'>MIN/MAX SYS</div><div class='stat-value' id='minMaxSys'>0/0</div></div>";
  html += "<div class='stat-box'><div class='stat-label'>MIN/MAX DIA</div><div class='stat-value' id='minMaxDia'>0/0</div></div>";
  html += "<div class='stat-box'><div class='stat-label'>ANZAHL</div><div class='stat-value' id='count'>0</div></div>";
  html += "</div>";
  // NOWE: osobne srednie dla pomiarow porannych (przed poludniem) i
  // wieczornych (po poludniu) - klinicznie bardziej sensowne niz jedna
  // wspolna srednia, bo cisnienie naturalnie rozni sie w ciagu dnia.
  html += "<div class='stats-grid' style='grid-template-columns:repeat(2,1fr)'>";
  html += "<div class='stat-box'><div class='stat-label'>RANO Ø (SYS/DIA)</div><div class='stat-value' style='font-size:18px' id='avgMorning'>–</div></div>";
  html += "<div class='stat-box'><div class='stat-label'>WIECZÓR Ø (SYS/DIA)</div><div class='stat-value' style='font-size:18px' id='avgEvening'>–</div></div>";
  html += "</div>";
  html += "<div class='stat-box' style='margin-top:10px'><div class='stat-label'>BEWERTUNG</div>";
  html += "<div class='stat-value' style='color:" + avgColor + "' id='avgCategory'>" + avgText + "</div></div></div>";
  
  html += "<div class='card'><div class='card-title'>➕ NEUER MESSWERT</div>";
  html += "<div class='input-group'>";
  html += "<input type='number' id='sys' placeholder='Systolisch'>";
  html += "<input type='number' id='dia' placeholder='Diastolisch'>";
  html += "<input type='number' id='pulse' placeholder='Puls'>";
  html += "</div>";
  html += "<button onclick='addMeasurement()'>💾 SPEICHERN</button></div>";
  
  html += "<div class='card'><div class='card-title'>📈 TREND (letzte 20 Messungen)</div>";
  html += "<canvas id='trendChart'></canvas></div>";

  html += "<div class='card'><div class='card-title'>📈 DŁUGOTERMINOWY TREND (średnie miesięczne + linia trendu)</div>";
  html += "<canvas id='longTermChart'></canvas></div>";

  html += "<div class='card'><div class='card-title'>📅 KALENDER (Farben = Tagesschwerpunkt)</div>";
  html += "<div id='calendar'></div>";
  html += "<div class='legend' style='margin-top:10px'>";
  html += "<div class='legend-item' style='background:#F44336'>🔴 HYPERTONIE 2-3</div>";
  html += "<div class='legend-item' style='background:#FF9800'>🟠 HYPERTONIE 1</div>";
  html += "<div class='legend-item' style='background:#FFC107'>🟡 HOCH NORMAL</div>";
  html += "<div class='legend-item' style='background:#2196F3'>🔵 NORMAL</div>";
  html += "<div class='legend-item' style='background:#4CAF50'>🟢 OPTIMAL</div>";
  html += "<div class='legend-item' style='background:#8B4513'>🟤 HYPOTONIE</div>";
  html += "</div></div>";
  
  html += "<div class='card'><div class='card-title'>📋 VERLAUF</div>";
  html += "<div class='measurement-list' id='list'></div>";
  html += "<div class='pagination' id='pagination'></div>";
  html += "<div class='page-info' id='pageInfo'></div>";
  html += "<button class='delete-btn' onclick='exportCSV()' style='background:#2196F3;margin-top:15px;width:100%'>📥 EXPORT CSV</button>";
  html += "<button class='delete-btn' onclick='pokazRaport()' style='background:#9C27B0;margin-top:10px;width:100%'>📅 RAPORT MIESIĘCZNY</button>";
  html += "<button class='delete-btn' onclick='document.getElementById(\"importFile\").click()' style='background:#4CAF50;margin-top:10px;width:100%'>📤 IMPORT CSV</button>";
  html += "<input type='file' id='importFile' accept='.csv' style='display:none' onchange='doImport()'>";
  html += "</div>";
  html += "<div class='footer'>ESP32 | Blutdruck-Tagebuch</div>";
  html += "</div>";
  
  html += "<script>";
  html += "let measurements=[];let chart=null;let longTermChart=null;let currentPage=1;const itemsPerPage=10;";
  html += "let currentCalendarYear=new Date().getFullYear();let currentCalendarMonth=new Date().getMonth();";
  html += "function getCalendarColor(sys,dia){if(sys<90&&dia<60)return '#8B4513';if(sys>=160||dia>=100)return '#F44336';if(sys>=140||dia>=90)return '#FF9800';if(sys>=130||dia>=85)return '#FFC107';if(sys>=120||dia>=80)return '#2196F3';return '#4CAF50';}";
  html += "function getCategory(sys,dia){if(sys<90&&dia<60)return 'HYPOTONIE';if(sys>=180||dia>=110)return 'HYPERTONIE 3';if(sys>=160||dia>=100)return 'HYPERTONIE 2';if(sys>=140||dia>=90)return 'HYPERTONIE 1';if(sys>=130||dia>=85)return 'HOCH NORMAL';if(sys>=120||dia>=80)return 'NORMAL';return 'OPTIMAL';}";
  html += "function loadData(){fetch('/cisnienie/list').then(r=>r.json()).then(data=>{measurements=data;updateStats();updateChart();updateLongTermChart();updateCalendar();updateList();});}";
  html += "function updateStats(){if(measurements.length===0)return;let sumSys=0,sumDia=0,sumPulse=0,minSys=300,maxSys=0,minDia=300,maxDia=0;let sumSysM=0,sumDiaM=0,countM=0,sumSysE=0,sumDiaE=0,countE=0;measurements.forEach(m=>{sumSys+=m.sys;sumDia+=m.dia;sumPulse+=m.pulse;if(m.sys<minSys)minSys=m.sys;if(m.sys>maxSys)maxSys=m.sys;if(m.dia<minDia)minDia=m.dia;if(m.dia>maxDia)maxDia=m.dia;let h=parseInt(m.time.split(':')[0],10);if(h<12){sumSysM+=m.sys;sumDiaM+=m.dia;countM++;}else{sumSysE+=m.sys;sumDiaE+=m.dia;countE++;}});document.getElementById('avgSys').innerHTML=(sumSys/measurements.length).toFixed(0);document.getElementById('avgDia').innerHTML=(sumDia/measurements.length).toFixed(0);document.getElementById('avgPulse').innerHTML=(sumPulse/measurements.length).toFixed(0);document.getElementById('minMaxSys').innerHTML=minSys+'/'+maxSys;document.getElementById('minMaxDia').innerHTML=minDia+'/'+maxDia;document.getElementById('count').innerHTML=measurements.length;let avgSys=Math.round(sumSys/measurements.length);let avgDia=Math.round(sumDia/measurements.length);document.getElementById('avgCategory').innerHTML=getCategory(avgSys,avgDia);document.getElementById('avgMorning').innerHTML=countM>0?(Math.round(sumSysM/countM)+'/'+Math.round(sumDiaM/countM)):'–';document.getElementById('avgEvening').innerHTML=countE>0?(Math.round(sumSysE/countE)+'/'+Math.round(sumDiaE/countE)):'–';}";
  html += "function updateChart(){const last20=measurements.slice(-20);const ctx=document.getElementById('trendChart').getContext('2d');if(chart)chart.destroy();if(last20.length===0)return;chart=new Chart(ctx,{type:'line',data:{labels:last20.map((_,i)=>i+1),datasets:[{label:'Systolisch',data:last20.map(m=>m.sys),borderColor:'#F44336',backgroundColor:'rgba(244,67,54,0.1)',fill:true,tension:0.3},{label:'Diastolisch',data:last20.map(m=>m.dia),borderColor:'#2196F3',backgroundColor:'rgba(33,150,243,0.1)',fill:true,tension:0.3}]},options:{responsive:true,maintainAspectRatio:true,plugins:{legend:{labels:{color:'#eee'}}},scales:{y:{grid:{color:'rgba(255,255,255,0.1)'},title:{display:true,text:'mmHg',color:'#aaa'}},x:{grid:{color:'rgba(255,255,255,0.1)'},title:{display:true,text:'Messung Nr.',color:'#aaa'}}}}});}";
  html += "function linReg(values){const n=values.length;if(n<2)return null;let sumX=0,sumY=0,sumXY=0,sumXX=0;for(let i=0;i<n;i++){sumX+=i;sumY+=values[i];sumXY+=i*values[i];sumXX+=i*i;}const denom=(n*sumXX-sumX*sumX);if(denom===0)return null;const slope=(n*sumXY-sumX*sumY)/denom;const intercept=(sumY-slope*sumX)/n;return {slope,intercept};}";
  html += "function updateLongTermChart(){let byMonth={};measurements.forEach(m=>{let key=m.year+'-'+String(m.month).padStart(2,'0');if(!byMonth[key])byMonth[key]={sumSys:0,sumDia:0,count:0};byMonth[key].sumSys+=m.sys;byMonth[key].sumDia+=m.dia;byMonth[key].count++;});let keys=Object.keys(byMonth).sort();const ctx=document.getElementById('longTermChart').getContext('2d');if(longTermChart)longTermChart.destroy();if(keys.length===0)return;let avgSysArr=keys.map(k=>byMonth[k].sumSys/byMonth[k].count);let avgDiaArr=keys.map(k=>byMonth[k].sumDia/byMonth[k].count);let trend=linReg(avgSysArr);let trendLine=trend?avgSysArr.map((_,i)=>trend.intercept+trend.slope*i):null;let datasets=[{label:'Systolisch (śr. mies.)',data:avgSysArr,borderColor:'#F44336',backgroundColor:'rgba(244,67,54,0.1)',fill:false,tension:0.2},{label:'Diastolisch (śr. mies.)',data:avgDiaArr,borderColor:'#2196F3',backgroundColor:'rgba(33,150,243,0.1)',fill:false,tension:0.2}];if(trendLine)datasets.push({label:'Linia trendu (SYS)',data:trendLine,borderColor:'#FFC107',borderDash:[6,4],pointRadius:0,fill:false});longTermChart=new Chart(ctx,{type:'line',data:{labels:keys,datasets:datasets},options:{responsive:true,maintainAspectRatio:true,plugins:{legend:{labels:{color:'#eee'}}},scales:{y:{grid:{color:'rgba(255,255,255,0.1)'},title:{display:true,text:'mmHg',color:'#aaa'}},x:{grid:{color:'rgba(255,255,255,0.1)'},title:{display:true,text:'Miesiąc',color:'#aaa'}}}}});}";
  html += "function updateCalendar(){let dayColors={};let colorOrder=['#F44336','#FF9800','#FFC107','#2196F3','#4CAF50','#8B4513'];measurements.forEach(m=>{let key=m.year+'-'+m.month+'-'+m.day;let col=getCalendarColor(m.sys,m.dia);if(!dayColors[key]){dayColors[key]=col;}else{let ci=colorOrder.indexOf(dayColors[key]);let ni=colorOrder.indexOf(col);if(ni<ci&&ni!=-1)dayColors[key]=col;}});let firstDay=new Date(currentCalendarYear,currentCalendarMonth,1).getDay();let daysInMonth=new Date(currentCalendarYear,currentCalendarMonth+1,0).getDate();let monthNames=['Januar','Februar','März','April','Mai','Juni','Juli','August','September','Oktober','November','Dezember'];let weekdays=['Mo','Di','Mi','Do','Fr','Sa','So'];let html='<div style=\"display:flex;justify-content:space-between;align-items:center;margin-bottom:15px;\"><button onclick=\"prevMonth()\" style=\"width:40px;background:#2196F3;padding:8px;border:none;border-radius:20px;cursor:pointer;color:white\">◀</button><span style=\"font-size:18px;font-weight:bold;\">'+monthNames[currentCalendarMonth]+' '+currentCalendarYear+'</span><button onclick=\"nextMonth()\" style=\"width:40px;background:#2196F3;padding:8px;border:none;border-radius:20px;cursor:pointer;color:white\">▶</button></div><div class=\"cal-weekday\">'+weekdays.join('</div><div class=\"cal-weekday\">')+'</div><div class=\"calendar\">';let startOffset=(firstDay+6)%7;for(let i=0;i<startOffset;i++){html+='<div class=\"cal-day empty\"></div>';}for(let d=1;d<=daysInMonth;d++){let key=currentCalendarYear+'-'+(currentCalendarMonth+1)+'-'+d;let col=dayColors[key]||'rgba(255,255,255,0.1)';html+='<div class=\"cal-day\" style=\"background:'+col+'\" onclick=\"filterByDate('+currentCalendarYear+','+(currentCalendarMonth+1)+','+d+')\">'+d+'</div>';}html+='</div>';document.getElementById('calendar').innerHTML=html;}";
  html += "function prevMonth(){currentCalendarMonth--;if(currentCalendarMonth<0){currentCalendarMonth=11;currentCalendarYear--;}updateCalendar();}";
  html += "function nextMonth(){currentCalendarMonth++;if(currentCalendarMonth>11){currentCalendarMonth=0;currentCalendarYear++;}updateCalendar();}";
  html += "function filterByDate(year,month,day){let filtered=measurements.filter(m=>m.year==year&&m.month==month&&m.day==day);if(filtered.length===0){alert('Keine Messungen an diesem Tag');return;}let msg='📅 '+day+'.'+month+'.'+year+'\\n\\n';filtered.forEach(m=>{msg+=m.sys+'/'+m.dia+' ('+m.pulse+') um '+m.time+'\\n';});alert(msg);}";
  html += "function updateList(){let totalPages=Math.ceil(measurements.length/itemsPerPage);if(totalPages===0)totalPages=1;if(currentPage>totalPages)currentPage=totalPages;let start=(currentPage-1)*itemsPerPage;let end=start+itemsPerPage;let pageItems=measurements.slice().reverse().slice(start,end);let html='';if(measurements.length===0){html='<div style=\"text-align:center;padding:30px\">📭 Keine Messungen</div>';}pageItems.forEach(m=>{html+=`<div class='measurement-item'><div><div class='measurement-values'>${m.sys}/${m.dia} (${m.pulse})</div><div class='measurement-date'>${m.date} ${m.time}</div></div><button class='delete-btn' onclick='deleteMeasurement(${m.id})'>🗑️</button></div>`;});document.getElementById('list').innerHTML=html;let pag='';for(let i=1;i<=totalPages;i++){pag+=`<button class='delete-btn' style='background:${i===currentPage?'#4CAF50':'#666'}' onclick='goToPage(${i})'>${i}</button>`;}document.getElementById('pagination').innerHTML=pag;document.getElementById('pageInfo').innerHTML='Seite '+currentPage+' von '+totalPages+' ('+measurements.length+' Einträge)';}";
  html += "function goToPage(page){currentPage=page;updateList();}";
  html += "function addMeasurement(){const s=parseInt(document.getElementById('sys').value);const d=parseInt(document.getElementById('dia').value);const p=parseInt(document.getElementById('pulse').value);if(!s||!d||!p){alert('Bitte alle Werte eingeben!');return;}if(s<50||s>250){alert('Systolisch 50-250');return;}if(d<30||d>150){alert('Diastolisch 30-150');return;}if(p<30||p>200){alert('Puls 30-200');return;}fetch('/cisnienie/add?sys='+s+'&dia='+d+'&pulse='+p).then(r=>r.text().then(t=>({ok:r.ok,text:t}))).then(res=>{if(!res.ok){alert(res.text);return;}document.getElementById('sys').value='';document.getElementById('dia').value='';document.getElementById('pulse').value='';loadData();});}";
  html += "function deleteMeasurement(id){if(confirm('Messung löschen?')){fetch('/cisnienie/delete?id='+id).then(()=>loadData());}}";
  html += "function exportCSV(){let csv='Data;Godzina;Systolisch;Diastolisch;Puls;Bewertung\\n';measurements.forEach(m=>{let bew=getCategory(m.sys,m.dia);csv+=m.date+';'+m.time+';'+m.sys+';'+m.dia+';'+m.pulse+';'+bew+'\\n';});const blob=new Blob([csv],{type:'text/csv'});const link=document.createElement('a');link.href=URL.createObjectURL(blob);link.download='blutdruck.csv';link.click();}";
  html += "function doImport(){let file=document.getElementById('importFile').files[0];if(!file)return;let reader=new FileReader();reader.onload=function(e){fetch('/cisnienie/import',{method:'POST',body:'data='+encodeURIComponent(e.target.result),headers:{'Content-Type':'application/x-www-form-urlencoded'}}).then(r=>r.text()).then(msg=>{alert(msg);loadData();});};reader.readAsText(file);}";
  html += "loadData();setInterval(loadData,30000);";
  html += "function pokazRaport() {";
  html += "let now = new Date();";
  html += "let rok = now.getFullYear();";
  html += "let miesiac = now.getMonth() + 1;";
  html += "let url = '/cisnienie/raport?rok=' + rok + '&miesiac=' + miesiac;";
  html += "window.open(url, '_blank');";
  html += "}";
  html += "</script></body></html>";
  server.send(200, "text/html", html);
}

void handleCisnienieRaportMiesieczny() {
  if (!server.hasArg("rok") || !server.hasArg("miesiac")) {
    server.send(400, "text/plain", "Brak parametrów: rok i miesiac");
    return;
  }
  
  int rok = server.arg("rok").toInt();
  int miesiac = server.arg("miesiac").toInt();

  // NAPRAWA: prawdziwa aktualna data/godzina (z NTP) zamiast __DATE__/__TIME__,
  // ktore pokazywalyby zawsze date SKOMPILOWANIA firmware'u, a nie date raportu.
  struct tm generatedTimeinfo;
  String generatedStr = "brak zsynchronizowanego czasu";
  if (getLocalTime(&generatedTimeinfo, 1000)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d %02d:%02d:%02d",
             generatedTimeinfo.tm_mday, generatedTimeinfo.tm_mon + 1, generatedTimeinfo.tm_year + 1900,
             generatedTimeinfo.tm_hour, generatedTimeinfo.tm_min, generatedTimeinfo.tm_sec);
    generatedStr = String(buf);
  }

  // Formatuj miesiąc z zerem (01-12)
  String miesiacStr = (miesiac < 10) ? "0" + String(miesiac) : String(miesiac);
  
  // Ostatni dzień miesiąca
  int dniWMiesiacu;
  if (miesiac == 2) {
    dniWMiesiacu = ((rok % 4 == 0 && rok % 100 != 0) || rok % 400 == 0) ? 29 : 28;
  } else if (miesiac == 4 || miesiac == 6 || miesiac == 9 || miesiac == 11) {
    dniWMiesiacu = 30;
  } else {
    dniWMiesiacu = 31;
  }
  
  // Zbierz dane z całego miesiąca
  float dzienneSrednieSys[32];
  float dzienneSrednieDia[32];
  float dzienneSredniePulse[32];
  int dzienneLiczby[32];
  
  for (int d = 1; d <= dniWMiesiacu; d++) {
    dzienneSrednieSys[d] = 0;
    dzienneSrednieDia[d] = 0;
    dzienneSredniePulse[d] = 0;
    dzienneLiczby[d] = 0;
  }
  
  // Przejdź przez wszystkie pomiary
  for (int i = 0; i < measureCount; i++) {
    if (measurements[i].year == rok && measurements[i].month == miesiac) {
      int dzien = measurements[i].day;
      dzienneSrednieSys[dzien] += measurements[i].systolic;
      dzienneSrednieDia[dzien] += measurements[i].diastolic;
      dzienneSredniePulse[dzien] += measurements[i].pulse;
      dzienneLiczby[dzien]++;
    }
  }
  
  // Oblicz średnie
  for (int d = 1; d <= dniWMiesiacu; d++) {
    if (dzienneLiczby[d] > 0) {
      dzienneSrednieSys[d] /= dzienneLiczby[d];
      dzienneSrednieDia[d] /= dzienneLiczby[d];
      dzienneSredniePulse[d] /= dzienneLiczby[d];
    }
  }
  
  // Oblicz średnią miesięczną
  float sumaSys = 0, sumaDia = 0, sumaPulse = 0;
  int dniZPomiarami = 0;
  for (int d = 1; d <= dniWMiesiacu; d++) {
    if (dzienneLiczby[d] > 0) {
      sumaSys += dzienneSrednieSys[d];
      sumaDia += dzienneSrednieDia[d];
      sumaPulse += dzienneSredniePulse[d];
      dniZPomiarami++;
    }
  }
  float sredniaSys = (dniZPomiarami > 0) ? sumaSys / dniZPomiarami : 0;
  float sredniaDia = (dniZPomiarami > 0) ? sumaDia / dniZPomiarami : 0;
  float sredniaPulse = (dniZPomiarami > 0) ? sumaPulse / dniZPomiarami : 0;
  
  // Funkcja do klasyfikacji ciśnienia
  // NAPRAWA: ta sama kolejnosc warunkow co w getCategoryText(), zeby raport
  // miesieczny klasyfikowal pomiary tak samo jak reszta aplikacji.
  auto getKategoria = [](float sys, float dia) -> String {
    if (sys < 90 && dia < 60) return "HYPOTONIE";
    if (sys >= 160 || dia >= 100) return "HYPERTONIE 2-3";
    if (sys >= 140 || dia >= 90) return "HYPERTONIE 1";
    if (sys >= 130 || dia >= 85) return "HOCH NORMAL";
    if (sys >= 120 || dia >= 80) return "NORMAL";
    return "OPTIMAL";
  };
  
  // Generuj HTML do druku
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><link rel='icon' type='image/svg+xml' href='data:image/svg+xml,%3Csvg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"%3E%3Crect width=\"100\" height=\"100\" rx=\"20\" fill=\"%231a1a2e\"/%3E%3Ccircle cx=\"50\" cy=\"45\" r=\"25\" fill=\"%236c63ff\"/%3E%3Ctext x=\"50\" y=\"60\" text-anchor=\"middle\" fill=\"white\" font-size=\"35\"%3E🩺%3C/text%3E%3C/svg%3E'>";
  html += "<title>Raport ciśnienia - " + String(rok) + "-" + miesiacStr + "</title>";
  // NOWE: eksport do PDF robimy w przegladarce (jsPDF + jspdf-autotable z CDN),
  // a nie na ESP32 - generowanie PDF na mikrokontrolerze bez wsparcia
  // biblioteki byloby bardzo ciezkie/niepraktyczne przy ograniczonej pamieci.
  html += "<script src='https://cdn.jsdelivr.net/npm/jspdf@2.5.1/dist/jspdf.umd.min.js'></script>";
  html += "<script src='https://cdn.jsdelivr.net/npm/jspdf-autotable@3.8.2/dist/jspdf-autotable.min.js'></script>";
  html += "<style>";
  html += "body{font-family:Arial;padding:20px}";
  html += "h1{text-align:center;color:#333}";
  html += "h2{text-align:center;color:#666}";
  html += "table{width:100%;border-collapse:collapse;margin-top:20px}";
  html += "th,td{border:1px solid #ccc;padding:10px;text-align:center}";
  html += "th{background:#4CAF50;color:white}";
  html += "tr:nth-child(even){background:#f9f9f9}";
  html += ".suma{background:#2196F3;color:white;font-weight:bold}";
  html += ".opt{background:#4CAF50;color:white}";
  html += ".hyp1{background:#FF9800;color:white}";
  html += ".hyp2{background:#F44336;color:white}";
  html += "@media print{body{margin:0;padding:0}button{display:none}}";
  html += "</style>";
  html += "<body>";
  html += "<button onclick='window.print()' style='padding:10px 20px;margin-bottom:20px;margin-right:10px;cursor:pointer'>🖨️ DRUKUJ</button>";
  html += "<button onclick='pobierzPDF()' style='padding:10px 20px;margin-bottom:20px;cursor:pointer'>📄 POBIERZ PDF</button>";
  html += "<h1>🩺 RAPORT MIESIĘCZNY CIŚNIENIA</h1>";
  html += "<h2>" + String(rok) + "-" + miesiacStr + "</h2>";

  // Tabela podsumowująca miesiąc
  html += "<table id='summaryTable' style='margin-bottom:20px'>";
  html += "<tr class='suma'><th colspan='2'>📊 PODSUMOWANIE MIESIĄCA</th></tr>";
  html += "<tr><td><strong>Średnia SYSTOLICZNA</strong></td><td>" + String(sredniaSys, 1) + " mmHg</td></tr>";
  html += "<tr><td><strong>Średnia DIASTOLICZNA</strong></td><td>" + String(sredniaDia, 1) + " mmHg</td></tr>";
  html += "<tr><td><strong>Średnia PULS</strong></td><td>" + String(sredniaPulse, 1) + " bpm</td></tr>";
  html += "<tr><td><strong>OGÓLNA OCENA</strong></td><td>" + getKategoria(sredniaSys, sredniaDia) + "</td></tr>";
  html += "<tr><td><strong>Dni z pomiarami</strong></td><td>" + String(dniZPomiarami) + " / " + String(dniWMiesiacu) + "</td></tr>";
  html += "</table>";
  
  // Tabela dzienna
  html += "<table id='dailyTable'>";
  html += "<tr><th>Dzień</th><th>Data</th><th>Śr. SYSTOLICZNE</th><th>Śr. DIASTOLICZNE</th><th>Śr. PULS</th><th>OCENA</th></tr>";
  
  for (int d = 1; d <= dniWMiesiacu; d++) {
    String data = String(rok) + "-" + miesiacStr + "-" + (d < 10 ? "0" + String(d) : String(d));
    float sys = dzienneSrednieSys[d];
    float dia = dzienneSrednieDia[d];
    float pulse = dzienneSredniePulse[d];
    int liczba = dzienneLiczby[d];
    
    String kategoria = getKategoria(sys, dia);
    String kolorKlasa = "";
    if (kategoria == "OPTIMAL") kolorKlasa = "opt";
    else if (kategoria == "HYPERTONIE 1") kolorKlasa = "hyp1";
    else if (kategoria == "HYPERTONIE 2-3") kolorKlasa = "hyp2";
    
    html += "<tr>";
    html += "<td>" + String(d) + "</td>";
    html += "<td>" + data + "</td>";
    if (liczba > 0) {
      html += "<td><strong>" + String(sys, 0) + "</strong></td>";
      html += "<td><strong>" + String(dia, 0) + "</strong></td>";
      html += "<td>" + String(pulse, 0) + "</td>";
      html += "<td class='" + kolorKlasa + "'><strong>" + kategoria + "</strong></td>";
    } else {
      html += "<td colspan='4' style='color:#999'>--- brak pomiarów ---</td>";
    }
    html += "</tr>";
  }
  
  html += "</table>";
  html += "<p style='margin-top:20px;text-align:center;color:#666'>Wygenerowano: " + generatedStr + "</p>";
  html += "<script>";
  html += "function pobierzPDF(){const { jsPDF } = window.jspdf;const doc=new jsPDF();doc.setFontSize(14);doc.text('Raport cisnienia - " + String(rok) + "-" + miesiacStr + "', 14, 15);doc.autoTable({html:'#summaryTable', startY:20});doc.autoTable({html:'#dailyTable'});doc.save('raport_cisnienia_" + String(rok) + "_" + miesiacStr + ".pdf');}";
  html += "</script>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}



// ============================================================
// CZĘŚĆ 3: VORRATSKAMMER (routes: /vorratskammer, /vorratskammer/...)
// ============================================================
// ==================== DATEN ====================
struct PantryItem {
  int id;
  String name;
  String category;
  float qty;
  String unit;
  float minQty;   // Mindestbestand-Schwelle (0 = deaktiviert)
  int expYear, expMonth, expDay; // 0,0,0 = kein Ablaufdatum
  String notes;
};

// HINWEIS: Jeder PantryItem hat 4 Felder vom Typ String (name, category, unit, notes).
// Die String-Klasse belegt auf dem ESP32 ~16 Byte pro Feld, ein Produkt braucht also ca. 88 Byte.
// Bei MAX_ITEMS=1000 waere das gesamte Array ~86 KB statischer DRAM - das ist zu viel
// (der Linker meldete "DRAM segment data does not fit" / region overflowed).
// NAPRAWA (po polaczeniu 3 programow w 1): zmniejszone z 300 do 220, zeby razem
// z MAX_MEASUREMENTS (Cisnienie) zrobic wystarczajaco duzo miejsca w RAM dla
// wszystkich 3 programow dzialajacych teraz w jednym szkicu.
// 220 produktow to nadal wiecej, niz realistyczna szafka/spizarnia potrzebuje.
const int MAX_ITEMS = 220;
PantryItem items[MAX_ITEMS];
int itemCount = 0;
int nextItemId = 1;

// entfernt Zeichen, die das einfache CSV-Format stoeren (Semikolon, Zeilenumbruch)
String sanitizeField(String s) {
  s.replace(";", ",");
  s.replace("\n", " ");
  s.replace("\r", " ");
  s.trim();
  return s;
}

// escaped Sonderzeichen, damit der String sicher in JSON eingefuegt werden kann
// (ohne das wuerden Name/Kategorie/Notizen mit " oder \ das ganze JSON kaputt machen,
// und die Liste liesse sich nicht mehr laden)
String jsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 4);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((unsigned char)c < 0x20) {
          // uebrige Steuerzeichen werden ausgelassen, um JSON nicht zu brechen
        } else {
          out += c;
        }
    }
  }
  return out;
}

void saveItems() {
  File f = SD.open("/vorratskammer.txt", FILE_WRITE);
  if (!f) return;
  f.println(itemCount);
  for (int i = 0; i < itemCount; i++) {
    f.print(items[i].id); f.print(";");
    f.print(items[i].name); f.print(";");
    f.print(items[i].category); f.print(";");
    f.print(items[i].qty, 2); f.print(";");
    f.print(items[i].unit); f.print(";");
    f.print(items[i].minQty, 2); f.print(";");
    f.print(items[i].expYear); f.print(";");
    f.print(items[i].expMonth); f.print(";");
    f.print(items[i].expDay); f.print(";");
    f.println(items[i].notes);
  }
  f.close();
}

void loadItems() {
  itemCount = 0;
  nextItemId = 1;
  if (!SD.exists("/vorratskammer.txt")) return;
  File f = SD.open("/vorratskammer.txt", FILE_READ);
  if (!f) return;
  int count = f.parseInt();
  f.readStringUntil('\n');
  if (count > MAX_ITEMS) count = MAX_ITEMS;
  for (int i = 0; i < count; i++) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) { i--; count--; if (count < 0) break; continue; }
    String felder[10];
    int fi = 0, start = 0;
    for (int c = 0; c <= line.length() && fi < 10; c++) {
      if (c == line.length() || line.charAt(c) == ';') {
        felder[fi++] = line.substring(start, c);
        start = c + 1;
      }
    }
    if (fi < 9) continue;
    items[itemCount].id = felder[0].toInt();
    items[itemCount].name = felder[1];
    items[itemCount].category = felder[2];
    items[itemCount].qty = felder[3].toFloat();
    items[itemCount].unit = felder[4];
    items[itemCount].minQty = felder[5].toFloat();
    items[itemCount].expYear = felder[6].toInt();
    items[itemCount].expMonth = felder[7].toInt();
    items[itemCount].expDay = felder[8].toInt();
    items[itemCount].notes = (fi > 9) ? felder[9] : "";
    itemCount++;
  }
  f.close();
  nextItemId = 1;
  for (int i = 0; i < itemCount; i++) {
    if (items[i].id >= nextItemId) nextItemId = items[i].id + 1;
  }
}

// ==================== DATUM / STATUS ====================
long daysBetween(int y1, int m1, int d1, int y2, int m2, int d2) {
  struct tm t1 = {0}; t1.tm_year = y1 - 1900; t1.tm_mon = m1 - 1; t1.tm_mday = d1; t1.tm_hour = 12; t1.tm_isdst = -1;
  struct tm t2 = {0}; t2.tm_year = y2 - 1900; t2.tm_mon = m2 - 1; t2.tm_mday = d2; t2.tm_hour = 12; t2.tm_isdst = -1;
  time_t tt1 = mktime(&t1), tt2 = mktime(&t2);
  return (long)round(difftime(tt2, tt1) / 86400.0);
}

void getToday(int &y, int &m, int &d) {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 200)) {
    y = timeinfo.tm_year + 1900; m = timeinfo.tm_mon + 1; d = timeinfo.tm_mday;
  } else {
    y = 2026; m = 1; d = 1; // sicherer Fallback, wenn die Zeit noch nicht synchronisiert ist
  }
}

// Status: 0=OK(gruen) 1=niedriger Bestand(gelb) 2=laeuft bald ab(orange) 3=abgelaufen(rot)
int itemStatus(const PantryItem &it, int ty, int tm_, int td) {
  if (it.expYear > 0) {
    long diff = daysBetween(ty, tm_, td, it.expYear, it.expMonth, it.expDay);
    if (diff < 0) return 3;
    if (diff <= 7) return 2;
  }
  if (it.minQty > 0 && it.qty <= it.minQty) return 1;
  return 0;
}

const char* STATUS_LABEL[] = {"OK", "Niedriger Bestand", "Laeuft bald ab", "Abgelaufen"};
const char* STATUS_BG[]    = {"#dcfce7", "#fef9c3", "#ffedd5", "#fee2e2"};
const char* STATUS_FG[]    = {"#166534", "#854d0e", "#9a3412", "#991b1b"};

// ==================== HANDLER ====================
void handleVorratskammerList() {
  int ty, tm_, td;
  getToday(ty, tm_, td);
  String json = "[";
  for (int i = 0; i < itemCount; i++) {
    int st = itemStatus(items[i], ty, tm_, td);
    if (i > 0) json += ",";
    json += "{\"id\":" + String(items[i].id) + ",";
    json += "\"name\":\"" + jsonEscape(items[i].name) + "\",";
    json += "\"category\":\"" + jsonEscape(items[i].category) + "\",";
    json += "\"qty\":" + String(items[i].qty, 2) + ",";
    json += "\"unit\":\"" + jsonEscape(items[i].unit) + "\",";
    json += "\"minQty\":" + String(items[i].minQty, 2) + ",";
    json += "\"expYear\":" + String(items[i].expYear) + ",";
    json += "\"expMonth\":" + String(items[i].expMonth) + ",";
    json += "\"expDay\":" + String(items[i].expDay) + ",";
    json += "\"notes\":\"" + jsonEscape(items[i].notes) + "\",";
    json += "\"status\":" + String(st) + ",";
    json += "\"statusLabel\":\"" + String(STATUS_LABEL[st]) + "\"}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleVorratskammerAdd() {
  if (!server.hasArg("name")) { server.send(400, "text/plain", "Kein Name angegeben"); return; }

  int id = server.hasArg("id") ? server.arg("id").toInt() : 0;
  int idx = -1;
  for (int i = 0; i < itemCount; i++) if (items[i].id == id) { idx = i; break; }
  if (idx == -1) {
    if (itemCount >= MAX_ITEMS) { server.send(400, "text/plain", "Produktlimit erreicht"); return; }
    idx = itemCount++;
    items[idx].id = nextItemId++;
  }

  items[idx].name = sanitizeField(server.arg("name"));
  items[idx].category = sanitizeField(server.arg("category"));
  items[idx].qty = server.arg("qty").toFloat();
  items[idx].unit = sanitizeField(server.arg("unit"));
  items[idx].minQty = server.hasArg("minQty") ? server.arg("minQty").toFloat() : 0;
  items[idx].expYear = server.hasArg("expYear") ? server.arg("expYear").toInt() : 0;
  items[idx].expMonth = server.hasArg("expMonth") ? server.arg("expMonth").toInt() : 0;
  items[idx].expDay = server.hasArg("expDay") ? server.arg("expDay").toInt() : 0;
  items[idx].notes = sanitizeField(server.arg("notes"));

  saveItems();
  server.send(200, "text/plain", "OK");
}

void handleVorratskammerDelete() {
  if (!server.hasArg("id")) { server.send(400, "text/plain", "Keine ID"); return; }
  int id = server.arg("id").toInt();
  for (int i = 0; i < itemCount; i++) {
    if (items[i].id == id) {
      for (int j = i; j < itemCount - 1; j++) items[j] = items[j + 1];
      itemCount--;
      saveItems();
      server.send(200, "text/plain", "OK");
      return;
    }
  }
  server.send(404, "text/plain", "Nicht gefunden");
}

void handleVorratskammerExport() {
  String csv = "ID;Name;Kategorie;Menge;Einheit;Mindestbestand;Ablaufjahr;Ablaufmonat;Ablauftag;Notizen\n";
  for (int i = 0; i < itemCount; i++) {
    csv += String(items[i].id) + ";" + items[i].name + ";" + items[i].category + ";" +
           String(items[i].qty, 2) + ";" + items[i].unit + ";" + String(items[i].minQty, 2) + ";" +
           String(items[i].expYear) + ";" + String(items[i].expMonth) + ";" + String(items[i].expDay) + ";" +
           items[i].notes + "\n";
  }
  server.sendHeader("Content-Disposition", "attachment; filename=vorratskammer.csv");
  server.send(200, "text/csv", csv);
}

void handleVorratskammerImport() {
  if (!server.hasArg("data")) { server.send(400, "text/plain", "Keine Daten"); return; }
  String csvData = server.arg("data");
  int imported = 0;
  int startIdx = csvData.indexOf('\n');
  if (startIdx > 0) startIdx++; else startIdx = 0;

  // die Schleife arbeitet Datenzeilen ab; die letzte Zeile hat evtl. kein abschliessendes \n
  // (z.B. eine CSV-Datei ohne Leerzeile am Ende), deshalb wird sie nach der Schleife
  // separat behandelt, statt sie stillschweigend zu verlieren
  while (startIdx < (int)csvData.length() && itemCount < MAX_ITEMS) {
    int endIdx = csvData.indexOf('\n', startIdx);
    bool lastChunk = (endIdx < 0);
    String line = lastChunk ? csvData.substring(startIdx) : csvData.substring(startIdx, endIdx);
    startIdx = lastChunk ? csvData.length() : endIdx + 1;
    line.trim();
    if (line.length() == 0) continue;

    String felder[10];
    int fi = 0, start = 0;
    for (int c = 0; c <= line.length() && fi < 10; c++) {
      if (c == line.length() || line.charAt(c) == ';') {
        felder[fi++] = line.substring(start, c);
        start = c + 1;
      }
    }
    if (fi < 5 || felder[1].length() == 0) continue;

    items[itemCount].id = nextItemId++;
    items[itemCount].name = sanitizeField(felder[1]);
    items[itemCount].category = sanitizeField(felder[2]);
    items[itemCount].qty = felder[3].toFloat();
    items[itemCount].unit = sanitizeField(felder[4]);
    items[itemCount].minQty = (fi > 5) ? felder[5].toFloat() : 0;
    items[itemCount].expYear = (fi > 6) ? felder[6].toInt() : 0;
    items[itemCount].expMonth = (fi > 7) ? felder[7].toInt() : 0;
    items[itemCount].expDay = (fi > 8) ? felder[8].toInt() : 0;
    items[itemCount].notes = (fi > 9) ? sanitizeField(felder[9]) : "";
    itemCount++;
    imported++;
  }
  saveItems();
  server.send(200, "text/plain", "OK Importiert: " + String(imported) + " Produkte");
}

void handleVorratskammer() {
  String html = R"rawliteral(
<!DOCTYPE html><html lang='de'><head><meta charset='UTF-8'>
<link rel="icon" type="image/png" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Ctext x='0' y='20' font-size='20' fill='white'%3E🗓️%3C/text%3E%3C/svg%3E">
<meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<title>🗓️ Vorratskammer</title>
<script>
// wird VOR dem CSS ausgefuehrt, damit beim Laden kein kurzes Aufblitzen
// des hellen Designs zu sehen ist, wenn eigentlich Dunkel gespeichert ist
(function() {
  try {
    var saved = localStorage.getItem('vorratskammerTheme');
    var dark = saved ? (saved === 'dark') : (window.matchMedia && window.matchMedia('(prefers-color-scheme: dark)').matches);
    if (dark) document.documentElement.classList.add('dark');
  } catch (e) {}
})();
</script>
<style>
:root{
  --bg:#fafafa; --text:#222; --text-muted:#777;
  --card-bg:#fff; --card-border:#ddd; --label-color:#666;
  --input-bg:#fff; --input-border:#ccc; --input-text:#222;
  --btn-bg:#111; --btn-text:#fff; --btn-secondary-bg:#666;
  --table-bg:#fff; --table-border:#eee;
  --link-color:#555; --link-del-color:#b91c1c;
  --badge-active-border:#111;
}
:root.dark{
  --bg:#16181d; --text:#e8e8e8; --text-muted:#9aa0a8;
  --card-bg:#1f232a; --card-border:#333842; --label-color:#aab0b8;
  --input-bg:#262b33; --input-border:#3a4048; --input-text:#e8e8e8;
  --btn-bg:#e8e8e8; --btn-text:#16181d; --btn-secondary-bg:#4a505a;
  --table-bg:#1f232a; --table-border:#333842;
  --link-color:#c3c8ce; --link-del-color:#ff6b6b;
  --badge-active-border:#e8e8e8;
}
body{font-family:Arial,Helvetica,sans-serif;max-width:820px;margin:0 auto;padding:16px;background:var(--bg);color:var(--text);transition:background-color .2s ease,color .2s ease}
.topbar{display:flex;justify-content:space-between;align-items:flex-start;gap:10px}
h1{font-size:22px;margin-bottom:4px} p.sub{color:var(--text-muted);font-size:13px;margin-top:0}
.theme-btn{flex-shrink:0;margin-top:0;background:var(--card-bg);color:var(--text);border:1px solid var(--card-border);border-radius:8px;padding:8px 10px;font-size:16px;line-height:1;cursor:pointer}
.summary{display:flex;gap:8px;flex-wrap:wrap;margin:14px 0}
.badge{padding:8px 12px;border-radius:10px;font-size:13px;flex:1;min-width:110px;text-align:center;cursor:pointer;border:2px solid transparent}
.badge.active{border-color:var(--badge-active-border)}
form{background:var(--card-bg);border:1px solid var(--card-border);border-radius:10px;padding:14px;margin-bottom:16px}
label{display:block;font-size:12px;color:var(--label-color);margin-top:8px}
input,select{width:100%;box-sizing:border-box;padding:8px;border:1px solid var(--input-border);border-radius:6px;font-size:14px;margin-top:2px;background:var(--input-bg);color:var(--input-text)}
.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:10px}
button{margin-top:14px;background:var(--btn-bg);color:var(--btn-text);border:none;padding:10px 16px;border-radius:6px;font-size:14px;cursor:pointer}
button.secondary{background:var(--btn-secondary-bg)}
.toolbar{display:flex;flex-direction:column;gap:8px;margin-bottom:10px}
.toolbar input{width:100%}
.toolbar-buttons{display:flex;gap:8px}
.toolbar-buttons button{flex:1;margin-top:0;font-size:12px;padding:8px 10px}
table{width:100%;border-collapse:collapse;font-size:13px;background:var(--table-bg)}
td,th{padding:7px 5px;text-align:left;border-bottom:1px solid var(--table-border)}
.tag{padding:2px 8px;border-radius:10px;font-size:11px;display:inline-block}
.actions a{font-size:12px;margin-right:8px;color:var(--link-color);cursor:pointer;text-decoration:underline}
.actions a.del{color:var(--link-del-color)}
</style></head><body>
<div class='topbar'>
<div>
<h1>Vorratskammer </h1>
<p class='sub'>Produkte, Mengen, Ablaufdaten - Farben zeigen, was Aufmerksamkeit braucht</p>
</div>
<a href='/' class='theme-btn' style='text-decoration:none;display:inline-flex;align-items:center;justify-content:center' title='Zum Hauptmenü'>🏠</a>
<button class='theme-btn' id='themeToggle' onclick='toggleTheme()' title='Helles/Dunkles Design umschalten'>🌙</button>
</div>

<div class='summary' id='summary'></div>

<form id='itemForm' onsubmit='saveItem(event)'>
<h3 id='formTitle' style='font-size:14px;margin-bottom:6px'>Produkt hinzufuegen</h3>
<input type='hidden' id='editId' value='0'>
<div class='row3'>
<div><label>Name</label><input id='fName' required></div>
<div><label>Kategorie</label><input id='fCategory' placeholder='Vorrat / Reinigungsmittel / Kosmetik...'></div>
<div><label>Notizen</label><input id='fNotes'></div>
</div>
<div class='row3'>
<div><label>Menge</label><input id='fQty' type='number' step='0.01' value='1'></div>
<div><label>Einheit</label><input id='fUnit' placeholder='Stk / kg / l'></div>
<div><label>Mindestbestand (opt.)</label><input id='fMinQty' type='number' step='0.01' placeholder='0 = deaktiviert'></div>
</div>
<div class='row'>
<div><label>Ablaufdatum (optional)</label><input id='fExp' type='date'></div>
<div></div>
</div>
<button type='submit'>Speichern</button>
<button type='button' class='secondary' onclick='resetForm()'>Bearbeitung abbrechen</button>
</form>

<div class='toolbar'>
<input id='search' placeholder='Suche nach Name oder Kategorie...' onkeyup='render()'>
<div class='toolbar-buttons'>
<button onclick="window.open('/vorratskammer/export','_blank')">📥 Exportieren</button>
<button onclick="document.getElementById('importFile').click()">📤 Importieren</button>
</div>
<input type='file' id='importFile' accept='.csv,text/csv' style='display:none' onchange='importCSV(this.files[0])'>
</div>

<table id='tbl'><thead><tr><th>Name</th><th>Kategorie</th><th>Menge</th><th>Ablauf</th><th>Status</th><th></th></tr></thead><tbody id='tbody'></tbody></table>

<script>
let allItems = [];
let filterStatus = -1;

async function loadData() {
  const r = await fetch('/vorratskammer/list');
  allItems = await r.json();
  render();
}

function renderSummary() {
  const counts = [0,0,0,0];
  allItems.forEach(it => counts[it.status]++);
  const labels = [['Insgesamt', -1, '#4b5563', '#ffffff'], ['OK', 0, '#16a34a', '#ffffff'],
                   ['Niedriger Bestand', 1, '#ca8a04', '#ffffff'], ['Laeuft bald ab', 2, '#ea580c', '#ffffff'],
                   ['Abgelaufen', 3, '#dc2626', '#ffffff']];
  let html = '';
  labels.forEach(([label, st, bg, fg]) => {
    const count = st === -1 ? allItems.length : counts[st];
    const active = filterStatus === st ? 'active' : '';
    html += `<div class='badge ${active}' style='background:${bg};color:${fg}' onclick='setFilter(${st})'>${label}<br><strong>${count}</strong></div>`;
  });
  document.getElementById('summary').innerHTML = html;
}

function setFilter(st) { filterStatus = (filterStatus === st) ? -1 : st; render(); }

function render() {
  renderSummary();
  const q = document.getElementById('search').value.toLowerCase();
  let html = '';
  allItems.filter(it => {
    if (filterStatus !== -1 && it.status !== filterStatus) return false;
    if (q && !it.name.toLowerCase().includes(q) && !it.category.toLowerCase().includes(q)) return false;
    return true;
  }).forEach(it => {
    const exp = it.expYear > 0 ? `${String(it.expDay).padStart(2,'0')}.${String(it.expMonth).padStart(2,'0')}.${it.expYear}` : '-';
    const colors = [['#16a34a','#ffffff'],['#ca8a04','#ffffff'],['#ea580c','#ffffff'],['#dc2626','#ffffff']];
    const [bg, fg] = colors[it.status];
    html += `<tr>
      <td>${it.name}</td>
      <td>${it.category}</td>
      <td>${it.qty} ${it.unit}</td>
      <td>${exp}</td>
      <td><span class='tag' style='background:${bg};color:${fg}'>${it.statusLabel}</span></td>
      <td class='actions'><a onclick='editItem(${it.id})'>bearbeiten</a><a class='del' onclick='deleteItem(${it.id})'>loeschen</a></td>
    </tr>`;
  });
  document.getElementById('tbody').innerHTML = html || "<tr><td colspan='6' style='color:#999'>Keine Produkte.</td></tr>";
}

function editItem(id) {
  const it = allItems.find(x => x.id === id);
  if (!it) return;
  document.getElementById('editId').value = it.id;
  document.getElementById('fName').value = it.name;
  document.getElementById('fCategory').value = it.category;
  document.getElementById('fQty').value = it.qty;
  document.getElementById('fUnit').value = it.unit;
  document.getElementById('fMinQty').value = it.minQty;
  document.getElementById('fNotes').value = it.notes;
  document.getElementById('fExp').value = it.expYear > 0 ? `${it.expYear}-${String(it.expMonth).padStart(2,'0')}-${String(it.expDay).padStart(2,'0')}` : '';
  document.getElementById('formTitle').textContent = 'Bearbeiten: ' + it.name;
  window.scrollTo(0,0);
}

function resetForm() {
  document.getElementById('itemForm').reset();
  document.getElementById('editId').value = '0';
  document.getElementById('formTitle').textContent = 'Produkt hinzufuegen';
}

async function saveItem(e) {
  e.preventDefault();
  const exp = document.getElementById('fExp').value;
  let params = new URLSearchParams();
  params.append('id', document.getElementById('editId').value);
  params.append('name', document.getElementById('fName').value);
  params.append('category', document.getElementById('fCategory').value);
  params.append('qty', document.getElementById('fQty').value || '0');
  params.append('unit', document.getElementById('fUnit').value);
  params.append('minQty', document.getElementById('fMinQty').value || '0');
  params.append('notes', document.getElementById('fNotes').value);
  if (exp) {
    const [y,m,d] = exp.split('-');
    params.append('expYear', y); params.append('expMonth', m); params.append('expDay', d);
  }
  await fetch('/vorratskammer/add?' + params.toString());
  resetForm();
  loadData();
}

async function deleteItem(id) {
  if (!confirm('Wirklich loeschen?')) return;
  await fetch('/vorratskammer/delete?id=' + id);
  loadData();
}

function importCSV(file) {
  if (!file) return;
  const reader = new FileReader();
  reader.onload = async function(e) {
    const resp = await fetch('/vorratskammer/import', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: 'data=' + encodeURIComponent(e.target.result)
    });
    const text = await resp.text();
    alert(text);
    document.getElementById('importFile').value = '';
    loadData();
  };
  reader.readAsText(file);
}

function updateThemeIcon() {
  document.getElementById('themeToggle').textContent = document.documentElement.classList.contains('dark') ? '☀️' : '🌙';
}

function toggleTheme() {
  document.documentElement.classList.toggle('dark');
  try {
    localStorage.setItem('vorratskammerTheme', document.documentElement.classList.contains('dark') ? 'dark' : 'light');
  } catch (e) {}
  updateThemeIcon();
}

updateThemeIcon();
loadData();
setInterval(loadData, 30000);
</script>
</body></html>
)rawliteral";
  server.send(200, "text/html; charset=UTF-8", html);
}

// ==================== SETUP / LOOP ====================

// ============================================================
// STRONA GŁÓWNA - MENU (łączy wszystkie 3 programy)
// ============================================================
// Zwraca aktualny czas serwera (epoch, sekundy) jako JSON - strona menu
// go raz pobiera i dalej "tyka" sama w JS (setInterval), zeby nie odpytywac
// ESP32 co sekunde. Dzieki temu zegarek pokazuje czas z NTP (ESP32), a nie
// czas telefonu/komputera.
void handleTimeApi() {
  String json = "{\"epoch\":" + String((unsigned long)time(nullptr)) + ",\"synced\":" + (timeSynced ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleMainMenu() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<link rel='icon' type='image/png' href=\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'%3E%3Ctext x='0' y='20' font-size='20' fill='white'%3E🧠%3C/text%3E%3C/svg%3E\">";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0, viewport-fit=cover'>";
  html += "<meta name='apple-mobile-web-app-capable' content='yes'>";
  html += "<title>🧠 Hauptmenü</title>";
  html += "<style>";
  html += "*{margin:0;padding:0;box-sizing:border-box}";
  html += "body{background:linear-gradient(135deg,#667eea,#764ba2);font-family:'Segoe UI',Arial;padding:6vh 20px 20px;min-height:100vh;display:flex;align-items:flex-start;justify-content:center}";
  html += ".container{max-width:420px;width:100%}";
  html += ".clock{text-align:center;color:#0df705;font-weight:800;line-height:1.08;margin-bottom:18px}";
  html += ".clock .time{font-size:clamp(2.3em,13vw,3.5em);letter-spacing:1px}";
  html += ".clock .date{font-size:clamp(1.7em,10vw,2.5em);margin-top:2px}";
  html += "h1{color:white;text-align:center;margin-bottom:30px;font-size:1.6em}";
  html += ".card{background:rgba(255,255,255,0.15);border-radius:20px;padding:20px;margin-bottom:16px;display:flex;align-items:center;gap:16px;text-decoration:none;transition:transform .15s}";
  html += ".card:active{transform:scale(0.97)}";
  html += ".card .emoji{font-size:2.2em}";
  html += ".card .txt{color:white}";
  html += ".card .txt .title{font-size:1.15em;font-weight:bold}";
  html += ".card .txt .sub{font-size:0.8em;color:#eee;opacity:0.85}";
  html += ".footer{text-align:center;color:rgba(255,255,255,0.7);font-size:0.75em;margin-top:20px}";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<div class='clock'><div class='time' id='clockTime'>--:--:--</div><div class='date' id='clockDate'>--.--.----</div></div>";
  html += "<h1>🏠 Hauptmenü 🏠</h1>";
  html += "<a class='card' href='/einkaufsliste'><span class='emoji'>🛒</span><span class='txt'><div class='title'>Einkaufsliste</div><div class='sub'>Listy zakupów, budżet, rachunki</div></span></a>";
  html += "<a class='card' href='/vorratskammer'><span class='emoji'>📦</span><span class='txt'><div class='title'>Vorratskammer</div><div class='sub'>Inwentarz domowy, ważność produktów</div></span></a>";
  html += "<a class='card' href='/cisnienie'><span class='emoji'>🩺</span><span class='txt'><div class='title'>Blutdruck-Tagebuch</div><div class='sub'>Monitor ciśnienia krwi</div></span></a>";
  html += "<div class='footer'>ESP32 3-w-1</div>";
  html += "</div>";
  html += "<script>";
  html += "let offset=0;";
  html += "function pad(n){return n<10?('0'+n):n;}";
  html += "function tick(){const d=new Date(Date.now()+offset);";
  html += "document.getElementById('clockTime').textContent=pad(d.getHours())+':'+pad(d.getMinutes())+':'+pad(d.getSeconds());";
  html += "document.getElementById('clockDate').textContent=pad(d.getDate())+'.'+pad(d.getMonth()+1)+'.'+d.getFullYear();}";
  html += "fetch('/time').then(r=>r.json()).then(j=>{offset=j.epoch*1000-Date.now();tick();setInterval(tick,1000);}).catch(()=>{tick();setInterval(tick,1000);});";
  html += "</script>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n\n=== ESP32 3-w-1: START ===\n");

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("❌ Błąd SD! Sprawdź piny/okablowanie/kartę (format FAT32).");
  } else {
    Serial.println("✅ SD OK!");
  }

  // --- wczytaj dane wszystkich 3 programów z karty SD ---
  loadData(); loadHistory(); loadTotals();   // Einkaufsliste
  loadMeasurements();                         // Ciśnienie
  loadItems();                                // Vorratskammer

  // --- WiFi ---
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Łączenie z WiFi");
  int wifiTries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    wifiTries++;
    if (wifiTries > 60) { // ok. 30 sekund próbowania - jak dalej nic, restart i spróbuj od nowa
      Serial.println("\n❌ Nie udało się połączyć z WiFi - restart ESP32...");
      ESP.restart();
    }
  }
  Serial.println();
  Serial.print("WiFi OK! IP: ");
  Serial.println(WiFi.localIP());

  // --- NTP (wspólny dla wszystkich 3 programów) ---
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  Serial.print("Oczekiwanie na czas NTP");
  int timeout = 0;
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo, 1000) && timeout < 30) { Serial.print("."); timeout++; }
  Serial.println();
  timeSynced = (timeout < 30);
  Serial.println(timeSynced ? "Czas pobrany!" : "Brak czasu NTP - będę próbował dalej w tle co minutę");
  updateTime(); // Ciśnienie - inicjalizuje własne zmienne czasu (cisnienieYear itd.)

  // --- Einkaufsliste: sprawdzenie nowego miesiąca + auto-backup ---
  checkNewMonth();
  loadBackupMeta();
  checkAutoBackup();
  loadRestartMeta();

  // --- OTA (wgrywanie nowego firmware przez WiFi) ---
  ArduinoOTA.setHostname("ESP32-3w1");
  //ArduinoOTA.setPassword("esp32ota2026"); // <<< ZMIEŃ na swoje hasło - bez tego kazdy w tej samej sieci WiFi mógłby wgrać nowy firmware
  ArduinoOTA.begin();

  // ================= ROUTES =================
  server.on("/", handleMainMenu);
  server.on("/time", handleTimeApi);

  // --- Einkaufsliste ---
  server.on("/einkaufsliste", handleRoot);
  server.on("/rechnung", handleRechnung);
  server.on("/list", handleList);
  server.on("/add", handleAdd);
  server.on("/quantity", handleQuantity);
  server.on("/toggle", handleToggle);
  server.on("/delete", handleDelete);
  server.on("/move", handleMove);
  server.on("/exportshop", handleExportShop);
  server.on("/importshop", HTTP_POST, handleImportShop);
  server.on("/paragon", handleParagon);
  server.on("/undo", handleUndo);
  server.on("/budget", handleBudget);
  server.on("/exportdata", handleExportData);
  server.on("/importdata", HTTP_POST, handleImportData);
  server.on("/totals", handleTotals);
  server.on("/bill", handleBill);
  server.on("/clearhistory", handleClearHistory);
  server.on("/backupnow", handleBackupNow);
  server.on("/downloadbackup", handleDownloadBackup);
  server.on("/restorebackup", HTTP_POST, handleRestoreBackup);
  server.on("/sdstatus", handleSdStatus);
  server.on("/backuplist", handleBackupList);
  server.on("/deletebackup", handleDeleteBackup);

  // --- Ciśnienie ---
  server.on("/cisnienie", handleCisnienie);
  server.on("/cisnienie/list", handleCisnienieList);
  server.on("/cisnienie/add", handleCisnienieAdd);
  server.on("/cisnienie/delete", handleCisnienieDelete);
  server.on("/cisnienie/export", handleCisnienieExport);
  server.on("/cisnienie/raport", handleCisnienieRaportMiesieczny);
  server.on("/cisnienie/import", HTTP_POST, handleCisnienieImport);

  // --- Vorratskammer ---
  server.on("/vorratskammer", handleVorratskammer);
  server.on("/vorratskammer/list", handleVorratskammerList);
  server.on("/vorratskammer/add", handleVorratskammerAdd);
  server.on("/vorratskammer/delete", handleVorratskammerDelete);
  server.on("/vorratskammer/export", handleVorratskammerExport);
  server.on("/vorratskammer/import", HTTP_POST, handleVorratskammerImport);

  server.begin();
  Serial.println("Serwer wystartował!");
  Serial.print("🌐 Menu główne:   http://"); Serial.println(WiFi.localIP());
  Serial.print("🛒 Einkaufsliste: http://"); Serial.print(WiFi.localIP()); Serial.println("/einkaufsliste");
  Serial.print("🩺 Ciśnienie:     http://"); Serial.print(WiFi.localIP()); Serial.println("/cisnienie");
  Serial.print("📦 Vorratskammer: http://"); Serial.print(WiFi.localIP()); Serial.println("/vorratskammer");
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  static unsigned long lastCheck = 0;
  static unsigned long lastWifiCheck = 0;
  static unsigned long lastTimeRetry = 0;

  checkDailyRestart(); // Einkaufsliste: nocny restart o 4:00 (raz dziennie) - sprawdzenie tanie, nie szkodzi

  if (millis() - lastWifiCheck > 15000) {
    lastWifiCheck = millis();
    checkWifiConnection();
  }

  // Jeśli czas nie zsynchronizował się przy starcie, próbuj dalej w tle -
  // co minutę, bez blokowania serwera WWW (getLocalTime z krótkim timeoutem).
  if (!timeSynced && millis() - lastTimeRetry > 60000) {
    lastTimeRetry = millis();
    struct tm ti;
    if (getLocalTime(&ti, 500)) {
      timeSynced = true;
      updateTime();
      Serial.println("✅ Czas NTP zsynchronizowany (próba w tle)");
    } else {
      Serial.println("⏳ Nadal brak czasu NTP - kolejna próba za minutę");
    }
  }

  if (millis() - lastCheck > 3600000) {
    lastCheck = millis();
    checkNewMonth();
    checkAutoBackup(); // raz dziennie robi pełny backup listy zakupów + budżetu
  }

  server.handleClient();
  ArduinoOTA.handle();
  delay(10);
}
