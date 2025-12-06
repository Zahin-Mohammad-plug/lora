// ===== RX: Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) =====
// v2 - Fixed radio state management and message handling
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "HT_SSD1306Wire.h"
#include <RadioLib.h>

// ---------- CONFIG ----------
const uint8_t  OLED_BRIGHTNESS = 128;  // 0..255
const bool     AUTO_HIDE       = false; // Disable auto-hide for debugging
const uint32_t HIDE_AFTER_MS   = 30000;
const int      FONT_PX         = 10;
const bool     WRAP_TEXT       = true;

// ---------- RADIO CONFIG (MUST MATCH TX) ----------
const float FREQ_MHZ = 915.0;
const int   SF       = 10;
const float BW_KHZ   = 125.0;
const int   CR       = 5;
const uint8_t SYNC_WORD = 0x12;  // LoRa default sync word
const int   PREAMBLE_LEN = 8;

// ---------- ENCODER PINS ----------
const int pinA = 40;
const int pinB = 4;
volatile int scrollOffset = 0;
volatile int lastEncoded = 0;

// Heltec V3 SX1262 pins
SX1262 radio = new Module(8, 14, 12, 13);

// OLED
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

void VextON()  { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW); }
void VextOFF() { pinMode(Vext, OUTPUT); digitalWrite(Vext, HIGH); }

// Display buffer
String displayText = "";
uint32_t lastDraw = 0;
int lastRSSI = 0;
float lastSNR = 0;
uint32_t msgCount = 0;

// ---------- ENCODER ISR ----------
void IRAM_ATTR handleEncoder() {
  int MSB = digitalRead(pinA);
  int LSB = digitalRead(pinB);
  int encoded = (MSB << 1) | LSB;
  int sum = (lastEncoded << 2) | encoded;
  if (sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) scrollOffset--;
  if (sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) scrollOffset++;
  lastEncoded = encoded;
}

int lineHeight() { return FONT_PX == 16 ? 16 : (FONT_PX == 24 ? 24 : 12); }
int maxCharsPerLine() { return FONT_PX == 16 ? 12 : (FONT_PX == 24 ? 8 : 20); }

void setFont() {
  if (FONT_PX == 16) display.setFont(ArialMT_Plain_16);
  else if (FONT_PX == 24) display.setFont(ArialMT_Plain_24);
  else display.setFont(ArialMT_Plain_10);
}

void appendMessage(const String& msg) {
  if (displayText.length() > 0) {
    displayText += WRAP_TEXT ? " " : "\n";
  }
  displayText += msg;
  if (displayText.length() > 500) {
    displayText = displayText.substring(displayText.length() - 500);
  }
}

void redrawAll() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  setFont();

  const int H = 64, h = lineHeight(), maxLines = H / h, maxC = maxCharsPerLine();
  String lines[50];
  int lineCount = 0;
  
  int pos = 0;
  while (pos < (int)displayText.length() && lineCount < 50) {
    int end = min(pos + maxC, (int)displayText.length());
    if (end < (int)displayText.length() && displayText[end] != ' ') {
      int lastSpace = displayText.lastIndexOf(' ', end);
      if (lastSpace > pos) end = lastSpace;
    }
    String line = displayText.substring(pos, end);
    line.trim();
    if (line.length() > 0) lines[lineCount++] = line;
    pos = end;
    if (pos < (int)displayText.length() && displayText[pos] == ' ') pos++;
  }

  int maxScroll = max(0, lineCount - maxLines);
  scrollOffset = constrain(scrollOffset, 0, maxScroll);
  
  int start = max(0, lineCount - maxLines - scrollOffset);
  int y = 0;
  for (int i = start; i < min(start + maxLines, lineCount); ++i) {
    display.drawString(0, y, lines[i]);
    y += h;
  }

  // Status bar at bottom
  display.setFont(ArialMT_Plain_10);
  char status[32];
  snprintf(status, sizeof(status), "Msgs:%lu RSSI:%d", (unsigned long)msgCount, lastRSSI);
  display.drawString(0, 54, status);

  display.display();
  lastDraw = millis();
}

