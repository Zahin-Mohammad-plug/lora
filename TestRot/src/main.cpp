#include <Arduino.h>
#include <Wire.h>
#include <SSD1306Wire.h>

// ---------- ENCODER PINS (GPIO 4 & 5 are safer) ----------
const int pinA = 4;  // CLK pin - Changed from 32 to avoid conflicts
const int pinB = 5;  // DT pin - Changed from 33 to avoid conflicts

volatile int encoderPos = 0;
volatile int lastEncoded = 0;

// ---------- HELTEC V3 OLED (using ThingPulse library, Heltec V3 pins) ----------
SSD1306Wire display(0x3c, SDA_OLED, SCL_OLED);

void VextON()  { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW);  }
void VextOFF() { pinMode(Vext, OUTPUT); digitalWrite(Vext, HIGH); }

void resetDisplay() {
  pinMode(RST_OLED, OUTPUT);
  digitalWrite(RST_OLED, LOW);
  delay(50);
  digitalWrite(RST_OLED, HIGH);
}

// ---------- ENCODER ISR (improved algorithm) ----------
void IRAM_ATTR handleEncoder() {
  int MSB = digitalRead(pinA);  // MSB = most significant bit
  int LSB = digitalRead(pinB);  // LSB = least significant bit

  int encoded = (MSB << 1) | LSB;           // Convert the 2 pin values to single number
  int sum = (lastEncoded << 2) | encoded;   // Add it to the previous encoded value

  if(sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) encoderPos++;
  if(sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) encoderPos--;

  lastEncoded = encoded;  // Store this value for next time
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n\nStarting Encoder Test...");

  // Turn on OLED power and init display
  Serial.println("Powering on display...");
  VextON();
  delay(100);
  
  Serial.println("Resetting display...");
  resetDisplay();
  delay(50);
  
  Serial.println("Initializing display...");
  Wire.begin(SDA_OLED, SCL_OLED);
  display.init();
  display.flipScreenVertically();
  display.setContrast(200);
  display.clear();
  display.display();

  Serial.println("Drawing boot text...");
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_16);
  display.drawString(0, 0, "Encoder:");
  display.drawString(0, 20, "0");
  display.display();

  // Encoder pins - attach interrupts to BOTH pins
  Serial.println("Setting up encoder pins...");
  pinMode(pinA, INPUT_PULLUP);
  pinMode(pinB, INPUT_PULLUP);
  
  // Read initial state
  int MSB = digitalRead(pinA);
  int LSB = digitalRead(pinB);
  lastEncoded = (MSB << 1) | LSB;

  attachInterrupt(digitalPinToInterrupt(pinA), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB), handleEncoder, CHANGE);
  
  Serial.println("Ready! Rotate the encoder.");
}

void loop() {
  static int lastShown = 0;

  if (encoderPos != lastShown) {
    lastShown = encoderPos;

    // Debug to Serial
    Serial.println(lastShown);

    // Update OLED
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_16);
    display.drawString(0, 0, "Encoder:");
    display.drawString(0, 20, String(lastShown));
    display.display();
  }
}
