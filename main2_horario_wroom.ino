// main.ino
// Este código está configurado para la placa ESP32 DevKitC (ESP32-WROOM-32)
// (Pinout y configuración de hardware revisados para esta placa)

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h> // Aseguramos la inclusión de HardwareSerial
#include <DFRobotDFPlayerMini.h> 
#include <time.h>
#include <ESP32Time.h>
// #include <esp_task_wdt.h> // Comentado por si no lo estás usando activamente
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Wire.h"
#include "credentials.h" // Asegúrate de tener este archivo con tus credenciales

// Definición de pines para ESP32 DevKitC (ESP32-WROOM-32)
// RELAY_PIN: Control del módulo relé para el timbre
#define RELAY_PIN           4   // ¡NUEVO PIN para el relé! (Es el GPIO15, físico pin 23 en tu diagrama)
// EMERGENCY_BUTTON_PIN: Botón para activar el timbre manualmente en caso de emergencia
#define EMERGENCY_BUTTON_PIN 32   // Pin para el botón de emergencia
// DISABLE_BUTTON_PIN: Botón para activar/desactivar el timbre de forma manual
#define DISABLE_BUTTON_PIN   33   // Pin para el botón de desactivar

// Configuración DFPlayer
// Usamos HardwareSerial (UART2) en pines GPIO16 (RX) y GPIO17 (TX)
// Conecta DFPlayer TX (datos del DFPlayer) a ESP32 RX (GPIO16)
// Conecta DFPlayer RX (datos al DFPlayer) a ESP32 TX (GPIO17)
HardwareSerial DFPlayerSerial(2); // Instancia de HardwareSerial para UART2
DFRobotDFPlayerMini myDFPlayer;

// Sync time co
const char* ntpServer = "co.pool.ntp.org";  //Servidor NTP para sincronizar el Time

const long  gmtOffset_sec = -18000;    // Establecer el GMT (3600 segundos = 1 hora) paa Colombia es GMT -5) es decir -18000
const int   daylightOffset_sec = 0;

// Reloj RTC
ESP32Time rtc;

// Servidor web
AsyncWebServer server(80);

// Credenciales WiFi - Debes cambiarlas en credentials.h
const char *ssid = SSID;
const char *password = PASSWORD;

// Credenciales de acceso a la web
const char *http_username = HTTP_USERNAME;
const char *http_password = HTTP_PASSWORD;
const int totalSongs = 28; // Asegúrate de que coincida con el número real de archivos MP3
bool songPlayed[totalSongs + 1]; // Índice 0 no se usa, +1 para indexar desde 1
int lastPlayedSong = 0;

// Variables para control del sistema
bool timbreEnabled = true; // true: El timbre está activado por el usuario (puede pausarse automáticamente)
                           // false: El timbre está desactivado por el usuario (manual override)
int activeSchedule = 0;    // 0, 1, o 2 según la configuración activa

// Nuevo: Estados operacionales del timbre
enum BellOperationalState {
  BELL_ACTIVE,
  BELL_PAUSED_WEEKEND,
  BELL_PAUSED_HOLIDAY,
  BELL_DEACTIVATED_MANUAL
};
BellOperationalState bellCurrentState; // Almacena el estado actual del timbre

// Estructura para los horarios
struct BellTime {
  int hour;
  int minute;
  bool isEndOfDay;
  String name; // <--- NUEVO: Para guardar el nombre o descripción del timbre (ej. "Primer descanso")
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
  20250101,   // Año Nuevo
  20250106,   // Reyes Magos
  20250324,   // San José
  20250417,   // Jueves Santo
  20250418,   // Viernes Santo
  20250501,   // Día del Trabajo
  20250512,   // Ascensión del Señor
  20250602,   // Corpus Christi
  20250613,   // Sagrado Corazón
  20250630,   // San Pedro y San Pablo
  20250720,   // Independencia de Colombia
  20250807,   // Batalla de Boyacá
  20250818,   // Asunción de la Virgen
  20251013,   // Día de la Raza
  20251103,   // Todos los Santos
  20251117,   // Independencia de Cartagena
  20251208,   // Inmaculada Concepción
  20251225    // Navidad
};


