// ===== RX: Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) =====
// Display: SSD1306Wire (matches your working code)
// Radio: RadioLib with explicit SX1262 pin mapping

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "HT_SSD1306Wire.h"
#include <RadioLib.h>

// ---------- CONFIG (tweak here) ----------
const uint8_t  OLED_BRIGHTNESS = 0;   // 0..255
const bool     AUTO_HIDE        = true; // true = clear screen after HIDE_AFTER_MS
const uint32_t HIDE_AFTER_MS    = 10000; // only used if AUTO_HIDE = true
// Font size: choose one of 10, 16, or 24
const int      FONT_PX          = 10;    // 10 ~20 chars/line, 16 ~12, 24 ~8
// Text wrapping: true = messages concatenated with spaces "1 2 3", false = each message on new line
const bool     WRAP_TEXT        = true;  // false = each message starts on new line
// ----------------------------------------

// ---------- ENCODER PINS FOR SCROLLING ----------
const int pinA = 40;  // CLK pin
const int pinB = 4;   // DT pin

volatile int scrollOffset = 0;  // Scroll position (lines to offset)
volatile int lastEncoded = 0;

// Heltec V3 SX1262 pins
SX1262 radio = new Module(/*cs*/8, /*irq(DIO1)*/14, /*rst*/12, /*busy*/13);

void setRadioParams() {
  radio.setSpreadingFactor(10);
  radio.setBandwidth(125.0);
  radio.setCodingRate(5);
  radio.setSyncWord(0x34); // Changed from 0x12
}

// OLED
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED,
                           GEOMETRY_128_64, RST_OLED);

void VextON()  { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW);  }
void VextOFF() { pinMode(Vext, OUTPUT); digitalWrite(Vext, HIGH); }

// ---------- ENCODER ISR FOR SCROLLING ----------
void IRAM_ATTR handleEncoder() {
  int MSB = digitalRead(pinA);  // MSB = most significant bit
  int LSB = digitalRead(pinB);  // LSB = least significant bit

  int encoded = (MSB << 1) | LSB;
  int sum = (lastEncoded << 2) | encoded;

  if(sum == 0b1101 || sum == 0b0100 || sum == 0b0010 || sum == 0b1011) scrollOffset--;  // Scroll up
  if(sum == 0b1110 || sum == 0b0111 || sum == 0b0001 || sum == 0b1000) scrollOffset++;  // Scroll down

  lastEncoded = encoded;
}

// simple display buffer: messages are concatenated with spaces, word-wrapped
String displayText = "";  // all received messages concatenated

uint32_t lastDraw = 0;
int lastRSSI = 0;
float lastSNR = 0;

// derive per-font metrics
int lineHeight() {
  if (FONT_PX == 16) return 16;
  if (FONT_PX == 24) return 24;
  return 12;  // for 10px font we give a tiny 2px lead
}
int maxCharsPerLine() {
  if (FONT_PX == 16) return 12;
  if (FONT_PX == 24) return 8;
  return 20;  // font 10
}
void setFont() {
  if (FONT_PX == 16) display.setFont(ArialMT_Plain_16);
  else if (FONT_PX == 24) display.setFont(ArialMT_Plain_24);
  else display.setFont(ArialMT_Plain_10);
}

// append new message to display text with space or newline separator
void appendMessage(const String& msg) {
  if (displayText.length() > 0) {
    displayText += WRAP_TEXT ? " " : "\n";  // add space or newline between messages
  }
  displayText += msg;
  
  // Keep only last ~500 chars to prevent overflow
  if (displayText.length() > 500) {
    displayText = displayText.substring(displayText.length() - 500);
  }
}

