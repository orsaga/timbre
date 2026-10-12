// main.ino (versión corregida y completa)
// Placa: ESP32 DevKitC (ESP32-WROOM-32) esp32 3.0.7
// CAMBIO: se agrega lectura/escritura real del módulo DS3231 como respaldo de hora

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <time.h>
#include <ESP32Time.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Wire.h"
#include <Preferences.h>   // guarda la red WiFi en la memoria del ESP32
#include <RTClib.h>              // <-- NUEVO: librería para el DS3231
#include "credentials.h" // Debes tener SSID1/PASSWORD1 (y opcional SSID2.., SSID3..), HTTP_USERNAME, HTTP_PASSWORD

// ---------- Pines ----------
#define RELAY_PIN           4
#define EMERGENCY_BUTTON_PIN 32
#define DISABLE_BUTTON_PIN   33

// DFPlayer (UART2)
HardwareSerial DFPlayerSerial(2);
DFRobotDFPlayerMini myDFPlayer;

// NTP / RTC
const char* ntpServer = "co.pool.ntp.org";
const long  gmtOffset_sec = -18000; // Colombia GMT-5
const int   daylightOffset_sec = 0;
ESP32Time rtc;

// WiFi configurable desde la web
Preferences prefs;
String institutionName = "";   // nombre del colegio o empresa, editable desde la página
unsigned long restartAt = 0;   // si es > 0, reinicia el ESP32 cuando llegue ese momento

// DS3231 (RTC físico con pila de respaldo) -- NUEVO
RTC_DS3231 ds3231;
bool ds3231Ok = false;

// Web server
AsyncWebServer server(80);

// Credenciales (del archivo credentials.h)
// Redes conocidas (de credentials.h). Se prueban en orden; SSID2 y SSID3 son opcionales.
const char* knownSsids[] = {
  SSID1,
#ifdef SSID2
  SSID2,
#endif
#ifdef SSID3
  SSID3,
#endif
};
const char* knownPasswords[] = {
  PASSWORD1,
#ifdef SSID2
  PASSWORD2,
#endif
#ifdef SSID3
  PASSWORD3,
#endif
};
const int knownNetworks = sizeof(knownSsids) / sizeof(knownSsids[0]);
const char *http_username = HTTP_USERNAME;
const char *http_password = HTTP_PASSWORD;

// Control de canciones
const int MAX_SONGS = 2999;      // máximo de pistas que el DFPlayer puede direccionar desde la raíz de la SD
const int DEFAULT_SONGS = 29;    // se usa solo si no se puede leer la SD
const int SONGS_OVERRIDE = 0;    // 0 = contar automático; si el conteo sale mal, pon aquí la cantidad real (ej. 29)
int totalSongs = DEFAULT_SONGS;  // se actualiza leyendo la SD en setup()
std::vector<uint16_t> songDeck;  // baraja de canciones mezcladas
int deckPosition = 0;
int lastPlayedSong = 0;
uint32_t deckSeed = 0;     // semilla con la que se armó la baraja actual
uint16_t deckAvoid = 0;    // canción que no podía quedar de primera al armarla
uint32_t rngState = 1;     // generador propio (permite reconstruir el mismo orden)

// Estados
bool timbreEnabled = true;
int activeSchedule = 0;

enum BellOperationalState {
  BELL_ACTIVE,
  BELL_PAUSED_WEEKEND,
  BELL_PAUSED_HOLIDAY,
  BELL_DEACTIVATED_MANUAL
};
BellOperationalState bellCurrentState;

// Estructura horario
struct BellTime {
  int hour;
  int minute;
  bool isEndOfDay;
  String name;
};

// Duraciones
int bell_time_duration = 6000;   // 6 s para relé
int mp3_time_duration = 60000;   // 60 s mp3

// Control no bloqueante timbre
unsigned long bellStartTime = 0;
int bellState = 0; // 0: inactivo, 1: primera campana, 2: pausa, 3: segunda campana, 4: mp3 timeout
bool isRingInProgress = false;
bool isEndOfDayBell = false;

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Horarios (3 configuraciones)
std::vector<BellTime> schedules[3];
int scheduleCount[3] = {0,0,0};
String scheduleNames[3] = { "Horario 1", "Horario 2", "Horario 3" };

// ---- Festivos de Colombia (calculados automáticamente, Ley 51 de 1983 "Ley Emiliani") ----
static int holidayYear = 0;      // año para el que se calcularon los festivos
static int holidayDays[24];      // día del año (1..366) de cada festivo
static int holidayTotal = 0;