void setup() {
  // esp_task_wdt_config_t config = {
  //   .timeout_ms = 120* 1000,   //   120 seconds
  //   .trigger_panic = true,   // Trigger panic if watchdog timer is not reset
  // };
  // esp_task_wdt_reconfigure(&config);
  Serial.begin(115200);
  // Inicializar el array de canciones reproducidas
  for (int i = 1; i <= totalSongs; i++) {
    songPlayed[i] = false;
  }

  // Inicializar pines
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  // Los botones usan INPUT_PULLUP: el pin está HIGH cuando no se presiona y LOW cuando se presiona
  pinMode(EMERGENCY_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DISABLE_BUTTON_PIN, INPUT_PULLUP);

  // Inicializar DFPlayer con la instancia de HardwareSerial (UART2)
  // DFPlayerSerial.begin(baudrate, SERIAL_CONFIG, RX_GPIO, TX_GPIO)
  // UART2 RX pin (datasheet, GPIO16)
  // UART2 TX pin (datasheet, GPIO17)
  DFPlayerSerial.begin(9600, SERIAL_8N1, 16, 17); // Baudrate, configuración, RX del ESP32, TX del ESP32
  if (!myDFPlayer.begin(DFPlayerSerial)) { // Pasamos la instancia HardwareSerial a myDFPlayer
    Serial.println(F("Error al inicializar DFPlayer"));
  }
  myDFPlayer.setTimeOut(500);
  myDFPlayer.volume(30); // Volumen (0-30)

  // Inicializar SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Error al montar SPIFFS");
  }

  // Cargar configuraciones guardadas
  loadSchedules();

  // Pantalla OLED (SSD1306)
  // Wire.begin() sin parámetros utiliza los pines I2C por defecto del ESP32:
  // SDA: GPIO21
  // SCL: GPIO22
  Wire.begin(); 
  // Inicializar la pantalla OLED
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
  setupWiFi();

  // Sincronizar hora con NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  setLocalTime();

  // Configurar servidor web
  setupWebServer();

  Serial.println("Sistema listo!");
  Serial.println("Hora: " + getCurrentTimeString());
  Serial.println("Config activa: " + scheduleNames[activeSchedule]);
}

void loop() {
  // Primero, actualizar el estado operacional del timbre
  updateBellOperationalState();

  // Verificar botones
  checkButtons();

  // Verificar si es hora de activar el timbre (solo si está ACTIVO, no PAUSADO)
  checkSchedule();

  // Manejar el timbre de forma no bloqueante
  handleBellState();

  // Actualizar pantalla
  updateDisplay();

  if (myDFPlayer.available()) {
    printDetail(myDFPlayer.readType(), myDFPlayer.read()); //Print the detail message from DFPlayer to handle different errors and states.
  }
}

// Nueva función para determinar el estado operacional del timbre
void updateBellOperationalState() {
  if (!timbreEnabled) { // El usuario lo ha desactivado manualmente
    bellCurrentState = BELL_DEACTIVATED_MANUAL;
  } else if (holiday()) { // Es un día festivo
    bellCurrentState = BELL_PAUSED_HOLIDAY;
  } else if (weekend(rtc.getDayofWeek())) { // Es fin de semana
    bellCurrentState = BELL_PAUSED_WEEKEND;
  } else { // Está activado por el usuario y es día hábil
    bellCurrentState = BELL_ACTIVE;
  }
}


void updateDisplay() {
  static unsigned long lastDisplayUpdate = 0;
  display.clearDisplay();
  display.setTextSize(1);

  if (millis() - lastDisplayUpdate > 1000) {

    if (isRingInProgress){
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.println("Timbre");
      display.setCursor(0, 16);
      display.println("Activado");
      display.display();

    }
    else  {
      // Mostrar IP
      display.setCursor(0, 0);
      if (WiFi.status() == WL_CONNECTED) {
        display.println(WiFi.localIP().toString());
      } else {
        display.println("AP: TimbreEscolar");
      }
      
      // Mostrar horario activo
      display.setCursor(0, 11);
      display.print("Config: ");
      display.println(scheduleNames[activeSchedule]);

      // Mostrar estado del timbre
      display.setCursor(0, 22);
      display.print("Estado: ");
      switch (bellCurrentState) {
        case BELL_ACTIVE:
          display.println("Activo");
          break;
        case BELL_PAUSED_WEEKEND:
          display.println("Pausa FDS");
          break;
        case BELL_PAUSED_HOLIDAY:
          display.println("Pausa Festivo");
          break;
        case BELL_DEACTIVATED_MANUAL:
          display.println("Desactivado");
          break;
      }
      
      display.display();

    }
    
    lastDisplayUpdate = millis();
  }
}

