#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <time.h>
#include <ESP32Time.h>
#include <esp_task_wdt.h> // Comentado si no se usa esp_task_wdt_reconfigure
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Wire.h"
#include "credentials.h"

// Definición de pines
#define RELAY_PIN 18
#define EMERGENCY_BUTTON_PIN 3
#define DISABLE_BUTTON_PIN 2

// Configuración DFPlayer
SoftwareSerial mySoftwareSerial(14, 20);  // RX, TX
DFRobotDFPlayerMini myDFPlayer;

// Sync time co
const char* ntpServer = "co.pool.ntp.org";   //Servidor NTP para sincronizar el Time
const long  gmtOffset_sec = -18000;         // Establecer el GMT (3600 segundos = 1 hora) paa Colombia es GMT -5) es decir  -18000
const int   daylightOffset_sec = 0;

// Reloj RTC
ESP32Time rtc;

// Servidor web
AsyncWebServer server(80);

// Credenciales WiFi - Debes cambiarlas
const char *ssid = SSID;
const char *password = PASSWORD;

// Credenciales de acceso a la web
const char *http_username = HTTP_USERNAME;
const char *http_password = HTTP_PASSWORD;
const int totalSongs = 28; // Asegúrate de que coincida con el número real de archivos MP3
bool songPlayed[totalSongs + 1]; // Índice 0 no se usa, +1 para indexar desde 1
int lastPlayedSong = 0;

// Variables para control del sistema
bool timbreEnabled = true;
int activeSchedule = 0;  // 0, 1, o 2 según la configuración activa

// Estructura para los horarios
struct BellTime {
  int hour;
  int minute;
  bool isEndOfDay;
};

//Tiempo encendido timbre
int bell_time_duration = 6000;  //Seis segundos
int mp3_time_duration = 60000;  //Un minuto

// Variables globales para control no bloqueante
unsigned long previousMillis = 0;
unsigned long bellStartTime = 0;
int bellState = 0; // 0: inactivo, 1: primera campana, 2: pausa para campana doble, 3: segunda campana, 4: fade out
bool isRingInProgress = false;
bool isEndOfDayBell = false;

//Pantalla oled 128x32
#define I2C_SDA 1
#define I2C_SCL 0

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 32 // OLED display height, in pixels

// Dirección I2C del display
#define OLED_ADDR 0x3C

// Crear objeto display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Arreglo para almacenar las tres configuraciones
std::vector<BellTime> schedules[3];
int scheduleCount[3] = { 0, 0, 0 };
String scheduleNames[3] = { "Horario 1", "Horario 2", "Horario 3" };

// Colombia holidays 2025
const byte holiday_count = 18;
const uint32_t holidays[holiday_count] = {
  20250101,  // Año Nuevo
  20250106,  // Reyes Magos
  20250324,  // San José
  20250417,  // Jueves Santo
  20250418,  // Viernes Santo
  20250501,  // Día del Trabajo
  20250512,  // Ascensión del Señor
  20250602,  // Corpus Christi
  20250613,  // Sagrado Corazón
  20250630,  // San Pedro y San Pablo
  20250720,  // Independencia de Colombia
  20250807,  // Batalla de Boyacá
  20250818,  // Asunción de la Virgen
  20251013,  // Día de la Raza
  20251103,  // Todos los Santos
  20251117,  // Independencia de Cartagena
  20251208,  // Inmaculada Concepción
  20251225   // Navidad
};

// --- INICIO NUEVAS VARIABLES Y DEFINICIONES PARA MANEJO DE HORA SIN INTERNET ---
#define LAST_TIME_FILE "/last_time.txt"
bool timeSetSuccessfully = false; // Indica si la hora del RTC se ha configurado correctamente
// --- FIN NUEVAS VARIABLES Y DEFINICIONES ---


// --- INICIO NUEVAS FUNCIONES PARA MANEJO DE HORA SIN INTERNET ---
// Guarda la hora actual del RTC a SPIFFS
void saveTimeRtcToSPIFFS() {
  if (!timeSetSuccessfully) { // No guardar si la hora no es válida o no está seteada
    Serial.println(F("No se guarda la hora en SPIFFS, RTC no tiene hora válida o no seteada."));
    return;
  }
  File timeFile = SPIFFS.open(LAST_TIME_FILE, "w");
  if (!timeFile) {
    Serial.println(F("Error al abrir archivo para guardar hora."));
    return;
  }
  time_t now = rtc.getEpoch(); // Obtener segundos desde epoch
  timeFile.print(now);
  timeFile.close();
  Serial.print(F("Hora guardada en SPIFFS (epoch): "));
  Serial.println(now);
  Serial.print(F("Equivalente a: "));
  Serial.println(rtc.getTimeDate(true));
}

