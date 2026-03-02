/*
 * ============================================================================
 *  Monitor de Calidad del Aire — Universidad de La Sabana
 *  Autor(es): Brainer Jimenez y Bruno Perez
 *  Curso: IoT
 *  Fecha: 02-03-2026
 *
 *  Descripción:
 *  Sistema embebido en Arduino UNO que monitorea calidad del aire integrando:
 *   - PMS5003 (PM2.5 / PM10) por UART (SoftwareSerial)
 *   - MQ135 (índice relativo RAW) por ADC
 *   - BME280 (temperatura/humedad/presión) por I2C
 *  y realiza actuación local con:
 *   - LCD I2C 16x2
 *   - LED RGB (PWM)
 *   - Buzzer activo (tone/noTone)
 *
 *  Nota de diseño:
 *  El loop está organizado como un "scheduler" no bloqueante con millis().
 *  El objetivo es evitar bloqueos del bus I2C y mantener respuesta fluida.
 * ============================================================================
 *
 *  MAPA DE PINES (Arduino UNO)
 *  ┌─────────────┬────────────┬──────────────────────────────────────────────┐
 *  │ Componente  │ Pin Arduino│ Notas                                        │
 *  ├─────────────┼────────────┼──────────────────────────────────────────────┤
 *  │ BME280 SDA  │ A4         │ Bus I2C compartido (50 kHz)                  │
 *  │ BME280 SCL  │ A5         │                                              │
 *  │ LCD SDA     │ A4         │ Bus I2C compartido                           │
 *  │ LCD SCL     │ A5         │                                              │
 *  │ PMS5003 TX  │ D8 (RX SW) │ SoftwareSerial a 9600 baud                   │
 *  │ PMS5003 RX  │ D7 (TX SW) │ Opcional (no imprescindible si solo se lee)  │
 *  │ PMS5003 SET │ 5V         │ Siempre activo                               │
 *  │ PMS5003 RST │ 5V         │ Siempre activo                               │
 *  │ MQ135 AOUT  │ A0         │ Entrada analógica (0–1023)                   │
 *  │ LED Rojo    │ D6  (PWM)  │ LED RGB cátodo común + resistencia en serie  │
 *  │ LED Verde   │ D5  (PWM)  │ LED RGB cátodo común + resistencia en serie  │
 *  │ LED Azul    │ D3  (PWM)  │ LED RGB cátodo común + resistencia en serie  │
 *  │ Buzzer (+)  │ D4         │ Buzzer activo                                │
 *  └─────────────┴────────────┴──────────────────────────────────────────────┘
 *
 *  LIBRERÍAS NECESARIAS (Arduino IDE > Library Manager)
 *   - Adafruit BME280 Library (+ Adafruit Unified Sensor)
 *   - LiquidCrystal I2C (Frank de Brabander)
 *
 *  I2C (NOTAS)
 *   - BME280: 0x76 (o 0x77 si SDO=HIGH)
 *   - LCD   : 0x27 (o 0x3F según módulo PCF8574)
 *   - I2C clock: 50 kHz (mayor estabilidad con cables largos/protoboard)
 *   - Pull-ups recomendadas (4.7k–10k) en SDA/SCL si cable > 20 cm
 *
 *  MQ135 (LECTURA)
 *   - Se usa RAW ADC como índice relativo.
 *   - En la mayoría de módulos: más gas -> lectura RAW tiende a bajar.
 *   - Se promedia 8 muestras para reducir ruido.
 *
 *  FUSIÓN (RESUMEN)
 *   - Se calcula un "score" ponderado y se clasifica en: OK / ADV / PEL.
 *   - Pesos: PM2.5 40%, GAS 35%, TEMP 15%, HUM 10%.
 *   - Umbrales score: <0.25 OK | 0.25–0.60 ADV | >0.60 PEL.
 *
 *  IMPORTANTE:
 *  Este código prioriza estabilidad del sistema (I2C) y lectura no bloqueante
 *  del PMS5003. El LCD solo se limpia al cambiar de pantalla.
 * ============================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_BME280.h>
#include <Adafruit_Sensor.h>
#include <SoftwareSerial.h>

// =============================================================================
//  CONFIGURACIÓN DE HARDWARE
// =============================================================================

// Pines Arduino UNO
#define MQ135_PIN    A0
#define LED_R_PIN    6
#define LED_G_PIN    5
#define LED_B_PIN    3
#define BUZZER_PIN   4
#define PMS_RX_PIN   8   // Arduino recibe en D8 (SoftwareSerial RX)
#define PMS_TX_PIN   7   // Arduino transmite en D7 (SoftwareSerial TX)

// Direcciones I2C
#define LCD_I2C_ADDR  0x27   
#define BME_I2C_ADDR  0x76   

// Objetos de periféricos
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, 16, 2);
Adafruit_BME280   bme;
SoftwareSerial    pmsSerial(PMS_RX_PIN, PMS_TX_PIN);

// =============================================================================
//  MODELO DE DATOS Y ESTADOS
// =============================================================================

/**
 * @brief Estados discretos de calidad del aire.
 * OK  : condiciones dentro de rangos esperados
 * ADV : advertencia (condiciones elevadas)
 * PEL : peligro (condiciones críticas)
 */