void setupWiFi() {

  Serial.println("Conectando WiFi...");

  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi Conectado!");
    Serial.println(WiFi.localIP().toString());

  } else {

  Serial.println("Error WiFi!");
  Serial.println("Modo AP activado");

  // Configurar Access Point si no se puede conectar
  // En las ESP32-WROOM-32, el SoftAP es muy estable
  WiFi.softAP("TimbreEscolar", "password"); 
  }
}

void setLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Error obteniendo hora");
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
    if (holidays[i] == fechaNum) {
      // Serial.println("¡Es festivo! (" + String(fechaNum) + ") Timbre Off"); // No es necesario imprimir aquí, lo hace updateBellOperationalState
      return true;
    }
  }
  return false;
}

void checkSchedule() {
  // Solo se activa el timbre si el estado operacional es ACTIVO
  if (bellCurrentState != BELL_ACTIVE) {
    return;
  }

  int currentHour = rtc.getHour(true);
  int currentMinute = rtc.getMinute();
  int currentSecond = rtc.getSecond();

  // Solo revisar cuando los segundos sean 0 para evitar activaciones múltiples
  if (currentSecond != 0) return;

  for (int i = 0; i < scheduleCount[activeSchedule]; i++) {
    if (schedules[activeSchedule][i].hour == currentHour && schedules[activeSchedule][i].minute == currentMinute) {
      startBell(schedules[activeSchedule][i].isEndOfDay);
      break;
    }
  }
}

void printDetail(uint8_t type, int value){
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
if (isRingInProgress) return; // Evita que se active un nuevo timbre si ya hay uno en progreso

  int randomSong;
  int attempts = 0;
  bool foundUnplayed = false;

  // Intentar hasta 10 veces encontrar una canción no reproducida
  while (attempts < 10 && !foundUnplayed) {
    randomSong = random(1, totalSongs + 1);
    if (!songPlayed[randomSong]) {
      foundUnplayed = true;
      songPlayed[randomSong] = true; // Marcar la canción como reproducida
      lastPlayedSong = randomSong;
    }
    attempts++;
  }

  // Si no se encontró una canción no reproducida (después de varios intentos),
  // se reproduce una aleatoria sin verificar (para evitar bucles infinitos)
  if (!foundUnplayed) {
    randomSong = random(1, totalSongs + 1);
    Serial.println("¡Timbre activado! (Reproducción forzada - posible repetición)");
  } else {
    Serial.println("¡Timbre activado! (Canción " + String(randomSong) + ")");
  }

  // Activar relé para el timbre (salida activa por HIGH)
  digitalWrite(RELAY_PIN, HIGH);

  // Reproducir mp3
  myDFPlayer.play(randomSong);
  myDFPlayer.volume(30);

  // Establecer variables para la gestión no bloqueante
  isRingInProgress = true;
  isEndOfDayBell = endOfDay;
  bellStartTime = millis();
  bellState = 1; // Inicia el estado de la máquina de estados del timbre
}