static bool esBisiesto(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

// Día del año (1..366) a partir de año, mes (1..12) y día
static int diaDelAnio(int y, int m, int d) {
  static const int acum[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  int n = acum[m - 1] + d;
  if (m > 2 && esBisiesto(y)) n++;
  return n;
}

// Día de la semana: 0 = domingo ... 6 = sábado
static int diaSemana(int y, int m, int d) {
  static const int t[] = {0,3,2,5,0,3,5,1,4,6,2,4};
  if (m < 3) y--;
  return (y + y/4 - y/100 + y/400 + t[m - 1] + d) % 7;
}

// Calcula los 18 festivos nacionales del año y los guarda como día del año
static void calcularFestivos(int y) {
  holidayTotal = 0;
  int jan1 = diaSemana(y, 1, 1);

  // Traslada al lunes siguiente (si ya es lunes, se queda igual)
  auto lunes = [&](int doy) {
    int dow = (jan1 + doy - 1) % 7;
    return doy + (8 - dow) % 7;
  };
  auto agregar = [&](int doy) {
    if (holidayTotal < 24) holidayDays[holidayTotal++] = doy;
  };

  // Fijos
  agregar(diaDelAnio(y, 1, 1));     // Año Nuevo
  agregar(diaDelAnio(y, 5, 1));     // Día del Trabajo
  agregar(diaDelAnio(y, 7, 20));    // Independencia
  agregar(diaDelAnio(y, 8, 7));     // Batalla de Boyacá
  agregar(diaDelAnio(y, 12, 8));    // Inmaculada Concepción
  agregar(diaDelAnio(y, 12, 25));   // Navidad

  // Se trasladan al lunes siguiente
  agregar(lunes(diaDelAnio(y, 1, 6)));    // Reyes Magos
  agregar(lunes(diaDelAnio(y, 3, 19)));   // San José
  agregar(lunes(diaDelAnio(y, 6, 29)));   // San Pedro y San Pablo
  agregar(lunes(diaDelAnio(y, 8, 15)));   // Asunción de la Virgen
  agregar(lunes(diaDelAnio(y, 10, 12)));  // Día de la Raza
  agregar(lunes(diaDelAnio(y, 11, 1)));   // Todos los Santos
  agregar(lunes(diaDelAnio(y, 11, 11)));  // Independencia de Cartagena

  // Semana Santa: fecha de Pascua (algoritmo de Meeus/Jones/Butcher)
  int a = y % 19, b = y / 100, c = y % 100;
  int d = b / 4, e = b % 4, f = (b + 8) / 25, g = (b - f + 1) / 3;
  int h = (19 * a + b - d - g + 15) % 30;
  int i = c / 4, k = c % 4;
  int l = (32 + 2 * e + 2 * i - h - k) % 7;
  int m = (a + 11 * h + 22 * l) / 451;
  int mesPascua = (h + l - 7 * m + 114) / 31;
  int diaPascua = ((h + l - 7 * m + 114) % 31) + 1;
  int pascua = diaDelAnio(y, mesPascua, diaPascua);

  agregar(pascua - 3);           // Jueves Santo
  agregar(pascua - 2);           // Viernes Santo
  agregar(lunes(pascua + 39));   // Ascensión del Señor
  agregar(lunes(pascua + 60));   // Corpus Christi
  agregar(lunes(pascua + 68));   // Sagrado Corazón

  holidayYear = y;
}


// variable para IP mostrada en pantalla/JSON (actualizada por evento WiFi)
String localIpStr = "AP: TimbreEscolar";

// DFPlayer ok
bool dfPlayerOk = false;

// Botones - debounce no bloqueante
unsigned long lastDebounceEmergency = 0;
unsigned long lastDebounceDisable = 0;
const unsigned long debounceDelay = 50;
bool lastEmergencyState = HIGH;
bool lastDisableState = HIGH;

// Para evitar repetir lectura de DFPlayer.available() en lugares inseguros:
void handleDFPlayerAvailableInLoop();

// ---------- PROTOTIPOS ----------
void setupWiFi();
void setLocalTime();
String getCurrentTimeString();
String getCurrentDateString();
bool weekend(int day);
bool holiday();
void checkSchedule();
void printDetail(uint8_t type, int value);
void startBell(bool endOfDay);
void handleBellState();
void checkButtons_nonBlocking();
void loadSchedules();
void saveSchedules();
String processor(const String &var);
void setupWebServer();
void buildDeck(uint32_t seed, uint16_t avoid);
void newDeck(uint16_t avoid);
void saveDeckState();
void loadDeckState();
int nextSong();
bool tryConnect(const char* s, const char* p);
bool networkVisible(const String& name, int n);

// ---------- IMPLEMENTACIÓN ----------

// Generador pseudoaleatorio propio: la misma semilla produce siempre el mismo orden
uint32_t xorshift32() {
  rngState ^= rngState << 13;
  rngState ^= rngState >> 17;
  rngState ^= rngState << 5;
  return rngState;
}

// Arma la baraja (1..totalSongs) mezclada con Fisher-Yates a partir de una semilla
void buildDeck(uint32_t seed, uint16_t avoid) {
  rngState = seed ? seed : 0x9E3779B9;
  songDeck.clear();
  for (int i = 1; i <= totalSongs; i++) songDeck.push_back(i);
  for (int i = (int)songDeck.size() - 1; i > 0; i--) {
    int j = xorshift32() % (i + 1);
    std::swap(songDeck[i], songDeck[j]);
  }
  // Evita que la primera de la baraja sea la última que sonó
  if (songDeck.size() > 1 && avoid != 0 && songDeck[0] == avoid) {
    std::swap(songDeck[0], songDeck[songDeck.size() - 1]);
  }
  deckSeed = seed;
  deckAvoid = avoid;
}

// Guarda en la memoria permanente lo necesario para continuar tras un corte de energía
void saveDeckState() {
  prefs.begin("songs", false);
  prefs.putUInt("seed", deckSeed);
  prefs.putUShort("avoid", deckAvoid);
  prefs.putUShort("total", totalSongs);
  prefs.putUShort("pos", deckPosition);
  prefs.putUShort("last", lastPlayedSong);
  prefs.end();
}

// Crea una baraja nueva con semilla aleatoria real y la guarda
void newDeck(uint16_t avoid) {
  uint32_t seed = esp_random();
  if (seed == 0) seed = 1;
  buildDeck(seed, avoid);
  deckPosition = 0;
  saveDeckState();
  Serial.println("Baraja nueva con " + String(totalSongs) + " canciones.");
}

// Al arrancar: restaura la baraja guardada si sigue siendo válida; si no, crea una nueva
void loadDeckState() {
  prefs.begin("songs", false);
  uint32_t seed = prefs.getUInt("seed", 0);
  uint16_t total = prefs.getUShort("total", 0);
  uint16_t pos = prefs.getUShort("pos", 0);
  uint16_t avoid = prefs.getUShort("avoid", 0);
  lastPlayedSong = prefs.getUShort("last", 0);
  prefs.end();

  if (seed != 0 && total == totalSongs && pos <= total) {
    buildDeck(seed, avoid);
    deckPosition = pos;
    Serial.println("Baraja restaurada: va en la cancion " + String(pos) + " de " + String(total));
  } else {
    newDeck(lastPlayedSong);   // primera vez, o cambió la cantidad de canciones en la SD
  }
}

// Entrega la siguiente canción sin repetir hasta agotar la baraja
int nextSong() {
  if (songDeck.empty() || deckPosition >= (int)songDeck.size()) newDeck(lastPlayedSong);
  lastPlayedSong = songDeck[deckPosition++];
  saveDeckState();   // se guarda antes de sonar: un corte de energía no repite esta canción
  return lastPlayedSong;
}

void setup() {
  Serial.begin(115200);

  // Pines
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(EMERGENCY_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DISABLE_BUTTON_PIN, INPUT_PULLUP);

  // DFPlayer serial
  DFPlayerSerial.begin(9600, SERIAL_8N1, 16, 17);
  delay(200); // pequeña pausa
  if (!myDFPlayer.begin(DFPlayerSerial)) {
    Serial.println(F("Error al inicializar DFPlayer"));
    dfPlayerOk = false;
  } else {
    dfPlayerOk = true;
    myDFPlayer.setTimeOut(500);
    myDFPlayer.volume(30);
    Serial.println("DFPlayer inicializado correctamente.");

    // Contar cuántas canciones hay realmente en la SD
    delay(500);
    int n = -1;
    for (int intento = 1; intento <= 3 && n <= 0; intento++) {
      n = myDFPlayer.readFileCounts();
      Serial.println("Lectura " + String(intento) + " de archivos en SD: " + String(n));
      if (n <= 0) delay(300);
    }
    if (SONGS_OVERRIDE > 0) {
      totalSongs = SONGS_OVERRIDE;
      Serial.println("Cantidad fijada manualmente: " + String(totalSongs));
    } else if (n > MAX_SONGS) {
      totalSongs = MAX_SONGS;
      Serial.println("La SD reporta " + String(n) + " archivos; el DFPlayer solo direcciona " + String(MAX_SONGS) + ", se usaran esos.");
    } else if (n > 0) {
      totalSongs = n;
      Serial.println("Canciones detectadas en la SD: " + String(totalSongs));
    } else {
      Serial.println("No se pudo leer la cantidad de canciones, usando " + String(DEFAULT_SONGS));
    }
  }

  // Restaurar la baraja guardada (si la hay) o crear una nueva
  loadDeckState();

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Error al montar SPIFFS");
  }

  // Cargar horarios
  loadSchedules();

  // Nombre de la institución (se guarda desde la página)
  {
    Preferences cfg;
    cfg.begin("config", false);
    institutionName = cfg.getString("inst", "");
    cfg.end();
  }

  // Bus I2C (compartido por OLED y DS3231)
  Wire.begin();

  // DS3231 -- NUEVO: inicializamos el RTC físico ANTES del NTP
  if (!ds3231.begin()) {
    Serial.println(F("No se detectó el módulo DS3231. Sin respaldo de hora físico."));
    ds3231Ok = false;
  } else {
    ds3231Ok = true;
    Serial.println("DS3231 detectado correctamente.");
    if (ds3231.lostPower()) {
      Serial.println("Aviso: el DS3231 reporta pérdida de energía (pila agotada o primera vez).");
    }
  }

  // OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("Error al inicializar SSD1306"));
    for(;;); // detenemos si no hay display (evita comportamiento indefinido)
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println(F("Iniciando..."));
  display.display();

  // Conectar WiFi (no bloqueante prolongado)
  setupWiFi();

  // NTP / RTC (con respaldo automático al DS3231 si no hay red)
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  setLocalTime();

  // Iniciar servidor web
  setupWebServer();

  Serial.println("Sistema listo!");
  Serial.println("Hora: " + getCurrentTimeString());
  Serial.println("Config activa: " + scheduleNames[activeSchedule]);
}

// non-blocking loop
void loop() {
  // Actualizar estado operacional
  updateBellOperationalState();

  // Revisar botones de forma no bloqueante (sin while ni delays largos)
  checkButtons_nonBlocking();

  // Revisar horarios
  checkSchedule();

  // Manejar estado del timbre
  handleBellState();

  // Revisar DFPlayer (leer eventos)
  handleDFPlayerAvailableInLoop();

  // Actualizar pantalla
  updateDisplay();

  // Reinicio diferido (después de guardar una red WiFi nueva)
  if (restartAt != 0 && millis() >= restartAt) {
    Serial.println("Reiniciando para aplicar la nueva red WiFi...");
    delay(100);
    ESP.restart();
  }

  // pequeño respiro
  delay(10);
}

// -----------------------------------------------------------------------------
// Funciones auxiliares
// -----------------------------------------------------------------------------

void setupWiFi() {
  Serial.println("Conectando WiFi...");
  // Registrar evento WiFi para actualizar IP sin llamar WiFi.localIP() en lugares inseguros
 WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info){
    if (event == IP_EVENT_STA_GOT_IP) {
      localIpStr = WiFi.localIP().toString();
      Serial.println("✅ WiFi Conectado! IP: " + localIpStr);
    } else if (event == WIFI_EVENT_STA_DISCONNECTED) {
      localIpStr = "AP: TimbreEscolar";
      Serial.println("⚠️ WiFi Desconectado.");
    }
});

  // Red guardada desde la interfaz web (si existe)
  prefs.begin("wifi", false);
  String savedSsid = prefs.getString("ssid", "");
  String savedPass = prefs.getString("pass", "");
  prefs.end();

  WiFi.mode(WIFI_STA);

  // Buscar redes cercanas para no esperar por redes que no están a la vista
  Serial.println("Buscando redes cercanas...");
  int visibles = WiFi.scanNetworks();   // bloqueante, unos 2-3 s; negativo = falló la búsqueda

  // 1) Red guardada desde la interfaz web
  if (savedSsid.length() > 0) {
    if (visibles < 0 || networkVisible(savedSsid, visibles)) {
      Serial.println("Probando red guardada: " + savedSsid);
      tryConnect(savedSsid.c_str(), savedPass.c_str());
    } else {
      Serial.println("Red guardada no esta a la vista: " + savedSsid);
    }
  }

  // 2) Redes de credentials.h, en orden
  for (int i = 0; i < knownNetworks && WiFi.status() != WL_CONNECTED; i++) {
    if (visibles < 0 || networkVisible(String(knownSsids[i]), visibles)) {
      Serial.println("\nProbando red " + String(i + 1) + " de credentials.h: " + String(knownSsids[i]));
      tryConnect(knownSsids[i], knownPasswords[i]);
    } else {
      Serial.println("Red " + String(i + 1) + " no esta a la vista: " + String(knownSsids[i]));
    }
  }
  WiFi.scanDelete();   // libera los resultados de la búsqueda

  if (WiFi.status() == WL_CONNECTED) {
    localIpStr = WiFi.localIP().toString();
    Serial.println("\nWiFi Conectado!");
    Serial.println(localIpStr);
  } else {
    Serial.println("\nNo se pudo conectar a WiFi, activando AP.");
    WiFi.mode(WIFI_AP_STA);   // AP + estación, para poder buscar redes desde la web
    WiFi.softAP("TimbreEscolar", "password");
    localIpStr = "AP: TimbreEscolar";
  }
}

