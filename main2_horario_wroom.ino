// main.ino (versión corregida y completa)
// Placa: ESP32 DevKitC (ESP32-WROOM-32) esp32 3.0.7

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
#include "credentials.h" // Debes tener SSID, PASSWORD, HTTP_USERNAME, HTTP_PASSWORD aquí

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

// Web server
AsyncWebServer server(80);

// Credenciales (del archivo credentials.h)
const char *ssid = SSID;
const char *password = PASSWORD;
const char *http_username = HTTP_USERNAME;
const char *http_password = HTTP_PASSWORD;

// Control de canciones
const int totalSongs = 366;
bool songPlayed[totalSongs + 1];
int lastPlayedSong = 0;

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

// Festivos 2025
const byte holiday_count = 18;
const uint32_t holidays[holiday_count] = {
  20250101,20250106,20250324,20250417,20250418,20250501,20250512,20250602,
  20250613,20250630,20250720,20250807,20250818,20251013,20251103,20251117,
  20251208,20251225
};

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

// ---------- IMPLEMENTACIÓN ----------

void setup() {
  Serial.begin(115200);

  // Inicializar array de canciones
  for (int i = 1; i <= totalSongs; i++) songPlayed[i] = false;

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
  }

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Error al montar SPIFFS");
  }

  // Cargar horarios
  loadSchedules();

  // OLED
  Wire.begin();
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

  // NTP / RTC
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

  WiFi.begin(ssid, password);

  // Esperar un tiempo prudente sin bloquear indefinidamente
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 8000) {
    delay(200);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    localIpStr = WiFi.localIP().toString();
    Serial.println("\nWiFi Conectado!");
    Serial.println(localIpStr);
  } else {
    Serial.println("\nNo se pudo conectar a WiFi, activando AP.");
    WiFi.softAP("TimbreEscolar", "password");
    localIpStr = "AP: TimbreEscolar";
  }
}

void setLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Error obteniendo hora NTP");
    return;
  }
  rtc.setTimeStruct(timeinfo);
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
  uint32_t fechaNum = (rtc.getYear() * 10000 + (rtc.getMonth() + 1) * 100 + rtc.getDay());
  for (int i = 0; i < holiday_count; i++) {
    if (holidays[i] == fechaNum) return true;
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
    display.setCursor(0,0);
    display.println(localIpStr);
    display.setCursor(0,11);
    display.print("Config: ");
    display.println(scheduleNames[activeSchedule]);
    display.setCursor(0,22);
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
      if (value >= 1 && value <= totalSongs) songPlayed[value] = false;
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

  int randomSong;
  int attempts = 0;
  bool foundUnplayed = false;

  while (attempts < 10 && !foundUnplayed) {
    randomSong = random(1, totalSongs + 1);
    if (!songPlayed[randomSong]) {
      foundUnplayed = true;
      songPlayed[randomSong] = true;
      lastPlayedSong = randomSong;
    }
    attempts++;
  }

  if (!foundUnplayed) {
    randomSong = random(1, totalSongs + 1);
    Serial.println("¡Timbre activado! (Reproducción forzada - posible repetición)");
  } else {
    Serial.println("¡Timbre activado! (Canción " + String(randomSong) + ")");
  }

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
          // reset songs if all played
          bool allPlayed = true;
          for (int i = 1; i <= totalSongs; i++) {
            if (!songPlayed[i]) { allPlayed = false; break; }
          }
          if (allPlayed) {
            for (int i = 1; i <= totalSongs; i++) songPlayed[i] = false;
            Serial.println("Todas las canciones reproducidas, reseteando lista.");
          }
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
    doc["currentDate"] = getCurrentTimeString(); // si quieres otra forma, se puede adaptar
    doc["ip"] = localIpStr;

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
    request->send(200, "text/plain", "Hora actualizada");
  });

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  server.begin();
}