void handleBellState() {
if (!isRingInProgress) return; // No hacer nada si no hay un timbre en progreso

  unsigned long currentMillis = millis();
  unsigned long elapsedTime = currentMillis - bellStartTime;

  switch (bellState) {
    case 1: // Primera campana: Relé activado y MP3 sonando
      if (elapsedTime >= bell_time_duration) {
        digitalWrite(RELAY_PIN, LOW); // Apaga el relé
        if (isEndOfDayBell) {
          // Si es fin de jornada, prepara para la segunda campana
          bellStartTime = currentMillis; // Reinicia el temporizador para la pausa
          bellState = 2; // Pasa al estado de pausa
        } else {
          // Si no es fin de jornada, se prepara para detener solo el MP3
          bellStartTime = currentMillis; // Reinicia el temporizador para la duración del MP3
          bellState = 4; // Pasa al estado de parada del MP3
        }
      }
      break;

    case 2: // Pausa entre campanas (solo para fin de jornada)
      // Se mantiene el relé apagado
      if (elapsedTime >= 500) { // Pequeña pausa de 500ms
        digitalWrite(RELAY_PIN, HIGH); // Activa el relé de nuevo para la segunda campana
        bellStartTime = currentMillis; // Reinicia el temporizador para la segunda campana
        bellState = 3; // Pasa al estado de segunda campana
      }
      break;

    case 3: // Segunda campana (solo para fin de jornada)
      if (elapsedTime >= bell_time_duration) {
        digitalWrite(RELAY_PIN, LOW); // Apaga el relé definitivamente
        bellStartTime = currentMillis; // Reinicia el temporizador para la detención del MP3
        bellState = 4; // Pasa al estado de parada del MP3
      }
      break;

    case 4: // Detener MP3 y reiniciar el proceso del timbre
      if (elapsedTime >= mp3_time_duration - (isEndOfDayBell ? (bell_time_duration + 500 + bell_time_duration) : bell_time_duration)) { 
        // El MP3 se detiene después de su duración total, ajustando por el tiempo de las campanadas.
        // Esto es una simplificación: lo más robusto sería escuchar el evento DFPlayerPlayFinished,
        // pero para un temporizador es una aproximación válida.
        myDFPlayer.stop(); // Detiene la reproducción del MP3
        isRingInProgress = false; // Marca que el timbre ha terminado
        bellState = 0; // Vuelve al estado inactivo
        Serial.println("MP3 detenido, campana inactiva."); 
        
        // Reset songPlayed array if all songs have been played
        bool allPlayed = true;
        for (int i = 1; i <= totalSongs; i++) {
          if (!songPlayed[i]) {
            allPlayed = false;
            break;
          }
        }
        if (allPlayed) {
          for (int i = 1; i <= totalSongs; i++) {
            songPlayed[i] = false;
          }
          Serial.println("Todas las canciones reproducidas, reseteando lista.");
        }
      }
      break;
  }
}


void checkButtons() {
    // Botón de emergencia
    // INPUT_PULLUP: LOW cuando se pulsa
    if (digitalRead(EMERGENCY_BUTTON_PIN) == LOW) {
        delay(50);  // Debounce: espera un momento para confirmar el pulso
        if (digitalRead(EMERGENCY_BUTTON_PIN) == LOW) { // Confirma la lectura
            if (!isRingInProgress) { // Solo si no hay un timbre en progreso
                startBell(false); // Activa el timbre (no como fin de jornada)
            }
            // Mantener la operación de timbre mientras el botón está pulsado
            while (digitalRead(EMERGENCY_BUTTON_PIN) == LOW) {
                // Sigue manejando el estado del timbre (para que complete su ciclo)
                handleBellState(); 
                delay(10); // Pequeño delay para no saturar el CPU
            }
        }
    }

    // Botón de activar/desactivar (control manual del estado general del timbre)
    // INPUT_PULLUP: LOW cuando se pulsa
    if (digitalRead(DISABLE_BUTTON_PIN) == LOW) {
        delay(50);  // Debounce: espera y confirma el pulso
        if (digitalRead(DISABLE_BUTTON_PIN) == LOW) { // Confirma la lectura
            timbreEnabled = !timbreEnabled; // Invierte el estado de activación manual
            Serial.print("Timbre ");
            Serial.println(timbreEnabled ? "ACTIVADO por BOTON" : "DESACTIVADO por BOTON");
            // Esperar a que el botón sea liberado para evitar múltiples toggles rápidos
            while (digitalRead(DISABLE_BUTTON_PIN) == LOW) {
                handleBellState(); // Permite que un timbre en progreso (si lo hay) termine
                delay(10);
            }
        }
    }
}