// Indica si un nombre de red aparece entre las n redes encontradas en la búsqueda
bool networkVisible(const String& name, int n) {
  for (int i = 0; i < n; i++) {
    if (WiFi.SSID(i) == name) return true;
  }
  return false;
}

// Intenta conectarse a una red durante 8 segundos
bool tryConnect(const char* s, const char* p) {
  WiFi.disconnect();
  WiFi.begin(s, p);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(200);
    Serial.print(".");
  }
  return WiFi.status() == WL_CONNECTED;
}

// -----------------------------------------------------------------------------
// setLocalTime -- REESCRITA
// 1) Intenta NTP. Si funciona: pone la hora en el reloj interno (ESP32Time)
//    Y además actualiza el DS3231, para que quede al día para el próximo corte.
// 2) Si NTP falla (por ejemplo, el router también se quedó sin energía):
//    lee la hora directamente del DS3231 en vez de quedarse sin hora válida.
// -----------------------------------------------------------------------------
void setLocalTime() {
  struct tm timeinfo;

  if (getLocalTime(&timeinfo, 5000)) {
    rtc.setTimeStruct(timeinfo);
    Serial.println("Hora sincronizada por NTP.");

    if (ds3231Ok) {
      ds3231.adjust(DateTime(
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
      ));
      Serial.println("DS3231 actualizado con la hora NTP.");
    }
  } else {
    Serial.println("No se pudo obtener hora por NTP.");

    if (ds3231Ok) {
      DateTime now = ds3231.now();
      rtc.setTime(now.second(), now.minute(), now.hour(), now.day(), now.month(), now.year());
      Serial.println("Hora tomada del DS3231 (respaldo sin red).");
    } else {
      Serial.println("¡ADVERTENCIA! Sin NTP ni DS3231 disponibles: la hora no es confiable.");
    }
  }
}