// word-wrap the display text and show lines based on scroll position
void redrawAll() {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  setFont();

  const int H = 64;  // full screen height
  const int h = lineHeight();
  const int maxLines = H / h;
  const int maxC = maxCharsPerLine();

  // Word-wrap the entire display text
  String lines[50];  // temp array for wrapped lines (increased for scrolling)
  int lineCount = 0;
  
  int pos = 0;
  while (pos < displayText.length() && lineCount < 50) {
    int end = min(pos + maxC, (int)displayText.length());
    
    // Try to break at word boundary if not at end
    if (end < displayText.length() && displayText[end] != ' ') {
      int lastSpace = displayText.lastIndexOf(' ', end);
      if (lastSpace > pos) {
        end = lastSpace;
      }
    }
    
    String line = displayText.substring(pos, end);
    line.trim();
    if (line.length() > 0) {
      lines[lineCount++] = line;
    }
    
    pos = end;
    if (pos < displayText.length() && displayText[pos] == ' ') pos++;
  }

  // Constrain scroll offset to valid range
  int maxScroll = max(0, lineCount - maxLines);
  scrollOffset = constrain(scrollOffset, 0, maxScroll);
  
  // Calculate which lines to show based on scroll position
  // scrollOffset = 0 means show the LAST lines (newest at bottom)
  // scrollOffset > 0 means scroll up to see older messages
  int start = max(0, lineCount - maxLines - scrollOffset);
  int y = 0;
  for (int i = start; i < min(start + maxLines, lineCount); ++i) {
    display.drawString(0, y, lines[i]);
    y += h;
    if (y >= H) break;
  }

  // Show scroll indicator if there are more lines
  if (lineCount > maxLines) {
    // Draw scroll bar on right edge
    int barHeight = (H * maxLines) / lineCount;
    int barY = (H - barHeight) * scrollOffset / maxScroll;
    display.drawLine(127, barY, 127, barY + barHeight);
  }

  display.display();
  lastDraw = millis();
}

void setup() {
  Serial.begin(115200);

  VextON(); delay(100);
  display.init();
  display.setContrast(OLED_BRIGHTNESS);
  display.clear(); display.display();

  // Boot banner - these will be cleared after radio is ready
  appendMessage("LoRa RX @ 915 MHz");
  appendMessage("SF10 BW125 CR4/5");
  appendMessage("Waiting...");
  redrawAll();

  // SPI + radio
  SPI.begin(/*SCK*/9, /*MISO*/11, /*MOSI*/10, /*SS*/8);
  int st = radio.begin(915.0);
  if (st != RADIOLIB_ERR_NONE) {
    display.clear(); display.drawString(0,0,"radio.begin FAIL");
    display.display();
    Serial.println("radio.begin FAIL: " + String(st));
    while (true) delay(1000);
  }
  radio.setDio2AsRfSwitch(true);
  setRadioParams(); // Use the new function
  radio.setCRC(true);
  
  // Wait 3 seconds then clear initialization messages
  delay(3000);
  displayText = "";  // Clear init messages
  display.clear();
  display.display();
  Serial.println("RX ready - initialization messages cleared");
  
  // Setup encoder for scrolling
  pinMode(pinA, INPUT_PULLUP);
  pinMode(pinB, INPUT_PULLUP);
  int MSB = digitalRead(pinA);
  int LSB = digitalRead(pinB);
  lastEncoded = (MSB << 1) | LSB;
  attachInterrupt(digitalPinToInterrupt(pinA), handleEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(pinB), handleEncoder, CHANGE);
  Serial.println("Encoder initialized for scrolling");
}

void loop() {
  static int lastScrollOffset = -1;
  
  // Check if scroll position changed and redraw
  if (scrollOffset != lastScrollOffset) {
    lastScrollOffset = scrollOffset;
    redrawAll();
  }
  
  String msg;
  int state = radio.receive(msg);          // ~500ms wait for a packet
  if (state == RADIOLIB_ERR_NONE) {
    // Get RSSI and SNR BEFORE doing anything else
    lastRSSI = radio.getRSSI();
    lastSNR  = radio.getSNR();

    Serial.print("RX: '"); Serial.print(msg); 
    Serial.print("' RSSI:"); Serial.print(lastRSSI);
    Serial.print(" SNR:"); Serial.println(lastSNR);

    // add message to display text
    appendMessage(msg);
    scrollOffset = 0;  // Reset scroll to bottom when new message arrives
    redrawAll();

    // ACK back w/ metrics (TX parses these)
    // Force radio into correct state for sending ACK
    Serial.println("Re-setting radio params for ACK send...");
    radio.standby();
    setRadioParams();
    delay(10); // Short delay for settings to apply

    String ack = String("A,") + String(lastRSSI) + "," + String(lastSNR,1);
    int ackState = radio.transmit(ack);
    Serial.print("ACK sent: '"); Serial.print(ack); Serial.print("' state:"); Serial.println(ackState);
    Serial.println("Returning to RX mode...");
  }

  if (AUTO_HIDE && lastDraw && (millis() - lastDraw > HIDE_AFTER_MS)) {
    display.clear(); display.display();
    lastDraw = 0;
  }
}