void loadSchedules() {
  if (SPIFFS.exists("/config.json")) {
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile) {
      size_t size = configFile.size();
      std::unique_ptr<char[]> buf(new char[size]);
      configFile.readBytes(buf.get(), size);

      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, buf.get());

      if (error) { // Manejo de error de deserialización
        Serial.println(F("Fallo al leer config.json, usando valores por defecto."));
        // Continúa para cargar valores por defecto si el error es fatal o config corrupta
        configFile.close(); // Cerrar el archivo antes de continuar
        // Llama recursivamente para establecer los valores por defecto
        // Esto es un poco hacky, pero asegura que si el archivo es corrupto, se establezcan los valores predeterminados.
        // Una mejor opción sería manejar el error de forma más explícita y luego llamar a la parte de 초기화 below.
        // Pero por simplicidad, y asumiendo que el archivo se borrará manualmente si está corrupto, lo hacemos así.
        goto load_default_config; 
      } else {
        activeSchedule = doc["activeSchedule"] | 0; // Usar | 0 para un valor por defecto si no existe
        timbreEnabled = doc["timbreEnabled"] | true; // Cargar estado de activacion manual

        // Deserializar nombres de horarios
        JsonArray namesArray = doc["scheduleNames"];
        if (namesArray && namesArray.size() == 3) {
          for (int s = 0; s < 3; s++) {
            scheduleNames[s] = namesArray[s].as<String>();
          }
        } else {
          Serial.println("Warning: scheduleNames array is missing or incorrect size.");
          // Si faltan, asignar nombres por defecto
          scheduleNames[0] = "Horario 1";
          scheduleNames[1] = "Horario 2";
          scheduleNames[2] = "Horario 3";
        }

        // Deserializar los horarios de los timbres
        JsonArray allSchedulesJson = doc["schedules"];
        if (allSchedulesJson) {
          for (int s = 0; s < 3; s++) {
            JsonArray currentScheduleJsonArray = allSchedulesJson[s];
            if (currentScheduleJsonArray) {
              schedules[s].clear(); // Limpiar el vector C++ antes de llenarlo
              for (int i = 0; i < currentScheduleJsonArray.size(); i++) {
                BellTime bell;
                bell.hour = currentScheduleJsonArray[i]["hour"] | 0;
                bell.minute = currentScheduleJsonArray[i]["minute"] | 0;
                bell.isEndOfDay = currentScheduleJsonArray[i]["isEndOfDay"] | false;
                bell.name = currentScheduleJsonArray[i]["name"] | ""; // <--- LEER EL NOMBRE
                schedules[s].push_back(bell);
              }
              scheduleCount[s] = schedules[s].size(); // Actualizar el contador
            } else {
              Serial.printf("Warning: Nested schedule array for index %d is null.\n", s);
              scheduleCount[s] = 0; // Asegurar que sea 0 si el arreglo anidado falta
              schedules[s].clear();
            }
          }
        } else {
          Serial.println("Warning: \"schedules\" root array is missing.");
          // Si falta el array principal de horarios, todos los contadores a 0
          for (int s = 0; s < 3; s++) {
              scheduleCount[s] = 0;
              schedules[s].clear();
          }
        }
      }
      configFile.close();
    }
  } else {
    load_default_config: // Etiqueta para saltar aquí en caso de error de deserialización
    // Configuración por defecto si config.json no existe o está corrupto
    Serial.println("config.json no encontrado o corrupto, creando configuración por defecto.");

    // Resetea todos los horarios y nombres
    for (int s = 0; s < 3; s++) {
      schedules[s].clear();
      scheduleCount[s] = 0;
    }

    // Horario 1 (Basado en la imagen)
    scheduleNames[0] = "Horario 1 (Defecto)";
    schedules[0].push_back({6, 10, false, ""});
    schedules[0].push_back({7, 5, false, ""});
    schedules[0].push_back({8, 0, false, ""});
    schedules[0].push_back({8, 55, false, "Primer descanso"});
    schedules[0].push_back({9, 25, false, ""});
    schedules[0].push_back({10, 20, false, ""});
    schedules[0].push_back({11, 15, false, "Segundo descanso"});
    schedules[0].push_back({11, 40, false, ""});
    schedules[0].push_back({12, 35, false, ""});
    schedules[0].push_back({13, 30, true, "Fin de jornada"}); // Marcar como fin de jornada
    scheduleCount[0] = schedules[0].size();

    // Horario 2 (Basado en la imagen)
    scheduleNames[1] = "Horario 2 (Defecto)";
    schedules[1].push_back({6, 10, false, ""});
    schedules[1].push_back({7, 0, false, ""});
    schedules[1].push_back({7, 50, false, ""});
    schedules[1].push_back({8, 35, false, ""});
    schedules[1].push_back({9, 20, false, "Primer descanso"});
    schedules[1].push_back({9, 55, false, ""});
    schedules[1].push_back({10, 40, false, ""});
    schedules[1].push_back({11, 25, false, ""});
    schedules[1].push_back({12, 10, true, "Fin de jornada"}); // Marcar como fin de jornada
    scheduleCount[1] = schedules[1].size();

    // Horario 3 (Por defecto vacío)
    scheduleNames[2] = "Horario 3 (Vacío)";
    scheduleCount[2] = 0;

    activeSchedule = 0; // Establece el Horario 1 como activo por defecto
    timbreEnabled = true; // Por defecto activado

    saveSchedules(); // Guarda la configuración por defecto
  }
}