// Fecha en español para mostrar en la interfaz, ej: "jueves, 8 de octubre de 2026"
String getCurrentDateString() {
  const char* dias[]  = {"domingo","lunes","martes","miércoles","jueves","viernes","sábado"};
  const char* meses[] = {"enero","febrero","marzo","abril","mayo","junio","julio",
                         "agosto","septiembre","octubre","noviembre","diciembre"};
  char buf[64];
  snprintf(buf, sizeof(buf), "%s, %d de %s de %d",
           dias[rtc.getDayofWeek()], rtc.getDay(), meses[rtc.getMonth()], rtc.getYear());
  return String(buf);
}

String getCurrentTimeString() {
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
  return String(timeStr);
}

bool weekend(int day) {
  // day: 0 = domingo, 6 = sábado
  return (day == 0 || day == 6);
}

bool holiday() {
  int y = rtc.getYear();
  if (y != holidayYear) calcularFestivos(y);   // se recalcula solo al cambiar de año
  int hoy = diaDelAnio(y, rtc.getMonth() + 1, rtc.getDay());
  for (int i = 0; i < holidayTotal; i++) {
    if (holidayDays[i] == hoy) return true;
  }
  return false;
}

void updateBellOperationalState() {
  if (!timbreEnabled) {
    bellCurrentState = BELL_DEACTIVATED_MANUAL;
  } else if (holiday()) {
    bellCurrentState = BELL_PAUSED_HOLIDAY;
  } else if (weekend(rtc.getDayofWeek())) {
    bellCurrentState = BELL_PAUSED_WEEKEND;
  } else {
    bellCurrentState = BELL_ACTIVE;
  }
}

