/*
  MAX30102 Heart Rate + SpO2 with SSD1306 OLED Display (Adafruit Feather M0)

  Reads heart rate and blood oxygen (SpO2) from a MAX30102 sensor and shows
  them on a 0.96" 128x64 SSD1306 OLED. Readings are also printed to the
  Serial Monitor / Serial Plotter at 115200 baud.

  ---------------------------------------------------------------------------
  Original example:
    Optical SP02 Detection (SPK Algorithm) using the MAX30105 Breakout
    By: Nathan Seidle @ SparkFun Electronics
    Date: October 19th, 2016
    https://github.com/sparkfun/MAX30105_Breakout

    This example is based on MAXREFDES117 and RD117_LILYPAD.ino from Maxim.
    Their example was modified to work with the SparkFun MAX30105 library.
    Please see the SparkFun library's license file for more info.

  Modifications in this version:
    - Added SSD1306 OLED output for heart rate and SpO2
      (updateDisplay() and showMessage() functions)
    - Removed the "press any key to start" wait so the sketch starts on its own
    - Removed unused pulseLED and MAX_BRIGHTNESS
    - Simplified serial output to one Plotter-friendly line per calculation
      (HR:72,SpO2:98)
    - Wiring updated for the Adafruit Feather M0
  ---------------------------------------------------------------------------

  Hardware connections (Adafruit Feather M0):
    Both modules share the I2C bus and connect to the same pins.

    MAX30102  VIN -> 3V            OLED  VCC -> 3V
              GND -> GND                 GND -> GND
              SDA -> SDA                 SDA -> SDA
              SCL -> SCL                 SCL -> SCL
              INT -> not connected

    The Feather M0 is a 3.3 V board and its pins are not 5 V tolerant,
    so power both modules from the 3V pin, not USB/5V.
    On the Feather M0, A4 and A5 are NOT I2C pins - use the pins labeled
    SDA and SCL.

    I2C addresses: MAX30102 = 0x57, OLED = 0x3C (some modules use 0x3D;
    change OLED_ADDRESS below if the screen stays blank).

  Tip: attach the sensor to your finger with a rubber band or tape.
  Uneven pressure changes blood flow in the finger and makes readings jumpy.

  Libraries (install via Library Manager):
    - SparkFun MAX3010x Pulse and Proximity Sensor Library
    - Adafruit SSD1306
    - Adafruit GFX Library

  Hobby/educational project - not a medical device.
*/

#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- OLED ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1       // no reset pin
#define OLED_ADDRESS 0x3C   // try 0x3D if the screen stays blank
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------- Sensor ----------
MAX30105 particleSensor;

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
uint16_t irBuffer[100];
uint16_t redBuffer[100];
#else
uint32_t irBuffer[100];
uint32_t redBuffer[100];
#endif

int32_t bufferLength;
int32_t spo2;
int8_t validSPO2;
int32_t heartRate;
int8_t validHeartRate;

byte readLED = 13; // blinks with each data read

// Draws the current readings on the OLED
void updateDisplay()
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // Heart rate
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(F("Heart rate (BPM)"));
  display.setTextSize(2);
  display.setCursor(0, 12);
  if (validHeartRate) display.print(heartRate);
  else                display.print(F("--"));

  // SpO2
  display.setTextSize(1);
  display.setCursor(0, 36);
  display.print(F("SpO2 (%)"));
  display.setTextSize(2);
  display.setCursor(0, 48);
  if (validSPO2) display.print(spo2);
  else           display.print(F("--"));

  display.display();
}

void showMessage(const __FlashStringHelper *line1, const __FlashStringHelper *line2)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.println(line1);
  display.println(line2);
  display.display();
}

void setup()
{
  Serial.begin(115200);
  pinMode(readLED, OUTPUT);

  // Start the OLED first so it can show errors
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS))
  {
    Serial.println(F("SSD1306 not found (or not enough RAM)."));
    while (1);
  }
  showMessage(F("Starting..."), F(""));

  // Start the sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST))
  {
    Serial.println(F("MAX30102 was not found. Please check wiring/power."));
    showMessage(F("Sensor not found!"), F("Check wiring."));
    while (1);
  }

  byte ledBrightness = 60; // 0=Off to 255=50mA
  byte sampleAverage = 4;  // 1, 2, 4, 8, 16, 32
  byte ledMode = 2;        // 2 = Red + IR
  byte sampleRate = 100;   // 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411;    // 69, 118, 215, 411
  int adcRange = 4096;     // 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);

  showMessage(F("Place finger on"), F("sensor..."));
}

void loop()
{
  bufferLength = 100; // 4 seconds of samples at 25 sps

  // Fill the buffer with the first 100 samples
  for (byte i = 0; i < bufferLength; i++)
  {
    while (particleSensor.available() == false)
      particleSensor.check();

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
  }

  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
  updateDisplay();

  // Keep sampling; recalculate every 25 samples (about once per second)
  while (1)
  {
    // Shift the last 75 samples to the front
    for (byte i = 25; i < 100; i++)
    {
      redBuffer[i - 25] = redBuffer[i];
      irBuffer[i - 25] = irBuffer[i];
    }

    // Take 25 new samples
    for (byte i = 75; i < 100; i++)
    {
      while (particleSensor.available() == false)
        particleSensor.check();

      digitalWrite(readLED, !digitalRead(readLED));

      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample();
    }

    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

    // Serial output in Plotter-friendly "label:value" format
    Serial.print(F("HR:"));
    Serial.print(validHeartRate ? heartRate : 0);
    Serial.print(F(",SpO2:"));
    Serial.println(validSPO2 ? spo2 : 0);

    // Update the OLED once per calculation, not every sample,
    // so screen refreshes don't slow down sampling
    updateDisplay();
  }
}