enum AirState { OK = 0, ADV = 1, PEL = 2 };
const char* STATE_LABEL[] = { "OK ", "ADV", "PEL" };

/**
 * @brief Estructura mínima de datos PMS5003 (concentración atmosférica).
 */
struct PMS5003Data {
  uint16_t pm1_0_atm;
  uint16_t pm2_5_atm;
  uint16_t pm10_atm;
};

// =============================================================================
//  VARIABLES GLOBALES (MEDICIÓN)
// =============================================================================

float    g_temperature = 0.0f;   // °C
float    g_humidity    = 0.0f;   // %
float    g_pressure    = 0.0f;   // hPa
int      g_rawMQ       = 1023;   // ADC 0–1023 (logica invertida, entre mas alto mas contaminacion)
uint16_t g_pm2_5       = 0;      // µg/m³
AirState g_airState    = OK;

// =============================================================================
//  SCHEDULER NO BLOQUEANTE (millis)
// =============================================================================

unsigned long lastSensorRead   = 0;  // BME280 + MQ135
unsigned long lastScreenSwitch = 0;  // cambio de pantalla
unsigned long lastBuzzerToggle = 0;  // toggle buzzer (ADV)
unsigned long lastLCDUpdate    = 0;  // refresco parcial LCD

const unsigned long SENSOR_INTERVAL = 2000; // ms
const unsigned long SCREEN_INTERVAL = 5000; // ms
const unsigned long BUZZER_INTERVAL = 500;  // ms
const unsigned long LCD_UPDATE_MS   = 500;  // ms

int  g_currentScreen = 0;  // 0: pantalla 1 | 1: pantalla 2
bool g_buzzerState   = false;

// =============================================================================
//  PMS5003 (BUFFER Y PARSEO)
// =============================================================================

#define PMS_FRAME_LEN 32
uint8_t pmsBuffer[PMS_FRAME_LEN];
uint8_t pmsIdx = 0;

// =============================================================================
//  FUNCIONES: PMS5003
// =============================================================================

/**
 * @brief Parsea el stream del PMS5003 de forma no bloqueante (byte a byte).
 *
 * Proceso:
 *  1) Buscar cabecera 0x42 0x4D.
 *  2) Acumular 32 bytes.
 *  3) Verificar checksum (suma de bytes 0..29 contra bytes 30..31).
 *  4) Extraer PM1.0/PM2.5/PM10 (atm) y devolver true.
 *
 * @param[out] data Estructura que se llena si el frame es válido.
 * @return true si se recibió un frame completo y válido.
 */
bool parsePMS5003(PMS5003Data &data) {
  while (pmsSerial.available()) {
    uint8_t c = pmsSerial.read();

    // Sincronización de cabecera
    if (pmsIdx == 0) {
      if (c != 0x42) continue;
    } else if (pmsIdx == 1) {
      if (c != 0x4D) { pmsIdx = 0; continue; }
    }

    pmsBuffer[pmsIdx++] = c;

    // Frame completo
    if (pmsIdx == PMS_FRAME_LEN) {
      pmsIdx = 0;

      // Checksum
      uint16_t sum = 0;
      for (uint8_t i = 0; i < 30; i++) sum += pmsBuffer[i];
      uint16_t rxCRC = ((uint16_t)pmsBuffer[30] << 8) | pmsBuffer[31];
      if (sum != rxCRC) return false;

      // Extracción (concentración atmosférica)
      data.pm1_0_atm = ((uint16_t)pmsBuffer[10] << 8) | pmsBuffer[11];
      data.pm2_5_atm = ((uint16_t)pmsBuffer[12] << 8) | pmsBuffer[13];
      data.pm10_atm  = ((uint16_t)pmsBuffer[14] << 8) | pmsBuffer[15];
      return true;
    }
  }
  return false;
}