void updateDisplay() {
  static unsigned long lastDisplayUpdate = 0;
  if (millis() - lastDisplayUpdate < 1000) return; // 1s entre actualizaciones

  display.clearDisplay();
  display.setTextSize(1);

  if (isRingInProgress) {
    display.setTextSize(2);
    display.setCursor(0,0);
    display.println("Timbre");
    display.setCursor(0,16);
    display.println("Activado");
    display.display();
  } else {
    display.setTextSize(1);
    display.setTextWrap(false);   // el texto largo se recorta en vez de saltar a otra línea
    display.setCursor(0,0);
    display.println(localIpStr);
    display.setCursor(0,8);
    display.print(getCurrentTimeString());
    display.println(ds3231Ok ? " RTC:OK" : " RTC:NO");   // RTC:NO = no se detectó el DS3231
    display.setCursor(0,16);
    display.print("Config: ");
    display.println(scheduleNames[activeSchedule]);
    display.setCursor(0,24);
    display.print("Estado: ");
    switch (bellCurrentState) {
      case BELL_ACTIVE: display.println("Activo"); break;
      case BELL_PAUSED_WEEKEND: display.println("Pausa FDS"); break;
      case BELL_PAUSED_HOLIDAY: display.println("Pausa Festivo"); break;
      case BELL_DEACTIVATED_MANUAL: display.println("Desactivado"); break;
    }
    display.display();
  }

  lastDisplayUpdate = millis();
}

// -----------------------------------------------------------------------------
// Check schedule: solo activa si bellCurrentState == BELL_ACTIVE
// -----------------------------------------------------------------------------
void checkSchedule() {
  if (bellCurrentState != BELL_ACTIVE) return;

  int currentHour = rtc.getHour(true);
  int currentMinute = rtc.getMinute();
  int currentSecond = rtc.getSecond();

  // Solo cuando segundos == 0 para evitar activaciones repetidas
  if (currentSecond != 0) return;

  for (int i = 0; i < scheduleCount[activeSchedule]; i++) {
    if (schedules[activeSchedule][i].hour == currentHour &&
        schedules[activeSchedule][i].minute == currentMinute) {
      startBell(schedules[activeSchedule][i].isEndOfDay);
      break;
    }
  }
}

// -----------------------------------------------------------------------------
// Manejo DFPlayer: imprimir eventos y actualizar songPlayed
// Solo llamada desde loop() (contexto seguro)
// -----------------------------------------------------------------------------
void handleDFPlayerAvailableInLoop() {
  if (!dfPlayerOk) return;

  if (myDFPlayer.available()) {
    uint8_t type = myDFPlayer.readType();
    int value = myDFPlayer.read();
    printDetail(type, value);
  }
}

void printDetail(uint8_t type, int value){
  switch (type) {
    case TimeOut: Serial.println(F("Time Out!")); break;
    case WrongStack: Serial.println(F("Stack Wrong!")); break;
    case DFPlayerCardInserted: Serial.println(F("Card Inserted!")); break;
    case DFPlayerCardRemoved: Serial.println(F("Card Removed!")); break;
    case DFPlayerCardOnline: Serial.println(F("Card Online!")); break;
    case DFPlayerUSBInserted: Serial.println("USB Inserted!"); break;
    case DFPlayerUSBRemoved: Serial.println("USB Removed!"); break;
    case DFPlayerPlayFinished:
      Serial.print(F("Number:")); Serial.print(value); Serial.println(F(" Play Finished!"));
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy: Serial.println(F("Card not found")); break;
        case Sleeping: Serial.println(F("Sleeping")); break;
        case SerialWrongStack: Serial.println(F("Get Wrong Stack")); break;
        case CheckSumNotMatch: Serial.println(F("Check Sum Not Match")); break;
        case FileIndexOut: Serial.println(F("File Index Out of Bound")); break;
        case FileMismatch: Serial.println(F("Cannot Find File")); break;
        case Advertise: Serial.println(F("In Advertise")); break;
        default: break;
      }
      break;
    default: break;
  }
}

// -----------------------------------------------------------------------------
// startBell: selecciona canción aleatoria no reproducida y activa relé + mp3
// -----------------------------------------------------------------------------
void startBell(bool endOfDay) {
  if (isRingInProgress) return;

  int randomSong = nextSong();
  Serial.println("¡Timbre activado! (Canción " + String(randomSong) + " de " + String(totalSongs) + ")");

  digitalWrite(RELAY_PIN, HIGH);

  if (dfPlayerOk) {
    myDFPlayer.play(randomSong);
    myDFPlayer.volume(30);
  }

  isRingInProgress = true;
  isEndOfDayBell = endOfDay;
  bellStartTime = millis();
  bellState = 1;
}