// Carga la hora desde SPIFFS al RTC
bool loadTimeRtcFromSPIFFS() {
  if (SPIFFS.exists(LAST_TIME_FILE)) {
    File timeFile = SPIFFS.open(LAST_TIME_FILE, "r");
    if (!timeFile) {
      Serial.println(F("Error al abrir archivo para cargar hora."));
      return false;
    }
    String timeStr = timeFile.readStringUntil('\n');
    timeFile.close();
    if (timeStr.length() > 0) {
      time_t epochTime = atol(timeStr.c_str());
      if (epochTime > 946684800) { // Validar que sea después del año 2000 aprox.
        rtc.setTime(epochTime);
        Serial.print(F("Hora cargada desde SPIFFS: "));
        Serial.println(rtc.getTimeDate(true));
        return true;
      } else {
        Serial.println(F("Timestamp en SPIFFS parece inválido."));
        return false;
      }
    }
  }
  Serial.println(F("No se encontró archivo de hora guardada o está vacío."));
  return false;
}
// --- FIN NUEVAS FUNCIONES PARA MANEJO DE HORA SIN INTERNET ---


void setup() {
  Serial.begin(115200);
  // Inicializar el array de canciones reproducidas
  for (int i = 1; i <= totalSongs; i++) {
    songPlayed[i] = false;
  }

  // Inicializar pines
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  pinMode(EMERGENCY_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DISABLE_BUTTON_PIN, INPUT_PULLUP);

  // Inicializar DFPlayer
  mySoftwareSerial.begin(9600);
  if (!myDFPlayer.begin(mySoftwareSerial)) {
    Serial.println(F("Error al inicializar DFPlayer"));
  }
  myDFPlayer.setTimeOut(500);
  myDFPlayer.volume(30);  // Volumen (0-30)

  // Inicializar SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println(F("Error al montar SPIFFS"));
  }

  // Cargar configuraciones guardadas
  loadSchedules();

  // Pantalla oled
  Wire.begin(I2C_SDA, I2C_SCL);
  if(!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println(F("Error al inicializar SSD1306"));
    for(;;); // No continuar si hay error
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(F("Iniciando..."));
  display.display();
  delay(1000);

  // Conectar a WiFi
  setupWiFi(); // setupWiFi intentará conectar o iniciará AP

  // --- INICIO LÓGICA DE SINCRONIZACIÓN DE HORA MEJORADA ---
  Serial.println(F("Configurando hora..."));
  timeSetSuccessfully = false; 

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("Intentando sincronizar hora con NTP..."));
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10000)) { // Intenta por 10 segundos
      rtc.setTimeStruct(timeinfo); // Configura ESP32Time rtc con la hora NTP
      Serial.println(F("Hora NTP obtenida y RTC configurado."));
      Serial.println(rtc.getTimeDate(true)); 
      if (rtc.getYear() > 2023) { // Chequeo básico de que el año es razonable
        timeSetSuccessfully = true;
        saveTimeRtcToSPIFFS(); // Guardar la hora válida obtenida por NTP
      } else {
        Serial.println(F("Año obtenido de NTP no parece válido. RTC no actualizado con NTP."));
      }
    } else {
      Serial.println(F("Fallo al obtener hora de NTP."));
    }
  } else {
    Serial.println(F("No hay conexión WiFi para NTP."));
  }

  if (!timeSetSuccessfully) { // Si NTP falló o no hubo WiFi
    Serial.println(F("Intentando cargar hora desde SPIFFS..."));
    if (loadTimeRtcFromSPIFFS()) {
      // rtc ya fue configurado por loadTimeRtcFromSPIFFS()
      if (rtc.getYear() > 2023) { // Chequeo básico
        Serial.println(F("Hora cargada desde SPIFFS y RTC configurado."));
        timeSetSuccessfully = true;
      } else {
        Serial.println(F("Hora de SPIFFS no parece válida, RTC podría tener hora por defecto."));
        // Opcional: resetear a una fecha "inválida" conocida para hacerlo obvio
        // rtc.setTime(0, 0, 0, 1, 1, 2000); 
      }
    } else {
      Serial.println(F("No se pudo cargar hora desde SPIFFS. RTC podría tener hora por defecto o incorrecta."));
      // rtc.setTime(0, 0, 0, 1, 1, 2000); // Opcional
    }
  }

  if (!timeSetSuccessfully) {
    Serial.println(F("ADVERTENCIA: La hora del sistema NO ESTÁ configurada correctamente."));
    Serial.println(F("Por favor, conecte a WiFi para NTP o configurela manualmente via Web."));
    // Por defecto, ESP32Time puede iniciar en 1/1/2000 si no se setea
  }
  // --- FIN LÓGICA DE SINCRONIZACIÓN DE HORA ---

  // Configurar servidor web
  setupWebServer();

  Serial.println(F("Sistema listo!"));
  Serial.print(F("Hora actual RTC: "));
  Serial.println(getCurrentTimeString());
  if (timeSetSuccessfully) {
    Serial.print(F("Fecha actual RTC: "));
    Serial.println(rtc.getDate(true)); // true para formato DD-MM-YYYY
  } else {
    Serial.println(F("¡¡¡LA HORA NO ESTÁ CONFIGURADA CORRECTAMENTE!!!"));
    Serial.print(F("Fecha y hora actual (podría ser incorrecta): "));
    Serial.println(rtc.getTimeDate(true));
  }
  Serial.println("Config activa: " + scheduleNames[activeSchedule]);
}