// =============================================================================
//  FUNCIONES: MQ135
// =============================================================================

/**
 * @brief Lee MQ135 como índice RAW (ADC) promediando N muestras.
 *
 * Nota:
 *  - Se añade delay(2) entre muestras para reducir ruido; el total es ~16 ms
 *    (8*2ms) y ocurre solo cada SENSOR_INTERVAL (2s), por lo que no afecta
 *    la reactividad del sistema.
 *
 * @return Promedio entero 0..1023.
 */
int readMQ135_RAW() {
  long suma = 0;
  for (int i = 0; i < 8; i++) {
    suma += analogRead(MQ135_PIN);
    delay(2);
  }
  return (int)(suma / 8);
}

// =============================================================================
//  FUNCIONES: FUSIÓN Y CLASIFICACIÓN
// =============================================================================

/**
 * @brief Calcula el estado discreto del aire a partir de variables medidas.
 *
 * Se asigna un score parcial por variable:
 *  0.0 = bueno | 0.5 = advertencia | 1.0 = peligro
 *
 * Pesos:
 *  - PM2.5: 40%
 *  - GAS : 35%
 *  - TEMP: 15%
 *  - HUM : 10%
 *
 * Umbrales score:
 *  - score < 0.25 => OK
 *  - score < 0.60 => ADV
 *  - else         => PEL
 *
 * @param temp Temperatura (°C)
 * @param hum  Humedad (%)
 * @param rawMQ Lectura RAW MQ135 (0..1023)
 * @param pm25 PM2.5 (µg/m³)
 * @return AirState (OK/ADV/PEL)
 */
AirState computeAirState(float temp, float hum, int rawMQ, uint16_t pm25) {

  // ---------------- PM2.5 ----------------
  float sPM = (pm25 < 25) ? 0.0f :
              (pm25 < 70) ? 0.5f : 1.0f;

  // ---------------- GAS (RAW MQ135) ----------------
  // Nota: Se ajustan estos umbrales según calibración experimental.
  float sGas = (rawMQ < 250) ? 0.0f :
               (rawMQ > 450) ? 0.5f : 1.0f;

  // ---------------- Temperatura (ASHRAE 55 aprox.) ----------------
  float sTemp;
  if      (temp >= 16.0f && temp <= 28.0f) sTemp = 0.0f;
  else if (temp >=  8.0f && temp <  16.0f) sTemp = 0.5f;
  else if (temp >  28.0f && temp <= 36.0f) sTemp = 0.5f;
  else                                      sTemp = 1.0f;

  // ---------------- Humedad relativa ----------------
  float sHum;
  if      (hum >= 30.0f && hum <= 70.0f)   sHum = 0.0f;
  else if (hum >= 20.0f && hum <  30.0f)   sHum = 0.5f;
  else if (hum >  70.0f && hum <= 80.0f)   sHum = 0.5f;
  else                                      sHum = 1.0f;

  // Score ponderado
  float score = sPM   * 0.40f
              + sGas  * 0.35f
              + sTemp * 0.15f
              + sHum  * 0.10f;

  // Clasificación discreta
  if      (score < 0.25f) return OK;
  else if (score < 0.60f) return ADV;
  else                    return PEL;
}

// =============================================================================
//  FUNCIONES: LED RGB
// =============================================================================

/**
 * @brief Control PWM del LED RGB (cátodo común).
 * @param r Duty cycle canal rojo  (0..255)
 * @param g Duty cycle canal verde (0..255)
 * @param b Duty cycle canal azul  (0..255)
 */
void setRGB(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(LED_R_PIN, r);
  analogWrite(LED_G_PIN, g);
  analogWrite(LED_B_PIN, b);
}

