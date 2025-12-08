// ===== RX: Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) =====
// v2.1 - Potentiometer scroll, newest message on top
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "HT_SSD1306Wire.h"
#include <RadioLib.h>

// ---------- CONFIG ----------
const uint8_t  OLED_BRIGHTNESS = 128;
const int      FONT_PX         = 10;
const bool     WRAP_TEXT       = true;
const int      MAX_SCROLL      = 10;  // Scroll range (adjust for sensitivity)

// ---------- RADIO CONFIG (MUST MATCH TX) ----------
const float FREQ_MHZ = 915.0;
const int   SF       = 10;
const float BW_KHZ   = 125.0;
const int   CR       = 5;
const uint8_t SYNC_WORD = 0x12;
const int   PREAMBLE_LEN = 8;

// ---------- POTENTIOMETER ----------
const int potPin = 4;  // GPIO 4 for potentiometer
int scrollOffset = 0;
int lastPotValue = -1;

// Heltec V3 SX1262 pins
SX1262 radio = new Module(8, 14, 12, 13);

// OLED
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

void VextON()  { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW); }
void VextOFF() { pinMode(Vext, OUTPUT); digitalWrite(Vext, HIGH); }

// Message storage - store individual messages for proper ordering
const int MAX_MESSAGES = 50;
String messages[MAX_MESSAGES];
int messageCount = 0;

uint32_t lastDraw = 0;
int lastRSSI = 0;
float lastSNR = 0;

int lineHeight() { return FONT_PX == 16 ? 16 : (FONT_PX == 24 ? 24 : 12); }
int maxCharsPerLine() { return FONT_PX == 16 ? 12 : (FONT_PX == 24 ? 8 : 20); }

void setFont() {
  if (FONT_PX == 16) display.setFont(ArialMT_Plain_16);
  else if (FONT_PX == 24) display.setFont(ArialMT_Plain_24);
  else display.setFont(ArialMT_Plain_10);
}

void addMessage(const String& msg) {
  // Shift messages if full
  if (messageCount >= MAX_MESSAGES) {
    for (int i = 0; i < MAX_MESSAGES - 1; i++) {
      messages[i] = messages[i + 1];
    }
    messageCount = MAX_MESSAGES - 1;
  }
  messages[messageCount++] = msg;
}

void redrawAll() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  
  // Calculate layout (full screen for scrollable content)
  const int h = lineHeight();
  const int maxLines = 64 / h;  // All lines available (5 lines at 10px font)
  const int maxC = maxCharsPerLine();
  
  // Build display lines from messages (newest first!)
  String lines[100];
  int lineCount = 0;
  
  // Process messages in REVERSE order (newest first)
  for (int m = messageCount - 1; m >= 0 && lineCount < 100; m--) {
    String msg = messages[m];
    
    if (WRAP_TEXT) {
      // Word wrap this message
      int pos = 0;
      while (pos < (int)msg.length() && lineCount < 100) {
        int end = min(pos + maxC, (int)msg.length());
        if (end < (int)msg.length() && msg[end] != ' ') {
          int lastSpace = msg.lastIndexOf(' ', end);
          if (lastSpace > pos) end = lastSpace;
        }
        String line = msg.substring(pos, end);
        line.trim();
        if (line.length() > 0) lines[lineCount++] = line;
        pos = end;
        if (pos < (int)msg.length() && msg[pos] == ' ') pos++;
      }
    } else {
      // No wrap - one line per message (truncate if needed)
      lines[lineCount++] = msg.substring(0, maxC);
    }
  }
  
  // Add status line as the LAST scrollable line
  char status[32];
  snprintf(status, sizeof(status), "Msgs:%d RSSI:%d Scr:%d", messageCount, lastRSSI, scrollOffset);
  if (lineCount < 100) {
    lines[lineCount++] = String(status);
  }
  
  // Constrain scroll to actual content
  int maxScroll = max(0, lineCount - maxLines);
  scrollOffset = constrain(scrollOffset, 0, maxScroll);
  
  // Draw lines (from top)
  // scrollOffset=0 means show newest messages at top
  // Scroll to bottom to see the status line
  setFont();
  int y = 0;
  for (int i = scrollOffset; i < min(scrollOffset + maxLines, lineCount); i++) {
    display.drawString(0, y, lines[i]);
    y += h;
  }
  
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
  
  Serial.println("Radio initialized");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== LoRa RX v2.1 Starting ===");

  VextON();
  delay(100);
  display.init();
  display.setContrast(OLED_BRIGHTNESS);
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "LoRa RX v2.1");
  display.drawString(0, 12, "Pot scroll mode");
  display.display();

  initRadio();

  // Potentiometer setup
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(potPin, INPUT);
  
  // Add initial message
  addMessage("RX Ready - scroll test");
  redrawAll();
  
  Serial.println("=== RX Ready ===\n");
}

void loop() {
  static int lastScrollOffset = -1;
  static uint32_t lastPotRead = 0;
  
  // Read potentiometer every 50ms
  if (millis() - lastPotRead > 50) {
    lastPotRead = millis();
    
    int potValue = analogRead(potPin);
    
    // Only update if pot moved significantly (reduce jitter)
    if (abs(potValue - lastPotValue) > 50) {
      lastPotValue = potValue;
      
      // Map pot to scroll: 0-4095 -> 0-MAX_SCROLL
      // Pot at 0 = scroll 0 (newest at top)
      // Pot at max = scroll down to older messages
      scrollOffset = map(potValue, 0, 4095, 0, MAX_SCROLL);
      
      Serial.print("Pot: "); Serial.print(potValue);
      Serial.print(" -> Scroll: "); Serial.println(scrollOffset);
    }
  }
  
  // Redraw on scroll change
  if (scrollOffset != lastScrollOffset) {
    lastScrollOffset = scrollOffset;
    redrawAll();
  }
  
  // Try to receive
  uint8_t rxBuffer[64];
  memset(rxBuffer, 0, sizeof(rxBuffer));
  int state = radio.receive(rxBuffer, 50);
  
  if (state == RADIOLIB_ERR_NONE) {
    size_t rxLen = radio.getPacketLength();
    lastRSSI = radio.getRSSI(true);
    lastSNR = radio.getSNR();
    
    if (rxLen > 0 && rxLen <= 50) {
      String msg = "";
      for (size_t i = 0; i < rxLen; i++) {
        msg += (char)rxBuffer[i];
      }
      
      // Send ACK
      radio.standby();
      delay(80);
      String ack = "A," + String(lastRSSI) + "," + String(lastSNR, 1);
      int ackState = radio.transmit(ack);
      radio.standby();
      delay(10);
      
      Serial.println("================");
      Serial.print("RX: '"); Serial.print(msg); Serial.println("'");
      Serial.print("  RSSI: "); Serial.print(lastRSSI); Serial.println(" dBm");
      Serial.print("  ACK: "); Serial.println(ackState == RADIOLIB_ERR_NONE ? "OK" : "FAIL");
      Serial.println("================");
      
      // Add message (newest will be at top)
      addMessage(msg);
      
      // Reset scroll to top when new message arrives
      scrollOffset = 0;
      lastScrollOffset = -1;
      lastPotValue = -1;  // Force pot re-read
      redrawAll();
    }
  }
}