// -----------------------------------------------------------------------------
// handleBellState: máquina de estados del timbre (no bloqueante)
// -----------------------------------------------------------------------------
void handleBellState() {
  if (!isRingInProgress) return;

  unsigned long currentMillis = millis();
  unsigned long elapsedTime = currentMillis - bellStartTime;

  switch (bellState) {
    case 1:
      if (elapsedTime >= bell_time_duration) {
        digitalWrite(RELAY_PIN, LOW);
        if (isEndOfDayBell) {
          bellStartTime = currentMillis;
          bellState = 2;
        } else {
          bellStartTime = currentMillis;
          bellState = 4;
        }
      }
      break;
    case 2:
      if (elapsedTime >= 500) {
        digitalWrite(RELAY_PIN, HIGH);
        bellStartTime = currentMillis;
        bellState = 3;
      }
      break;
    case 3:
      if (elapsedTime >= bell_time_duration) {
        digitalWrite(RELAY_PIN, LOW);
        bellStartTime = currentMillis;
        bellState = 4;
      }
      break;
    case 4:
      {
        unsigned long mp3Elapsed = currentMillis - bellStartTime;
        unsigned long target = mp3_time_duration - (isEndOfDayBell ? (bell_time_duration + 500 + bell_time_duration) : bell_time_duration);
        if (mp3Elapsed >= target) {
          if (dfPlayerOk) myDFPlayer.stop();
          isRingInProgress = false;
          bellState = 0;
          Serial.println("MP3 detenido, campana inactiva.");
        }
      }
      break;
    default:
      break;
  }
}

// -----------------------------------------------------------------------------
// checkButtons_nonBlocking: debounce por millis y detección por flanco
// No bloquea el loop ni llama a funciones de red ni heavy ops
// -----------------------------------------------------------------------------
void checkButtons_nonBlocking() {
  bool emergencyState = digitalRead(EMERGENCY_BUTTON_PIN);
  bool disableState = digitalRead(DISABLE_BUTTON_PIN);
  unsigned long now = millis();

  // Emergencia (flanco FALLING: HIGH->LOW)
  if (emergencyState != lastEmergencyState) {
    lastDebounceEmergency = now;
    lastEmergencyState = emergencyState;
  }
  if ((now - lastDebounceEmergency) > debounceDelay) {
    static bool emergencyHandled = false;
    if (emergencyState == LOW && !emergencyHandled) {
      // pulsado
      if (!isRingInProgress) startBell(false);
      emergencyHandled = true;
    } else if (emergencyState == HIGH && emergencyHandled) {
      // liberado
      emergencyHandled = false;
    }
  }

  // Disable toggle (on release to avoid multiple toggles)
  if (disableState != lastDisableState) {
    lastDebounceDisable = now;
    lastDisableState = disableState;
  }
  if ((now - lastDebounceDisable) > debounceDelay) {
    static bool lastDisableHandledState = HIGH;
    // Detectar flanco FALLING (press) y esperar release para toggle (prevenir rebotes múltiples)
    if (disableState == LOW && lastDisableHandledState == HIGH) {
      // recordamos que se presionó; no hacemos el toggle aún, esperamos release
      lastDisableHandledState = LOW;
    } else if (disableState == HIGH && lastDisableHandledState == LOW) {
      // release -> toggle
      timbreEnabled = !timbreEnabled;
      Serial.print("Timbre ");
      Serial.println(timbreEnabled ? "ACTIVADO por BOTON" : "DESACTIVADO por BOTON");
      saveSchedules(); // guardamos estado
      lastDisableHandledState = HIGH;
    }
  }
}

// -----------------------------------------------------------------------------
// SPIFFS: cargar y guardar horarios
// -----------------------------------------------------------------------------
void loadSchedules() {
  if (SPIFFS.exists("/config.json")) {
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile) {
      size_t size = configFile.size();
      std::unique_ptr<char[]> buf(new char[size + 1]);
      configFile.readBytes(buf.get(), size);
      buf[size] = '\0';

      DynamicJsonDocument doc(4096);
      DeserializationError error = deserializeJson(doc, buf.get());

      if (error) {
        Serial.println(F("Fallo al leer config.json, usando valores por defecto."));
        configFile.close();
        goto load_default_config;
      } else {
        activeSchedule = doc["activeSchedule"] | 0;
        timbreEnabled = doc["timbreEnabled"] | true;

        JsonArray namesArray = doc["scheduleNames"];
        if (namesArray && namesArray.size() == 3) {
          for (int s = 0; s < 3; s++) scheduleNames[s] = namesArray[s].as<String>();
        } else {
          scheduleNames[0] = "Horario 1";
          scheduleNames[1] = "Horario 2";
          scheduleNames[2] = "Horario 3";
        }

        JsonArray allSchedulesJson = doc["schedules"];
        if (allSchedulesJson) {
          for (int s = 0; s < 3; s++) {
            JsonArray currentScheduleJsonArray = allSchedulesJson[s];
            if (currentScheduleJsonArray) {
              schedules[s].clear();
              for (int i = 0; i < currentScheduleJsonArray.size(); i++) {
                BellTime bell;
                bell.hour = currentScheduleJsonArray[i]["hour"] | 0;
                bell.minute = currentScheduleJsonArray[i]["minute"] | 0;
                bell.isEndOfDay = currentScheduleJsonArray[i]["isEndOfDay"] | false;
                bell.name = currentScheduleJsonArray[i]["name"] | "";
                schedules[s].push_back(bell);
              }
              scheduleCount[s] = schedules[s].size();
            } else {
              scheduleCount[s] = 0;
              schedules[s].clear();
            }
          }
        } else {
          for (int s = 0; s < 3; s++) { scheduleCount[s] = 0; schedules[s].clear(); }
        }
      }
      configFile.close();
      return;
    }
  }

  load_default_config:
  Serial.println("config.json no encontrado o corrupto, creando configuración por defecto.");
  for (int s = 0; s < 3; s++) { schedules[s].clear(); scheduleCount[s] = 0; }

  scheduleNames[0] = "Horario 1 (Defecto)";
  schedules[0].push_back({6,10,false,""});
  schedules[0].push_back({7,5,false,""});
  schedules[0].push_back({8,0,false,""});
  schedules[0].push_back({8,55,false,""});
  schedules[0].push_back({9,50,false,"Descanso"});
  schedules[0].push_back({10,20,false,""});
  schedules[0].push_back({11,15,false,""});
  schedules[0].push_back({12,10,false,""});
  schedules[0].push_back({13,5,false,"Fin de jornada"});
  scheduleCount[0] = schedules[0].size();

  scheduleNames[1] = "Horario 2 (Defecto)";
  schedules[1].push_back({6,10,false,""});
  schedules[1].push_back({7,0,false,""});
  schedules[1].push_back({7,50,false,""});
  schedules[1].push_back({8,35,false,""});
  schedules[1].push_back({9,20,false,"Primer descanso"});
  schedules[1].push_back({9,55,false,""});
  schedules[1].push_back({10,40,false,""});
  schedules[1].push_back({11,25,false,""});
  schedules[1].push_back({12,10,true,"Fin de jornada"});
  scheduleCount[1] = schedules[1].size();

  scheduleNames[2] = "Horario 3 (Defecto)";
  schedules[2].push_back({6,10,false,""});
  schedules[2].push_back({7,0,false,""});
  schedules[2].push_back({7,50,false,""});
  schedules[2].push_back({8,40,false,"Primer Descanso"});
  schedules[2].push_back({9,5,false,""});
  schedules[2].push_back({9,55,false,""});
  schedules[2].push_back({10,45,false,"Segundo Descanso"});
  schedules[2].push_back({11,10,false,""});
  schedules[2].push_back({12,0,true,""});
  schedules[2].push_back({12,0,true,"Fin de jornada"});
  scheduleCount[2] = schedules[2].size();

  activeSchedule = 0;
  timbreEnabled = true;
  saveSchedules();
}