/**
 * @brief Actualiza el LED según el estado de aire.
 *  OK  => verde
 *  ADV => amarillo
 *  PEL => rojo
 */
void updateLED(AirState state) {
  switch (state) {
    case OK:  setRGB(0,   255, 0);  break;
    case ADV: setRGB(255, 180, 0);  break;
    case PEL: setRGB(255, 0,   0);  break;
  }
}

// =============================================================================
//  FUNCIONES: BUZZER
// =============================================================================

/**
 * @brief Actualiza el buzzer según el estado.
 *  OK  => silencio
 *  ADV => intermitente (toggle cada BUZZER_INTERVAL)
 *  PEL => tono continuo (1500 Hz)
 *
 * Nota:
 *  - Se usa millis() para no bloquear el loop.
 */
void updateBuzzer(AirState state) {
  unsigned long now = millis();

  if (state == OK) {
    noTone(BUZZER_PIN);
    g_buzzerState = false;
    return;
  }

  if (state == PEL) {
    tone(BUZZER_PIN, 1500);
    return;
  }

  // ADV: parpadeo no bloqueante
  if (now - lastBuzzerToggle >= BUZZER_INTERVAL) {
    lastBuzzerToggle = now;
    g_buzzerState = !g_buzzerState;
    if (g_buzzerState) tone(BUZZER_PIN, 900);
    else               noTone(BUZZER_PIN);
  }
}

// =============================================================================
//  FUNCIONES: LCD (UI)
// =============================================================================

/**
 * @brief Pantalla 1: Temperatura, Humedad, Presión + Estado
 */
void showScreen1() {
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(g_temperature, 1);
  lcd.print("C H:");
  if (g_humidity < 100.0f) lcd.print(' ');
  lcd.print((int)g_humidity);
  lcd.print('%');
  lcd.print(' ');

  lcd.setCursor(0, 1);
  lcd.print("P:");
  if (g_pressure < 1000.0f) lcd.print(' ');
  lcd.print((int)g_pressure);
  lcd.print("hPa");
  lcd.print("   ");
  lcd.print(STATE_LABEL[g_airState]);
}

/**
 * @brief Pantalla 2: PM2.5, GAS RAW + Estado
 */
void showScreen2() {
  lcd.setCursor(0, 0);
  lcd.print("PM2.5:");
  if (g_pm2_5 < 100) lcd.print(' ');
  if (g_pm2_5 <  10) lcd.print(' ');
  lcd.print(g_pm2_5);
  lcd.print("ug/m3");

  lcd.setCursor(0, 1);
  lcd.print("GAS:");
  if (g_rawMQ < 1000) lcd.print(' ');
  if (g_rawMQ <  100) lcd.print(' ');
  if (g_rawMQ <   10) lcd.print(' ');
  lcd.print(g_rawMQ);
  lcd.print("  ");
  lcd.print(STATE_LABEL[g_airState]);
}

/**
 * @brief Refresca el contenido de la pantalla activa.
 */
void updateLCD() {
  if (g_currentScreen == 0) showScreen1();
  else                      showScreen2();
}

// =============================================================================
//  SETUP
// =============================================================================

/**
 * @brief Inicializa periféricos, I2C, LCD, BME280 y seriales.
 *
 * Secuencia:
 *  1) Serial debug + SoftwareSerial PMS
 *  2) Pines de salida (LED/Buzzer)
 *  3) I2C a 50 kHz
 *  4) LCD (mensaje inicial)
 *  5) BME280 (con configuración de muestreo)
 */