void loop() {
  checkButtons();
  if (timeSetSuccessfully) { // Solo chequear horarios si la hora está configurada
    checkSchedule();
  }
  handleBellState();
  updateDisplay();

  if (myDFPlayer.available()) {
    printDetail(myDFPlayer.readType(), myDFPlayer.read());
  }
}

void updateDisplay() {
  static unsigned long lastDisplayUpdate = 0;
  unsigned long currentMillis = millis();

  if (currentMillis - lastDisplayUpdate >= 1000) { // Actualizar cada segundo
    lastDisplayUpdate = currentMillis;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

    if (isRingInProgress){
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.println(F("Timbre"));
      display.setCursor(0, 16);
      display.println(F("Activado"));
    } else if (!timeSetSuccessfully) { // Si la hora no está configurada correctamente
      display.setCursor(0, 0);
      display.println(F("HORA NO AJUSTADA"));
      display.setCursor(0, 10);
      display.println(F("Conecte WiFi o"));
      display.setCursor(0, 20);
      display.println(F("configurela web"));
    } else {
      // Mostrar IP
      display.setCursor(0, 0);
      if (WiFi.status() == WL_CONNECTED) {
        display.println(WiFi.localIP().toString());
      } else {
        // En modo AP, mostrar la IP del AP si está disponible
        display.print(F("AP: TimbreEscolar"));
        // display.println(WiFi.softAPIP()); // Descomentar si quieres mostrar la IP del AP
      }
      
      // Mostrar horario activo (asegúrate que quepa)
      display.setCursor(0, 11);
      String currentScheduleName = scheduleNames[activeSchedule];
      if (currentScheduleName.length() > 20) { // Ajusta 20 al máximo de caracteres que caben
          currentScheduleName = currentScheduleName.substring(0, 19) + ".";
      }
      display.println(currentScheduleName);

      // Mostrar hora actual
      display.setCursor(0, 22);
      display.print(F("Hora: "));
      display.println(getCurrentTimeString());
    }
    display.display();
  }
}

void setupWiFi() {
  Serial.println(F("Conectando WiFi..."));
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\nWiFi Conectado!"));
    Serial.println(WiFi.localIP().toString());
  } else {
    Serial.println(F("\nError WiFi o no configurado!"));
    Serial.println(F("Modo AP activado: TimbreEscolar"));
    WiFi.softAP("TimbreEscolar", "password"); // Considera una contraseña más segura o configurable
    Serial.print(F("AP IP address: "));
    Serial.println(WiFi.softAPIP());
  }
}

// La función setLocalTime() original ya no es necesaria explícitamente aquí,
// la configuración de 'rtc' se maneja en setup()
/*
void setLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Error obteniendo hora");
    return;
  }
  rtc.setTimeStruct(timeinfo);
}
*/

String getCurrentTimeString() {
  if (!timeSetSuccessfully && rtc.getYear() < 2024) { // Si la hora no está bien y el año es irreal
    return "--:--:--";
  }
  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
  return String(timeStr);
}

