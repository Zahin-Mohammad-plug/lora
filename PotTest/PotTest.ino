// ===== POTENTIOMETER CALIBRATION TEST =====
// Heltec WiFi LoRa 32 V3
// Pot on GPIO 4 (ADC), 3.3V, GND
#include <Arduino.h>
#include <Wire.h>
#include "HT_SSD1306Wire.h"

// OLED Display
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
void VextON() { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW); }

// Potentiometer pin
const int potPin = 4;

// Calibration values (will be determined)
int minVal = 4095;  // Track minimum seen
int maxVal = 0;     // Track maximum seen

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize OLED
  VextON();
  delay(100);
  display.init();
  display.setContrast(255);
  
  // Configure ADC
  analogReadResolution(12);  // 12-bit (0-4095)
  analogSetAttenuation(ADC_11db);  // Full 0-3.3V range
  pinMode(potPin, INPUT);
  
  Serial.println("\n========================================");
  Serial.println("   POTENTIOMETER CALIBRATION");
  Serial.println("========================================");
  Serial.println("Rotate pot fully CCW then CW to calibrate");
  Serial.println("========================================\n");
}

void loop() {
  static uint32_t lastUpdate = 0;
  
  // Read potentiometer
  int rawValue = analogRead(potPin);
  
  // Update min/max for calibration
  if (rawValue < minVal) minVal = rawValue;
  if (rawValue > maxVal) maxVal = rawValue;
  
  // Calculate percentage (0-100%)
  int range = maxVal - minVal;
  int percent = 0;
  if (range > 100) {  // Only calculate if we have a reasonable range
    percent = map(rawValue, minVal, maxVal, 0, 100);
    percent = constrain(percent, 0, 100);
  }
  
  // Calculate scroll position (0 = bottom, 10 = top)
  int scrollPos = map(percent, 0, 100, 0, 10);
  
  // Update display
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  
  display.drawString(0, 0, "POT CALIBRATION");
  display.drawString(0, 12, "Raw: " + String(rawValue));
  display.drawString(64, 12, "(" + String(minVal) + "-" + String(maxVal) + ")");
  display.drawString(0, 24, "Percent: " + String(percent) + "%");
  display.drawString(0, 36, "Scroll Pos: " + String(scrollPos));
  
  // Draw visual bar
  int barWidth = map(percent, 0, 100, 0, 120);
  display.drawRect(0, 50, 124, 10);
  display.fillRect(2, 52, barWidth, 6);
  
  display.display();
  
  // Serial output every 200ms
  if (millis() - lastUpdate > 200) {
    lastUpdate = millis();
    Serial.print("Raw="); Serial.print(rawValue);
    Serial.print(" Min="); Serial.print(minVal);
    Serial.print(" Max="); Serial.print(maxVal);
    Serial.print(" %="); Serial.print(percent);
    Serial.print(" Scroll="); Serial.println(scrollPos);
  }
  
  delay(50);
}

