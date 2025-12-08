// ===== ENCODER TEST - Heltec WiFi LoRa 32 V3 =====
// SIMPLE SINGLE PIN TEST - just count clicks
#include <Arduino.h>
#include <Wire.h>
#include "HT_SSD1306Wire.h"

// OLED Display
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
void VextON() { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW); }

// SINGLE PIN TEST - connect encoder to GPIO 4
const int encoderPin = 4;

// Counters
volatile int clickCount = 0;
volatile int risingCount = 0;
volatile int fallingCount = 0;
volatile uint32_t lastClickTime = 0;

// Debounce - 100ms (very aggressive for noisy encoder)
const uint32_t DEBOUNCE_US = 100000;

// Simple ISR - count ALL transitions
void IRAM_ATTR encoderClick() {
  uint32_t now = micros();
  if (now - lastClickTime < DEBOUNCE_US) return;
  lastClickTime = now;
  
  clickCount++;
  if (digitalRead(encoderPin) == HIGH) {
    risingCount++;
  } else {
    fallingCount++;
  }
}

void updateDisplay() {
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  
  // Title
  display.drawString(0, 0, "SINGLE PIN TEST");
  
  // Counts
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 18, "GPIO: " + String(encoderPin));
  display.drawString(0, 30, "Total Clicks: " + String(clickCount));
  display.drawString(0, 42, "Rising:  " + String(risingCount));
  display.drawString(0, 54, "Falling: " + String(fallingCount));
  
  display.display();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  // Initialize OLED
  VextON();
  delay(100);
  display.init();
  display.setContrast(255);
  display.clear();
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 20, "Single Pin Test");
  display.drawString(0, 40, "GPIO " + String(encoderPin));
  display.display();
  delay(1000);
  
  Serial.println("\n\n========================================");
  Serial.println("   SINGLE PIN ENCODER TEST");
  Serial.println("========================================");
  Serial.println();
  Serial.print("Testing GPIO: "); Serial.println(encoderPin);
  Serial.println("Connect encoder to this pin only!");
  Serial.println();
  
  pinMode(encoderPin, INPUT_PULLUP);
  
  int initState = digitalRead(encoderPin);
  Serial.print("Initial pin state: "); Serial.println(initState);
  Serial.println();
  
  attachInterrupt(digitalPinToInterrupt(encoderPin), encoderClick, CHANGE);
  
  Serial.println("Rotate 5 clicks and watch the count!");
  Serial.println("========================================\n");
  
  updateDisplay();
}

void loop() {
  static int lastClick = 0;
  static uint32_t lastPrint = 0;
  
  // Check for changes
  noInterrupts();
  int clicks = clickCount;
  int rising = risingCount;
  int falling = fallingCount;
  interrupts();
  
  // Print immediately when there's a change
  if (clicks != lastClick) {
    Serial.print(">>> CLICK #"); Serial.print(clicks);
    Serial.print("  (Rising="); Serial.print(rising);
    Serial.print(" Falling="); Serial.print(falling);
    Serial.println(")");
    
    lastClick = clicks;
    updateDisplay();
  }
  
  // Status every 2 seconds
  if (millis() - lastPrint > 2000) {
    lastPrint = millis();
    int pinState = digitalRead(encoderPin);
    Serial.print("[Status] Pin="); Serial.print(pinState);
    Serial.print("  Clicks="); Serial.print(clicks);
    Serial.print("  Rising="); Serial.print(rising);
    Serial.print("  Falling="); Serial.println(falling);
  }
  
  delay(10);
}

