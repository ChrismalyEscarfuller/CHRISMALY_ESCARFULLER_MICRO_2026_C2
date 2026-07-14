/*
 * Control tipo gamepad con ESP32-S3
 * 2 joysticks analógicos, 4 botones centrales, 4 botones laterales,
 * LCD I2C, MPU6050 (acelerómetro + giróscopo), lectura de batería, HID USB nativo.
 *
 * Librerías necesarias (Arduino Library Manager):
 *  - LiquidCrystal I2C (fdebrabander)
 *
 * Placa: ESP32S3 Dev Module
 * Tools > USB Mode: "USB-OTG (TinyUSB)"
 * Tools > USB CDC On Boot: "Enabled" (para ver Serial por el mismo puerto USB-C)
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "USB.h"
#include "USBHIDGamepad.h"

// ---------- Definición de pines ----------
// Joysticks
#define PIN_JOY1_Y      1
#define PIN_JOY1_X      2
#define PIN_JOY0_BTN    3
#define PIN_JOY0_X      4
#define PIN_JOY0_Y      5
#define PIN_JOY1_BTN    46

// I2C
#define PIN_I2C_SDA     6
#define PIN_I2C_SCL     7

// Batería
#define PIN_VBAT        8

// Botones centrales
#define PIN_BTN0        9
#define PIN_BTN2        10
#define PIN_BTN1        11
#define PIN_BTN3        12

// MPU6050 interrupción
#define PIN_MPU_INT     16

// Botones laterales
#define PIN_BTN_L4      39
#define PIN_BTN_L3      40
#define PIN_BTN_L2      41
#define PIN_BTN_L1      42

// Nota: los pines 19 (D-) y 20 (D+) son para el USB nativo, no se tocan.

// ---------- Constantes generales ----------
#define LCD_ADDR        0x27
#define LCD_COLS        16
#define LCD_ROWS        2

#define MPU_ADDR        0x68
#define ADC_MAX         4095
#define ADC_CENTER      2048
#define JOY_DEADZONE    120

// Divisor de tensión de la batería (ajustar según el hardware real)
#define VBAT_DIVIDER    2.0f
#define VBAT_ADC_VREF   3.3f
#define VBAT_EMPTY      3.0f
#define VBAT_FULL       4.2f

// Índices de botones para el reporte HID (1..N)
enum {
  BTN_IDX_0 = 1,
  BTN_IDX_1,
  BTN_IDX_2,
  BTN_IDX_3,
  BTN_IDX_L1,
  BTN_IDX_L2,
  BTN_IDX_L3,
  BTN_IDX_L4,
  BTN_IDX_JOY0,
  BTN_IDX_JOY1
};

// ---------- Objetos globales ----------
LiquidCrystal_I2C lcd(LCD_ADDR, LCD_COLS, LCD_ROWS);
USBHIDGamepad Gamepad;

// Variables para MPU6050
volatile bool mpuDataReady = false;
int16_t axAxis = 0, ayAxis = 0, azAxis = 0;
int16_t gxAxis = 0, gyAxis = 0, gzAxis = 0;

// Variables de batería
float   batteryVoltage = 0.0f;
uint8_t batteryPercent = 0;

// ---------- MPU6050 (registro directo, sin librería externa) ----------
void mpuWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void mpuInit() {
  mpuWriteReg(0x6B, 0x00); // PWR_MGMT_1: salir de sleep, reloj interno
  mpuWriteReg(0x1C, 0x00); // ACCEL_CONFIG: ±2g
  mpuWriteReg(0x1B, 0x00); // GYRO_CONFIG: ±250 dps
  mpuWriteReg(0x38, 0x01); // INT_ENABLE: interrupción por dato listo
}

bool mpuReadAccelGyro(int16_t *ax, int16_t *ay, int16_t *az,
                       int16_t *gx, int16_t *gy, int16_t *gz) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); // ACCEL_XOUT_H
  if (Wire.endTransmission(false) != 0) return false;
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14, (uint8_t)true);
  if (Wire.available() < 14) return false;

  *ax = (Wire.read() << 8) | Wire.read();
  *ay = (Wire.read() << 8) | Wire.read();
  *az = (Wire.read() << 8) | Wire.read();
  Wire.read(); Wire.read(); // Temperatura (no se usa)
  *gx = (Wire.read() << 8) | Wire.read();
  *gy = (Wire.read() << 8) | Wire.read();
  *gz = (Wire.read() << 8) | Wire.read();
  return true;
}

void IRAM_ATTR mpuISR() {
  mpuDataReady = true;
}

// ---------- Lectura de joysticks ----------
int8_t readAxis(int pin) {
  int raw = analogRead(pin);
  int delta = raw - ADC_CENTER;
  if (abs(delta) < JOY_DEADZONE) delta = 0;

  // Mapear el rango completo (-2048 a +2047) a -127..127
  long mapped = map(delta, -ADC_CENTER, ADC_MAX - ADC_CENTER, -127, 127);
  return (int8_t)constrain(mapped, -127, 127);
}

// ---------- Lectura de batería ----------
void readBattery() {
  int raw = analogRead(PIN_VBAT);
  float pinVoltage = (raw / (float)ADC_MAX) * VBAT_ADC_VREF;
  batteryVoltage = pinVoltage * VBAT_DIVIDER;

  float pct = (batteryVoltage - VBAT_EMPTY) / (VBAT_FULL - VBAT_EMPTY) * 100.0f;
  batteryPercent = (uint8_t)constrain(pct, 0.0f, 100.0f);
}

// ---------- Lectura de botones (mediante arrays) ----------
// Pines de los botones (en el orden de los índices BTN_IDX_*)
const int buttonPins[] = {
  PIN_BTN0,   // BTN_IDX_0
  PIN_BTN1,   // BTN_IDX_1
  PIN_BTN2,   // BTN_IDX_2
  PIN_BTN3,   // BTN_IDX_3
  PIN_BTN_L1, // BTN_IDX_L1
  PIN_BTN_L2, // BTN_IDX_L2
  PIN_BTN_L3, // BTN_IDX_L3
  PIN_BTN_L4, // BTN_IDX_L4
  PIN_JOY0_BTN, // BTN_IDX_JOY0
  PIN_JOY1_BTN  // BTN_IDX_JOY1
};
const int numButtons = sizeof(buttonPins) / sizeof(buttonPins[0]);

uint32_t readAllButtons() {
  uint32_t mask = 0;
  for (int i = 0; i < numButtons; i++) {
    if (digitalRead(buttonPins[i]) == LOW) {
      mask |= (1UL << i); // El bit i corresponde al índice i+1 en el reporte HID
    }
  }
  return mask;
}

// ---------- Actualización del LCD ----------
void updateLCD(int8_t jx0, int8_t jy0, int8_t jx1, int8_t jy1) {
  lcd.setCursor(0, 0);
  lcd.print("Bat:");
  lcd.print(batteryPercent);
  lcd.print("%  ");

  lcd.setCursor(9, 0);
  lcd.print(batteryVoltage, 2);
  lcd.print("V");

  lcd.setCursor(0, 1);
  lcd.print("J0:");
  lcd.print(jx0);
  lcd.print(",");
  lcd.print(jy0);
  lcd.print(" J1:");
  lcd.print(jx1);
  lcd.print(",");
  lcd.print(jy1);
  lcd.print("   ");
}

// ---------- Configuración inicial ----------
void setup() {
  Serial.begin(115200);

  // Configurar todos los botones como entrada con pull-up
  for (int i = 0; i < numButtons; i++) {
    pinMode(buttonPins[i], INPUT_PULLUP);
  }

  // Interrupción del MPU6050
  pinMode(PIN_MPU_INT, INPUT);

  // Resolución ADC y atenuación
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_JOY0_X, ADC_11db);
  analogSetPinAttenuation(PIN_JOY0_Y, ADC_11db);
  analogSetPinAttenuation(PIN_JOY1_X, ADC_11db);
  analogSetPinAttenuation(PIN_JOY1_Y, ADC_11db);
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);

  // Iniciar I2C
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  // Iniciar LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("ESP32-S3 Gamepad");
  delay(1000);
  lcd.clear();

  // Iniciar MPU6050
  mpuInit();
  attachInterrupt(digitalPinToInterrupt(PIN_MPU_INT), mpuISR, RISING);

  // Iniciar USB HID
  Gamepad.begin();
  USB.begin();
}

// ---------- Bucle principal ----------
void loop() {
  static unsigned long lastSend = 0;
  static unsigned long lastLCD = 0;
  static unsigned long lastBattery = 0;
  unsigned long now = millis();

  // Leer MPU6050 si hay interrupción
  if (mpuDataReady) {
    mpuDataReady = false;
    mpuReadAccelGyro(&axAxis, &ayAxis, &azAxis, &gxAxis, &gyAxis, &gzAxis);
  }

  // Actualizar batería cada segundo
  if (now - lastBattery >= 1000) {
    lastBattery = now;
    readBattery();
  }

  // Leer ejes y botones
  int8_t jx0 = readAxis(PIN_JOY0_X);
  int8_t jy0 = readAxis(PIN_JOY0_Y);
  int8_t jx1 = readAxis(PIN_JOY1_X);
  int8_t jy1 = readAxis(PIN_JOY1_Y);
  uint32_t buttons = readAllButtons();

  // Enviar reporte HID cada ~15 ms (≈66 Hz)
  if (now - lastSend >= 15) {
    lastSend = now;
    Gamepad.leftStick(jx0, jy0);
    Gamepad.rightStick(jx1, jy1);
    Gamepad.buttons(buttons);
    Gamepad.send();
  }

  // Actualizar LCD cada 250 ms
  if (now - lastLCD >= 250) {
    lastLCD = now;
    updateLCD(jx0, jy0, jx1, jy1);
  }
}