void initRadio() {
  Serial.println("Initializing radio...");
  SPI.begin(9, 11, 10, 8);
  
  int st = radio.begin(FREQ_MHZ);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.print("radio.begin FAILED: "); Serial.println(st);
    display.clear();
    display.drawString(0, 0, "RADIO FAIL: " + String(st));
    display.display();
    while (true) delay(1000);
  }
  
  radio.setDio2AsRfSwitch(true);
  radio.setSpreadingFactor(SF);
  radio.setBandwidth(BW_KHZ);
  radio.setCodingRate(CR);
  radio.setSyncWord(SYNC_WORD);
  radio.setPreambleLength(PREAMBLE_LEN);
  radio.setCRC(true);
  radio.standby();
  
  Serial.println("Radio initialized:");
  Serial.print("  Freq: "); Serial.print(FREQ_MHZ); Serial.println(" MHz");
  Serial.print("  SF: "); Serial.println(SF);
  Serial.print("  BW: "); Serial.print(BW_KHZ); Serial.println(" kHz");
  Serial.print("  CR: 4/"); Serial.println(CR);
  Serial.print("  Sync: 0x"); Serial.println(SYNC_WORD, HEX);
  Serial.print("  Preamble: "); Serial.println(PREAMBLE_LEN);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== LoRa RX v2 Starting ===");

  VextON();
  delay(100);
  display.init();
  display.setContrast(OLED_BRIGHTNESS);
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "LoRa RX v2");
  display.drawString(0, 12, "Initializing...");
  display.display();

  initRadio();

  // Encoder setup
  pinMode(pinA, INPUT_PULLUP);
  pinMode(pinB, INPUT_PULLUP);
  lastEncoded = (digitalRead(pinA) << 1) | digitalRead(pinB);
  attachInterrupt(digitalPinToInterrupt(pinA), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB), handleEncoder, CHANGE);

  displayText = "";
  appendMessage("RX Ready");
  appendMessage("Waiting...");
  redrawAll();
  
  Serial.println("=== RX Ready ===\n");
}

void loop() {
  static int lastScrollOffset = -1;
  static uint32_t lastDebug = 0;
  
  // Redraw on scroll change
  if (scrollOffset != lastScrollOffset) {
    lastScrollOffset = scrollOffset;
    redrawAll();
  }
  
  // Try to receive (blocking with timeout)
  String msg;
  int state = radio.receive(msg, 100);  // 100ms timeout
  
  if (state == RADIOLIB_ERR_NONE && msg.length() > 0) {
    // Got a message!
    lastRSSI = radio.getRSSI();
    lastSNR = radio.getSNR();
    msgCount++;
    
    Serial.println("================");
    Serial.print("RX MSG: '"); Serial.print(msg); Serial.println("'");
    Serial.print("  Length: "); Serial.println(msg.length());
    Serial.print("  RSSI: "); Serial.print(lastRSSI); Serial.println(" dBm");
    Serial.print("  SNR: "); Serial.print(lastSNR); Serial.println(" dB");
    
    // Print hex dump for debugging
    Serial.print("  Hex: ");
    for (unsigned int i = 0; i < msg.length(); i++) {
      Serial.print((uint8_t)msg[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
    
    // Display the message
    appendMessage(msg);
    scrollOffset = 0;
    lastScrollOffset = -1;
    redrawAll();
    
    // Send ACK
    radio.standby();
    delay(20);
    
    String ack = "A," + String(lastRSSI) + "," + String(lastSNR, 1);
    Serial.print("Sending ACK: '"); Serial.print(ack); Serial.println("'");
    
    int ackState = radio.transmit(ack);
    if (ackState == RADIOLIB_ERR_NONE) {
      Serial.println("ACK sent OK");
    } else {
      Serial.print("ACK FAILED: "); Serial.println(ackState);
    }
    
    // Back to standby, ready for next receive
    radio.standby();
    delay(10);
    Serial.println("================\n");
  }
  
  // Debug output every 10 seconds
  if (millis() - lastDebug > 10000) {
    lastDebug = millis();
    Serial.print("Status: msgs="); Serial.print(msgCount);
    Serial.print(" scroll="); Serial.print(scrollOffset);
    Serial.print(" pinA="); Serial.print(digitalRead(pinA));
    Serial.print(" pinB="); Serial.println(digitalRead(pinB));
  }
  
  // Auto-hide
  if (AUTO_HIDE && lastDraw && (millis() - lastDraw > HIDE_AFTER_MS)) {
    display.clear();
    display.display();
    lastDraw = 0;
  }
}