bool weekend(int day) {
  // day: 0 = domingo, 6 = sábado (según ESP32Time)
  return (day == 0 || day == 6);
}

bool holiday() {
  if (!timeSetSuccessfully) return false; // No podemos determinar festivos sin hora correcta

  // ESP32Time rtc.getMonth() devuelve 0-11, por eso +1
  uint32_t fechaNum = (uint32_t)rtc.getYear() * 10000 + (uint32_t)(rtc.getMonth() + 1) * 100 + (uint32_t)rtc.getDay();

  for (int i = 0; i < holiday_count; i++) {
    if (holidays[i] == fechaNum) {
      Serial.println("¡Es festivo! (" + String(fechaNum) + ") Timbre Off");
      return true;
    }
  }
  return false;
}

void checkSchedule() {
  // Esta función solo debería ejecutarse si timeSetSuccessfully es true
  // (ya se verifica en loop antes de llamar a checkSchedule)

  bool isWeekendOrHoliday = false; 

  if (holiday()) {
    // Mensaje ya se imprime en holiday()
    isWeekendOrHoliday = true;
  } else if (weekend(rtc.getDayofWeek())) { 
    Serial.println(F("Es fin de semana. Timbre desactivado temporalmente."));
    isWeekendOrHoliday = true;
  }

  if (isWeekendOrHoliday) {
    // No se desactiva timbreEnabled permanentemente aquí,
    // solo se evita que suene en este ciclo.
    // timbreEnabled global se maneja por botón/web.
    return; 
  }

  if (!timbreEnabled) {
      // Serial.println("Timbre desactivado manualmente (botón/web)."); // Mensaje opcional
      return;
  }

  int currentHour = rtc.getHour(true);
  int currentMinute = rtc.getMinute();
  
  // Revisar solo una vez por minuto para evitar activaciones múltiples si el segundo no es 0
  // o si hay algún pequeño drift.
  static int lastCheckedMinute = -1;
  if (currentMinute == lastCheckedMinute && rtc.getSecond() > 5) { // Solo chequear una vez por minuto
      return;
  }
  // Solo revisar cuando los segundos sean 0 para evitar activaciones múltiples
  // if (rtc.getSecond() != 0) return; // Esta línea es muy estricta, puede fallar.
                                    // Es mejor chequear una vez por minuto.

  lastCheckedMinute = currentMinute; // Actualizar el último minuto chequeado

  for (int i = 0; i < scheduleCount[activeSchedule]; i++) {
    if (schedules[activeSchedule][i].hour == currentHour && schedules[activeSchedule][i].minute == currentMinute) {
      Serial.print(F("Horario coincidente: "));
      Serial.print(schedules[activeSchedule][i].hour);
      Serial.print(":");
      Serial.println(schedules[activeSchedule][i].minute);
      startBell(schedules[activeSchedule][i].isEndOfDay);
      break; 
    }
  }
}