void setup() {
  Serial.begin(9600);
  pmsSerial.begin(9600);

  pinMode(LED_R_PIN,  OUTPUT);
  pinMode(LED_G_PIN,  OUTPUT);
  pinMode(LED_B_PIN,  OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // I2C a 50 kHz: tolerante a cables largos y protoboard
  Wire.begin();
  Wire.setClock(50000UL);

  // Pantalla de bienvenida
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print(" AIR QUALITY MON");
  lcd.setCursor(0, 1);
  lcd.print("  Iniciando...  ");
  setRGB(0, 80, 0);
  delay(2500);

  // Inicializar BME280
  if (!bme.begin(BME_I2C_ADDR)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("BME280 no found!");
    lcd.setCursor(0, 1);
    lcd.print("Check addr/cable");
    setRGB(255, 0, 0);
    while (true) {}
  }

  // Oversampling y filtro IIR del BME280 (mejora estabilidad)
  bme.setSampling(
    Adafruit_BME280::MODE_NORMAL,
    Adafruit_BME280::SAMPLING_X2,
    Adafruit_BME280::SAMPLING_X16,
    Adafruit_BME280::SAMPLING_X1,
    Adafruit_BME280::FILTER_X16,
    Adafruit_BME280::STANDBY_MS_500
  );

  lcd.clear();
  delay(500);
}

// =============================================================================
//  LOOP PRINCIPAL (Scheduler)
// =============================================================================

/**
 * @brief Loop principal no bloqueante.
 *
 * Tareas:
 *  - (2s)    Leer BME280 + MQ135, validar rangos, fusionar y actualizar LED.
 *  - (cont.) Leer PMS5003 (byte a byte) y actualizar PM2.5 cuando el frame sea válido.
 *  - (cont.) Actualizar buzzer según estado (ADV intermitente, PEL continuo).
 *  - (5s)    Cambiar pantalla y limpiar LCD (solo en el cambio).
 *  - (500ms) Refrescar contenido de LCD (sin lcd.clear constante).
 */
void loop() {
  unsigned long now = millis();

  // --------------------------------------------------------------------------
  // TAREA 1 (cada 2 s): BME280 + MQ135 + FUSIÓN + LED
  // --------------------------------------------------------------------------
  if (now - lastSensorRead >= SENSOR_INTERVAL) {
    lastSensorRead = now;

    float t = bme.readTemperature();
    float h = bme.readHumidity();
    float p = bme.readPressure() / 100.0f; // Pa -> hPa

    // Validación de rango físico plausible (evita saltos por lecturas erróneas)
    if (!isnan(t) && t > -40.0f  && t <   85.0f)  g_temperature = t;
    if (!isnan(h) && h >=  0.0f  && h <= 100.0f)  g_humidity    = h;
    if (!isnan(p) && p >= 300.0f && p <= 1013.0f) g_pressure    = p;

    // MQ135 RAW (promedio)
    g_rawMQ = readMQ135_RAW();

    // Fusión -> estado global
    g_airState = computeAirState(g_temperature, g_humidity, g_rawMQ, g_pm2_5);

    // Actualización LED
    updateLED(g_airState);

    // Debug serial (opcional)
    Serial.print("T:");     Serial.print(g_temperature, 1);
    Serial.print(" H:");    Serial.print(g_humidity, 0);
    Serial.print(" P:");    Serial.print(g_pressure, 0);
    Serial.print(" RAW:");  Serial.print(g_rawMQ);
    Serial.print(" PM25:"); Serial.print(g_pm2_5);
    Serial.print(" EST:");  Serial.println(STATE_LABEL[g_airState]);
  }

  // --------------------------------------------------------------------------
  // TAREA 2 (continuo): PMS5003 (no bloqueante)
  // --------------------------------------------------------------------------
  {
    PMS5003Data pmsData;
    if (parsePMS5003(pmsData)) {
      g_pm2_5 = pmsData.pm2_5_atm;
    }
  }

  // --------------------------------------------------------------------------
  // TAREA 3 (continuo): Buzzer
  // --------------------------------------------------------------------------
  updateBuzzer(g_airState);

  // --------------------------------------------------------------------------
  // TAREA 4 (cada 5 s): Cambio de pantalla (lcd.clear solo en el cambio)
  // --------------------------------------------------------------------------
  if (now - lastScreenSwitch >= SCREEN_INTERVAL) {
    lastScreenSwitch = now;
    g_currentScreen  = 1 - g_currentScreen;
    lcd.clear();
    lastLCDUpdate = 0; // fuerza refresco inmediato tras el clear
  }

  // --------------------------------------------------------------------------
  // TAREA 5 (cada 500 ms): Refresco LCD (sin clear)
  // --------------------------------------------------------------------------
  if (now - lastLCDUpdate >= LCD_UPDATE_MS) {
    lastLCDUpdate = now;
    updateLCD();
  }

  // Pequeña pausa (suaviza CPU sin afectar la respuesta del sistema)
  delay(5);
}