void saveSchedules() {
  DynamicJsonDocument doc(2048); // El tamaño puede necesitar ajustarse si hay muchos timbres

  doc["activeSchedule"] = activeSchedule;
  doc["timbreEnabled"] = timbreEnabled; // Guardar el estado de activación manual

  // Crear el arreglo de nombres de horarios
  JsonArray namesRoot = doc.createNestedArray("scheduleNames");
  for (int s = 0; s < 3; s++) {
    namesRoot.add(scheduleNames[s]);
  }

  // Crear el arreglo principal de horarios (arreglo de arreglos)
  JsonArray schedulesRoot = doc.createNestedArray("schedules");
  for (int s = 0; s < 3; s++) {
    JsonArray currentScheduleArray = schedulesRoot.createNestedArray(); // Crea el arreglo anidado para este horario
    for (int i = 0; i < scheduleCount[s]; i++) {
      JsonObject bell = currentScheduleArray.add<JsonObject>();
      bell["hour"] = schedules[s][i].hour;
      bell["minute"] = schedules[s][i].minute;
      bell["isEndOfDay"] = schedules[s][i].isEndOfDay;
      bell["name"] = schedules[s][i].name; // <--- GUARDAR EL NOMBRE
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

String processor(const String &var) {
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

    DynamicJsonDocument doc(2048); // El tamaño puede necesitar ajustarse si hay muchos timbres
    doc["activeSchedule"] = activeSchedule;
    doc["timbreEnabled"] = timbreEnabled; // Estado manual (Activado/Desactivado por usuario)
    doc["bellOperationalState"] = (int)bellCurrentState; // Nuevo: Estado de operación actual (Enum to int)
    doc["currentTime"] = getCurrentTimeString();
      
    // Añadir fecha actual para el frontend
    struct tm timeinfo;
    char dateStr[80]; // Buffer for date string
    if (getLocalTime(&timeinfo)) {
      strftime(dateStr, sizeof(dateStr), "%A, %d de %B de %Y", &timeinfo);
      // Capitalize first letter of day and month if needed for Spanish
      dateStr[0] = toupper(dateStr[0]);
      // Find and capitalize month - this is tricky with accents and different month names
      // For simplicity, we just capitalize the first letter of the string.
      // For more robust localization, consider using a library or an array of capitalized month names.
    } else {
      strcpy(dateStr, "Fecha no disponible");
    }
    doc["currentDate"] = dateStr;

    // Llenar los nombres de los horarios
    JsonArray namesJson = doc.createNestedArray("scheduleNames");
    for(int s=0; s<3; s++){
      namesJson.add(scheduleNames[s]);
    }

    // Llenar los horarios de los timbres (arreglo de arreglos)
    JsonArray allSchedulesJson = doc.createNestedArray("schedules");
    for (int s = 0; s < 3; s++) {
      JsonArray currentScheduleJsonArray = allSchedulesJson.createNestedArray(); // Crea el arreglo anidado para este horario
      for (int i = 0; i < scheduleCount[s]; i++) {
        JsonObject bell = currentScheduleJsonArray.add<JsonObject>();
        bell["hour"] = schedules[s][i].hour;
        bell["minute"] = schedules[s][i].minute;
        bell["isEndOfDay"] = schedules[s][i].isEndOfDay;
        bell["name"] = schedules[s][i].name; // <--- ENVIAR EL NOMBRE AL FRONTEND
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
        saveSchedules();
        request->send(200, "text/plain", "Configuración cambiada");
      } else {
        request->send(400, "text/plain", "Valor de configuración inválido");
      }
    } else {
      request->send(400, "text/plain", "Falta parámetro schedule");
    }
  });

  // API para activar/desactivar timbre (solo el switch manual)
  server.on("/api/toggleTimbre", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();

    timbreEnabled = !timbreEnabled; // Solo cambia el indicador manual
    saveSchedules(); // Guardar el nuevo estado manual en SPIFFS
    request->send(200, "text/plain", timbreEnabled ? "Timbre activado (manualmente)" : "Timbre desactivado (manualmente)");
  });

  // API para activar timbre manualmente
  server.on("/api/ringNow", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();

    if (!isRingInProgress) {
      startBell(false);
    }
    request->send(200, "text/plain", "Timbre activado manualmente");
  });

  // API para actualizar configuraciones
  server.on("/api/updateSchedule", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();

    // Verificar parámetros requeridos
    if (!request->hasParam("data", true)) {
      request->send(400, "text/plain", "Faltan datos de configuración");
      return;
    }

    String jsonData = request->getParam("data", true)->value();
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, jsonData);

    if (error) {
      request->send(400, "text/plain", "Error en formato JSON: " + String(error.c_str()));
      return;
    }

    int scheduleIndex = doc["scheduleIndex"];
    if (scheduleIndex < 0 || scheduleIndex > 2) {
      request->send(400, "text/plain", "Índice de configuración inválido");
      return;
    }

    // Actualizar nombre de la configuración
    scheduleNames[scheduleIndex] = doc["name"].as<String>();

    // Actualizar horarios
    JsonArray bells = doc["bells"].as<JsonArray>(); // Obtén el array de "bells"
    schedules[scheduleIndex].clear(); // Limpiar el vector C++ para este horario
    scheduleCount[scheduleIndex] = bells.size(); // Actualizar el contador

    for (JsonObject bell : bells) {
      BellTime newBell;
      newBell.hour = bell["hour"].as<int>();
      newBell.minute = bell["minute"].as<int>();
      newBell.isEndOfDay = bell["isEndOfDay"].as<bool>();
      newBell.name = bell["name"].as<String>(); // <--- LEER Y GUARDAR EL NOMBRE
      schedules[scheduleIndex].push_back(newBell);
    }

    saveSchedules();
    request->send(200, "text/plain", "Configuración actualizada");
  });

  // API para configurar fecha y hora
  server.on("/api/setTime", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(http_username, http_password))
      return request->requestAuthentication();

    if (!request->hasParam("hour", true) || !request->hasParam("minute", true) || !request->hasParam("dayOfMonth", true) || !request->hasParam("month", true) || !request->hasParam("year", true)) {
      request->send(400, "text/plain", "Faltan parámetros de fecha y hora.");
      return;
    }

    int hour = request->getParam("hour", true)->value().toInt();
    int minute = request->getParam("minute", true)->value().toInt();
    int dayOfMonth = request->getParam("dayOfMonth", true)->value().toInt();
    int month = request->getParam("month", true)->value().toInt(); // 1-12
    int year = request->getParam("year", true)->value().toInt();

    // Ajustar el RTC interno. setTime usa 0-59 sec, 0-59 min, 0-23 hr, 1-31 day, 1-12 month, year (e.g., 2025)
    rtc.setTime(0, minute, hour, dayOfMonth, month, year);
    
    // Opcionalmente, puedes forzar una resincronización de NTP aquí si quieres
    // configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    // setLocalTime();

    request->send(200, "text/plain", "Hora y fecha actualizadas");
  });


  // Iniciar servidor
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  server.begin();
}