void printDetail(uint8_t type, int value){
  // (Sin cambios en esta función)
  // ... tu código original de printDetail ...
switch (type) {
    case TimeOut:
      Serial.println(F("Time Out!"));
      break;
    case WrongStack:
      Serial.println(F("Stack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("Card Online!"));
      break;
    case DFPlayerUSBInserted:
      Serial.println("USB Inserted!");
      break;
    case DFPlayerUSBRemoved:
      Serial.println("USB Removed!");
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("Number:"));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      // Reiniciar la marca de la canción reproducida
      if (value >= 1 && value <= totalSongs) {
        songPlayed[value] = false;
      }
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy:
          Serial.println(F("Card not found"));
          break;
        case Sleeping:
          Serial.println(F("Sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("Get Wrong Stack"));
          break;
        case CheckSumNotMatch:
          Serial.println(F("Check Sum Not Match"));
          break;
        case FileIndexOut:
          Serial.println(F("File Index Out of Bound"));
          break;
        case FileMismatch:
          Serial.println(F("Cannot Find File"));
          break;
        case Advertise:
          Serial.println(F("In Advertise"));
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}

void startBell(bool endOfDay) {
  if (isRingInProgress) return;

  int randomSong;
  int attempts = 0;
  bool foundUnplayed = false;
  int songsAvailableToPlay = 0;

  for (int i = 1; i <= totalSongs; i++) {
    if (!songPlayed[i]) {
      songsAvailableToPlay++;
    }
  }

  if (songsAvailableToPlay == 0) { // Todas las canciones ya se reprodujeron
    Serial.println(F("Todas las canciones reproducidas, reiniciando flags."));
    for (int i = 1; i <= totalSongs; i++) {
      songPlayed[i] = false;
    }
    songsAvailableToPlay = totalSongs;
  }
  
  // Intentar encontrar una canción no reproducida
  do {
    randomSong = random(1, totalSongs + 1);
    if (!songPlayed[randomSong]) {
      foundUnplayed = true;
    }
    attempts++;
  } while (!foundUnplayed && attempts < (totalSongs * 2)); // Intentar un número razonable de veces

  if (!foundUnplayed) { // Si aún no se encontró (muy improbable si hay disponibles)
    randomSong = random(1, totalSongs + 1); // Escoger una al azar de todas formas
    Serial.println(F("¡Timbre activado! (Reproducción forzada aleatoria - posible repetición)"));
  } else {
     Serial.println("¡Timbre activado! (Canción " + String(randomSong) + ")");
  }

  songPlayed[randomSong] = true; // Marcar la canción como reproducida
  lastPlayedSong = randomSong;


  digitalWrite(RELAY_PIN, LOW);
  myDFPlayer.play(randomSong);
  myDFPlayer.volume(30);

  isRingInProgress = true;
  isEndOfDayBell = endOfDay;
  bellStartTime = millis();
  bellState = 1;
}

void handleBellState() {
  // (Sin cambios en esta función)
  // ... tu código original de handleBellState ...
  if (!isRingInProgress) return;

  unsigned long currentMillis = millis();
  unsigned long elapsedTime = currentMillis - bellStartTime;

  switch (bellState) {
    case 1: // Primera campana
      if (elapsedTime >= bell_time_duration) {
        digitalWrite(RELAY_PIN, HIGH);

        if (isEndOfDayBell) {
          bellStartTime = currentMillis;
          bellState = 2;
        } else {
          bellStartTime = currentMillis;
          bellState = 4;
        }
      }
      break;

    case 2: // Pausa entre campanas (para fin de jornada)
      if (elapsedTime >= 500) {
        digitalWrite(RELAY_PIN, LOW);
        bellStartTime = currentMillis;
        bellState = 3;
      }
      break;

    case 3: // Segunda campana (fin de jornada)
      if (elapsedTime >= bell_time_duration) {
        digitalWrite(RELAY_PIN, HIGH);
        bellStartTime = currentMillis;
        bellState = 4;
      }
      break;

    case 4: // Apagar mp3 y reiniciar variables
      if (elapsedTime >= mp3_time_duration - bell_time_duration) { // Espera a que termine el MP3 (menos la duración del timbre)
        myDFPlayer.stop();
        isRingInProgress = false;
        bellState = 0;
        Serial.println(F("Timbre finalizado."));
      }
      break;
  }
}

void checkButtons() {
  // (Sin cambios en esta función, pero considera el debounce si hay rebotes)
  // ... tu código original de checkButtons ...
  // Botón de emergencia
  if (digitalRead(EMERGENCY_BUTTON_PIN) == LOW) {
    delay(50);   // Debounce
    if (digitalRead(EMERGENCY_BUTTON_PIN) == LOW) {
      Serial.println(F("Botón de emergencia presionado."));
      if (!isRingInProgress) {
        startBell(false);
      }
      while (digitalRead(EMERGENCY_BUTTON_PIN) == LOW) {
        // handleBellState(); // Esto podría ser problemático si se mantiene presionado mucho tiempo
        delay(10); 
      }
    }
  }

  // Botón de desactivar
  if (digitalRead(DISABLE_BUTTON_PIN) == LOW) {
    delay(50);   // Debounce
    if (digitalRead(DISABLE_BUTTON_PIN) == LOW) {
      timbreEnabled = !timbreEnabled;
      Serial.print(F("Timbre globalmente "));
      Serial.println(timbreEnabled ? F("ACTIVADO") : F("DESACTIVADO"));
      // updateDisplay(); // updateDisplay se llama en loop, no es necesario aquí directamente
      while (digitalRead(DISABLE_BUTTON_PIN) == LOW) {
        delay(10);
      }
    }
  }
}


void loadSchedules() {
  // (Sin cambios en esta función)
  // ... tu código original de loadSchedules ...
  if (SPIFFS.exists("/config.json")) {
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile) {
      size_t size = configFile.size();
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);

      DynamicJsonDocument doc(2048); // Aumenta si es necesario
      DeserializationError error = deserializeJson(doc, buf.get());
      if (error) {
        Serial.print(F("Error al deserializar config.json: "));
        Serial.println(error.c_str());
        // Cargar configuración por defecto si hay error
        // return loadDefaultSchedules(); // Llama a una función que cargue los por defecto
      }


      activeSchedule = doc["activeSchedule"] | 0; // Valor por defecto 0 si no existe

      JsonArray scheduleNamesDoc = doc["scheduleNames"];
      if (!scheduleNamesDoc.isNull()) {
        for (int s = 0; s < 3; s++) {
            if (s < scheduleNamesDoc.size()) {
                scheduleNames[s] = scheduleNamesDoc[s].as<String>();
            } else {
                scheduleNames[s] = "Horario " + String(s+1); // Nombre por defecto
            }
        }
      }


      JsonArray schedulesDoc = doc["schedules"];
       if (!schedulesDoc.isNull()) {
        for (int s = 0; s < 3; s++) {
            schedules[s].clear(); // Limpiar horarios existentes para este schedule
            if (s < schedulesDoc.size()) {
                JsonArray schedule_s_Doc = schedulesDoc[s];
                if (!schedule_s_Doc.isNull()) {
                    scheduleCount[s] = schedule_s_Doc.size();
                    for (int i = 0; i < scheduleCount[s]; i++) {
                        JsonObject bellDoc = schedule_s_Doc[i];
                        if (!bellDoc.isNull()) {
                            BellTime bell;
                            bell.hour = bellDoc["hour"] | 0;
                            bell.minute = bellDoc["minute"] | 0;
                            bell.isEndOfDay = bellDoc["isEndOfDay"] | false;
                            schedules[s].push_back(bell);
                        }
                    }
                } else {
                     scheduleCount[s] = 0; // No hay horarios para este schedule
                }
            } else {
                scheduleCount[s] = 0; // No existe este schedule en el JSON
            }
        }
      }
      configFile.close();
      Serial.println(F("Configuraciones cargadas desde SPIFFS."));
    } else {
        Serial.println(F("No se pudo abrir config.json, cargando valores por defecto."));
        //loadDefaultSchedules();
    }
  } else {
    Serial.println(F("config.json no encontrado, cargando valores por defecto."));
    // Configuración por defecto (ejemplo)
    scheduleNames[0] = "Horario Normal";
    scheduleNames[1] = "Horario Corto";
    scheduleNames[2] = "Horario Especial";
    
    // Limpiar cualquier horario previo
    for(int s=0; s<3; ++s) schedules[s].clear();

    scheduleCount[0] = 9;
    schedules[0].push_back({ 7, 0, false });    // 7:00 AM
    schedules[0].push_back({ 8, 0, false });    // 8:00 AM
    schedules[0].push_back({ 9, 0, false });    // 9:00 AM
    schedules[0].push_back({ 10, 0, false });   // 10:00 AM
    schedules[0].push_back({ 10, 30, false });  // 10:30 AM
    schedules[0].push_back({ 11, 0, false });   // 11:00 AM
    schedules[0].push_back({ 12, 0, false });   // 12:00 PM
    schedules[0].push_back({ 13, 0, false });   // 1:00 PM
    schedules[0].push_back({ 14, 0, true });    // 2:00 PM - Fin de jornada
    
    scheduleCount[1] = 0; // Horario 2 vacío por defecto
    scheduleCount[2] = 0; // Horario 3 vacío por defecto

    saveSchedules(); // Guardar la configuración por defecto
  }
}

void saveSchedules() {
  // (Sin cambios en esta función)
  // ... tu código original de saveSchedules ...
  DynamicJsonDocument doc(2048); // Aumenta si tu config es más grande

  doc["activeSchedule"] = activeSchedule;

  JsonArray scheduleNamesDoc = doc.createNestedArray("scheduleNames");
  for (int s = 0; s < 3; s++) {
    scheduleNamesDoc.add(scheduleNames[s]);
  }

  JsonArray schedulesDoc = doc.createNestedArray("schedules");
  for (int s = 0; s < 3; s++) {
    JsonArray schedule_s_Doc = schedulesDoc.createNestedArray();
    for (int i = 0; i < scheduleCount[s]; i++) {
      JsonObject bellDoc = schedule_s_Doc.createNestedObject();
      bellDoc["hour"] = schedules[s][i].hour;
      bellDoc["minute"] = schedules[s][i].minute;
      bellDoc["isEndOfDay"] = schedules[s][i].isEndOfDay;
    }
  }

  File configFile = SPIFFS.open("/config.json", "w");
  if (configFile) {
    if (serializeJson(doc, configFile) == 0) {
      Serial.println(F("Error al escribir en config.json"));
    } else {
      Serial.println(F("Configuraciones guardadas en SPIFFS."));
    }
    configFile.close();
  } else {
    Serial.println(F("No se pudo abrir config.json para escribir."));
  }
}

String processor(const String &var) {
  // (Sin cambios en esta función)
  // ... tu código original de processor ...
  if (var == "ACTIVE_SCHEDULE") {
    return String(activeSchedule);
  }
  if (var == "SCHEDULE1_NAME") {
    return scheduleNames[0];
  }
  if (var == "SCHEDULE2_NAME") {
    return scheduleNames[1];
  }
  if (var == "SCHEDULE3_NAME") {
    return scheduleNames[2];
  }
  if (var == "TIMBRE_STATUS") {
    return timbreEnabled ? "Activado" : "Desactivado";
  }
  // --- AÑADIDO PARA MOSTRAR HORA EN HTML SI ES NECESARIO ---
  if (var == "CURRENT_TIME") {
    return getCurrentTimeString();
  }
  if (var == "CURRENT_DATE") {
    if (timeSetSuccessfully) return rtc.getDate(true); // DD-MM-YYYY
    return "Fecha no disp.";
  }
  if (var == "TIME_SET_STATUS") {
      return timeSetSuccessfully ? "Hora Sincronizada" : "HORA NO AJUSTADA";
  }
  return String();
}

void setupWebServer() {
  // Ruta para la página principal
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    request->send(SPIFFS, "/index.html", String(), false, processor);
  });

  // Ruta para archivos estáticos (CSS, JS)
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/style.css", "text/css");
  });

  server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/script.js", "application/javascript");
  });

  // API para obtener datos actuales
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();

    DynamicJsonDocument doc(2048); // Aumenta si es necesario
    doc["activeSchedule"] = activeSchedule;
    doc["timbreEnabled"] = timbreEnabled;
    doc["currentTime"] = getCurrentTimeString();
    doc["currentDate"] = timeSetSuccessfully ? rtc.getDate(true) : "No Sinc."; // Añadido
    doc["timeSetSuccessfully"] = timeSetSuccessfully; // Añadido

    JsonArray scheduleNamesDoc = doc.createNestedArray("scheduleNames");
    for (int s = 0; s < 3; s++) {
      scheduleNamesDoc.add(scheduleNames[s]);
    }

    JsonArray schedulesDoc = doc.createNestedArray("schedules");
    for (int s = 0; s < 3; s++) {
      JsonArray schedule_s_Doc = schedulesDoc.createNestedArray();
      for (int i = 0; i < scheduleCount[s]; i++) {
        JsonObject bellDoc = schedule_s_Doc.createNestedObject();
        bellDoc["hour"] = schedules[s][i].hour;
        bellDoc["minute"] = schedules[s][i].minute;
        bellDoc["isEndOfDay"] = schedules[s][i].isEndOfDay;
      }
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  // API para cambiar configuración activa
  server.on("/api/setActive", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();

    if (request->hasParam("schedule", true)) {
      int newSchedule = request->getParam("schedule", true)->value().toInt();
      if (newSchedule >= 0 && newSchedule < 3) {
        activeSchedule = newSchedule;
        saveSchedules(); // Guardar el cambio
        Serial.print(F("Horario activo cambiado a: "));
        Serial.println(scheduleNames[activeSchedule]);
        request->send(200, "text/plain", "Configuracion cambiada");
      } else {
        request->send(400, "text/plain", "Valor de configuracion invalido");
      }
    } else {
      request->send(400, "text/plain", "Falta parametro schedule");
    }
  });

  // API para activar/desactivar timbre
  server.on("/api/toggleTimbre", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();

    timbreEnabled = !timbreEnabled;
    Serial.print(F("Timbre globalmente (via web) "));
    Serial.println(timbreEnabled ? F("ACTIVADO") : F("DESACTIVADO"));
    request->send(200, "text/plain", timbreEnabled ? "Timbre activado" : "Timbre desactivado");
  });

  // API para activar timbre manualmente
  server.on("/api/ringNow", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();
    
    if (!timeSetSuccessfully) {
        request->send(400, "text/plain", "Error: La hora del sistema no esta configurada. No se puede activar el timbre.");
        return;
    }
    if (!isRingInProgress) {
      Serial.println(F("Timbre activado manualmente via web."));
      startBell(false); // false para no considerarlo fin de jornada
      request->send(200, "text/plain", "Timbre activado manualmente");
    } else {
      request->send(200, "text/plain", "Timbre ya esta sonando");
    }
  });

  // API para actualizar configuraciones
  server.on("/api/updateSchedule", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();

    if (!request->hasParam("data", true)) {
      request->send(400, "text/plain", "Faltan datos de configuracion");
      return;
    }

    String jsonData = request->getParam("data", true)->value();
    DynamicJsonDocument doc(2048); // Asegúrate que el tamaño es suficiente
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error) {
      Serial.print(F("Error deserializando JSON en updateSchedule: "));
      Serial.println(error.c_str());
      request->send(400, "text/plain", "Error en formato JSON: " + String(error.c_str()));
      return;
    }

    int scheduleIndex = doc["scheduleIndex"] | -1; // Valor por defecto si no existe
    if (scheduleIndex < 0 || scheduleIndex > 2) {
      request->send(400, "text/plain", "Indice de configuracion invalido");
      return;
    }

    scheduleNames[scheduleIndex] = doc["name"].as<String>() | ("Horario " + String(scheduleIndex + 1)); // Nombre por defecto

    JsonArray bells = doc["bells"];
    if (bells.isNull()) {
        request->send(400, "text/plain", "Falta el array 'bells' en los datos");
        return;
    }
    
    schedules[scheduleIndex].clear(); // Limpiar horarios anteriores
    scheduleCount[scheduleIndex] = 0; // Reiniciar contador

    for (JsonObject bellJson : bells) {
      if (scheduleCount[scheduleIndex] < 50) { // Limitar el número de timbres por horario
        BellTime newBell;
        newBell.hour = bellJson["hour"] | 0; // Valor por defecto 0
        newBell.minute = bellJson["minute"] | 0; // Valor por defecto 0
        newBell.isEndOfDay = bellJson["isEndOfDay"] | false; // Valor por defecto false
        schedules[scheduleIndex].push_back(newBell);
        scheduleCount[scheduleIndex]++;
      } else {
        Serial.println(F("Límite de timbres por horario alcanzado."));
        break; 
      }
    }

    saveSchedules();
    Serial.print(F("Horario '"));
    Serial.print(scheduleNames[scheduleIndex]);
    Serial.println(F("' actualizado."));
    request->send(200, "text/plain", "Configuracion actualizada");
  });

  // --- ENDPOINT MODIFICADO para configurar fecha y hora completas ---
  server.on("/api/setTime", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();

    if (!request->hasParam("year", true) || !request->hasParam("month", true) ||
        !request->hasParam("day", true) || !request->hasParam("hour", true) ||
        !request->hasParam("minute", true)) {
      request->send(400, "text/plain", "Faltan parametros: year, month, day, hour, minute");
      return;
    }

    int year = request->getParam("year", true)->value().toInt();
    int month = request->getParam("month", true)->value().toInt(); // 1-12
    int day = request->getParam("day", true)->value().toInt();     // 1-31
    int hour = request->getParam("hour", true)->value().toInt();   // 0-23
    int minute = request->getParam("minute", true)->value().toInt(); // 0-59
    int second = 0; // Puedes obtenerlo también si lo envías desde el cliente

    // Validaciones básicas (puedes hacerlas más exhaustivas)
    if (year < 2024 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59) {
      request->send(400, "text/plain", "Valores de fecha/hora invalidos");
      return;
    }
    
    // ESP32Time rtc.setTime(sec, min, hour, day, month, year)
    // El mes para ESP32Time es 1-12 en su función setTime.
    rtc.setTime(second, minute, hour, day, month, year); 
    
    timeSetSuccessfully = true; // Marcar que la hora fue configurada
    saveTimeRtcToSPIFFS();      // Guardar la hora configurada manualmente en SPIFFS

    String successMsg = "Fecha y Hora actualizada: " + rtc.getTimeDate(true);
    Serial.println(successMsg);
    request->send(200, "text/plain", successMsg);
  });

  // Iniciar servidor
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  server.begin();
}