void saveSchedules() {
  DynamicJsonDocument doc(4096);

  doc["activeSchedule"] = activeSchedule;
  doc["timbreEnabled"] = timbreEnabled;

  JsonArray namesRoot = doc.createNestedArray("scheduleNames");
  for (int s = 0; s < 3; s++) namesRoot.add(scheduleNames[s]);

  JsonArray schedulesRoot = doc.createNestedArray("schedules");
  for (int s = 0; s < 3; s++) {
    JsonArray currentScheduleArray = schedulesRoot.createNestedArray();
    for (int i = 0; i < scheduleCount[s]; i++) {
      JsonObject bell = currentScheduleArray.add<JsonObject>();
      bell["hour"] = schedules[s][i].hour;
      bell["minute"] = schedules[s][i].minute;
      bell["isEndOfDay"] = schedules[s][i].isEndOfDay;
      bell["name"] = schedules[s][i].name;
    }
  }

  File configFile = SPIFFS.open("/config.json", "w");
  if (configFile) {
    serializeJson(doc, configFile);
    configFile.close();
    Serial.println("Configuracion guardada en SPIFFS.");
  } else {
    Serial.println("ERROR: No se pudo abrir config.json para escribir.");
  }
}

// -----------------------------------------------------------------------------
// Processor para inyectar variables en index.html si lo usas (%%VARIABLE%%)
// -----------------------------------------------------------------------------
String processor(const String &var) {
  if (var == "ACTIVE_SCHEDULE") return String(activeSchedule);
  if (var == "SCHEDULE1_NAME") return scheduleNames[0];
  if (var == "SCHEDULE2_NAME") return scheduleNames[1];
  if (var == "SCHEDULE3_NAME") return scheduleNames[2];
  if (var == "CURRENT_TIME") return getCurrentTimeString();
  if (var == "IP") return localIpStr;
  if (var == "BELL_STATE") {
    switch(bellCurrentState) {
      case BELL_ACTIVE: return "Activo";
      case BELL_PAUSED_WEEKEND: return "Pausa FDS";
      case BELL_PAUSED_HOLIDAY: return "Pausa Festivo";
      case BELL_DEACTIVATED_MANUAL: return "Desactivado";
    }
  }
  return String();
}

// -----------------------------------------------------------------------------
// setupWebServer: rutas y endpoints
// -----------------------------------------------------------------------------
void setupWebServer() {
  // Página principal
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });

  // Archivos estáticos
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/style.css", "text/css");
  });
  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/script.js", "application/javascript");
  });

  // Estado en JSON para AJAX
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();

    DynamicJsonDocument doc(512);
    doc["activeSchedule"] = activeSchedule;
    doc["timbreEnabled"] = timbreEnabled;
    doc["bellOperationalState"] = (int)bellCurrentState;
    doc["currentTime"] = getCurrentTimeString();
    doc["currentDate"] = getCurrentDateString();
    doc["ip"] = localIpStr;
    doc["institution"] = institutionName;

    JsonArray namesJson = doc.createNestedArray("scheduleNames");
    for (int s = 0; s < 3; s++) namesJson.add(scheduleNames[s]);

    JsonArray allSchedulesJson = doc.createNestedArray("schedules");
    for (int s = 0; s < 3; s++) {
      JsonArray cur = allSchedulesJson.createNestedArray();
      for (int i = 0; i < scheduleCount[s]; i++) {
        JsonObject bell = cur.add<JsonObject>();
        bell["hour"] = schedules[s][i].hour;
        bell["minute"] = schedules[s][i].minute;
        bell["isEndOfDay"] = schedules[s][i].isEndOfDay;
        bell["name"] = schedules[s][i].name;
      }
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // Cambiar activo
  server.on("/api/setActive", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();

    if (request->hasParam("schedule", true)) {
      int newSchedule = request->getParam("schedule", true)->value().toInt();
      if (newSchedule >= 0 && newSchedule < 3) {
        activeSchedule = newSchedule;
        saveSchedules();
        request->send(200, "text/plain", "Configuración cambiada");
      } else request->send(400, "text/plain", "Valor inválido");
    } else request->send(400, "text/plain", "Falta parámetro schedule");
  });

  // Toggle timbre manual
  server.on("/api/toggleTimbre", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    timbreEnabled = !timbreEnabled;
    saveSchedules();
    request->send(200, "text/plain", timbreEnabled ? "Timbre activado" : "Timbre desactivado");
  });

  // Activar timbre manual ahora
  server.on("/api/ringNow", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    if (!isRingInProgress) startBell(false);
    request->send(200, "text/plain", "Timbre activado manualmente");
  });

  // Actualizar horario completo (JSON)
  server.on("/api/updateSchedule", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();

    if (!request->hasParam("data", true)) {
      request->send(400, "text/plain", "Faltan datos");
      return;
    }

    String jsonData = request->getParam("data", true)->value();
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, jsonData);
    if (error) {
      request->send(400, "text/plain", "JSON inválido: " + String(error.c_str()));
      return;
    }

    int scheduleIndex = doc["scheduleIndex"];
    if (scheduleIndex < 0 || scheduleIndex > 2) {
      request->send(400, "text/plain", "Índice inválido");
      return;
    }

    scheduleNames[scheduleIndex] = doc["name"].as<String>();
    JsonArray bells = doc["bells"].as<JsonArray>();
    schedules[scheduleIndex].clear();
    scheduleCount[scheduleIndex] = bells.size();
    for (JsonObject bell : bells) {
      BellTime nb;
      nb.hour = bell["hour"].as<int>();
      nb.minute = bell["minute"].as<int>();
      nb.isEndOfDay = bell["isEndOfDay"].as<bool>();
      nb.name = bell["name"].as<String>();
      schedules[scheduleIndex].push_back(nb);
    }
    saveSchedules();
    request->send(200, "text/plain", "Configuración actualizada");
  });

  // Set time
  server.on("/api/setTime", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();

    if (!request->hasParam("hour", true) || !request->hasParam("minute", true) ||
        !request->hasParam("dayOfMonth", true) || !request->hasParam("month", true) ||
        !request->hasParam("year", true)) {
      request->send(400, "text/plain", "Faltan parámetros");
      return;
    }

    int hour = request->getParam("hour", true)->value().toInt();
    int minute = request->getParam("minute", true)->value().toInt();
    int dayOfMonth = request->getParam("dayOfMonth", true)->value().toInt();
    int month = request->getParam("month", true)->value().toInt();
    int year = request->getParam("year", true)->value().toInt();

    rtc.setTime(0, minute, hour, dayOfMonth, month, year);

    // NUEVO: también actualizamos el DS3231 cuando el usuario pone la hora manualmente
    if (ds3231Ok) {
      ds3231.adjust(DateTime(year, month, dayOfMonth, hour, minute, 0));
    }

    request->send(200, "text/plain", "Hora actualizada");
  });

  // ---- Nombre de la institución ----
  server.on("/api/institution", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    String name = request->hasParam("name", true) ? request->getParam("name", true)->value() : "";
    name.trim();
    if (name.length() > 40) {
      request->send(400, "text/plain", "El nombre es muy largo (máximo 40 caracteres)");
      return;
    }
    institutionName = name;
    Preferences cfg;                    // objeto local: evita choques con el loop principal
    cfg.begin("config", false);
    cfg.putString("inst", institutionName);
    cfg.end();
    request->send(200, "text/plain", "Nombre guardado");
  });

  // ---- WiFi desde la interfaz ----
  server.on("/api/wifi/current", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    DynamicJsonDocument doc(256);
    bool conectado = (WiFi.status() == WL_CONNECTED);
    doc["connected"] = conectado;
    doc["ssid"] = conectado ? WiFi.SSID() : String("");
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    DynamicJsonDocument doc(2048);
    JsonArray nets = doc.createNestedArray("networks");
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
      WiFi.scanNetworks(true);      // inicia búsqueda sin bloquear
      doc["scanning"] = true;
    } else if (n == WIFI_SCAN_RUNNING) {
      doc["scanning"] = true;
    } else {
      for (int i = 0; i < n && i < 20; i++) {
        String name = WiFi.SSID(i);
        if (name.length() > 0) nets.add(name);
      }
      WiFi.scanDelete();
      doc["scanning"] = false;
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/api/wifi/set", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    if (!request->hasParam("ssid", true)) {
      request->send(400, "text/plain", "Falta el nombre de la red");
      return;
    }
    String newSsid = request->getParam("ssid", true)->value();   // sin trim: algunas redes terminan en espacio
    String newPass = request->hasParam("password", true) ? request->getParam("password", true)->value() : "";
    if (newSsid.length() < 1 || newSsid.length() > 32) {
      request->send(400, "text/plain", "Nombre de red inválido");
      return;
    }
    if (newPass.length() > 0 && (newPass.length() < 8 || newPass.length() > 63)) {
      request->send(400, "text/plain", "La clave debe tener entre 8 y 63 caracteres");
      return;
    }
    prefs.begin("wifi", false);
    prefs.putString("ssid", newSsid);
    prefs.putString("pass", newPass);
    prefs.end();
    request->send(200, "text/plain", "Red guardada. El timbre se reiniciará para conectarse.");
    restartAt = millis() + 2000;   // da tiempo a que la respuesta llegue al navegador
  });

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  server.begin();
}
