// ===== TX: Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) =====
// v2 - Fixed radio state management and message handling
#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include "HT_SSD1306Wire.h"
#include <WiFi.h>
#include <WebServer.h>
#include <RadioLib.h>

// Heltec V3 SX1262 pins
SX1262 radio = new Module(8, 14, 12, 13);

// Wi-Fi creds
const char* SSID = "logi_mouse_02";
const char* PASS = "gg1234567";

// OLED
static SSD1306Wire display(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);
void VextON()  { pinMode(Vext, OUTPUT); digitalWrite(Vext, LOW); }
void VextOFF() { pinMode(Vext, OUTPUT); digitalWrite(Vext, HIGH); }

WebServer server(80);

// ---------- RADIO CONFIG (MUST MATCH RX) ----------
const float FREQ_MHZ     = 915.0;
const int   SF           = 10;
const float BW_KHZ       = 125.0;
const int   CR           = 5;
const uint8_t SYNC_WORD  = 0x12;  // LoRa default sync word
const int   PREAMBLE_LEN = 8;
const int   TX_POWER_DBM = 17;

// Metrics
uint32_t pktSent = 0, pktAck = 0;
int lastAckRSSI = 0;
float lastAckSNR = 0.0;
uint32_t lastRTTms = 0;
int lastTxState = 0;

// Message history
struct MessageLog {
  String msg;
  bool acked;
  bool sent;
  uint32_t timestamp;
};
const int MAX_MSG_HISTORY = 20;
MessageLog msgHistory[MAX_MSG_HISTORY];
int msgHistoryCount = 0;

void addToHistory(const String& msg, bool sent, bool acked) {
  if (msgHistoryCount >= MAX_MSG_HISTORY) {
    for (int i = 1; i < MAX_MSG_HISTORY; i++) {
      msgHistory[i-1] = msgHistory[i];
    }
    msgHistoryCount = MAX_MSG_HISTORY - 1;
  }
  msgHistory[msgHistoryCount].msg = msg;
  msgHistory[msgHistoryCount].sent = sent;
  msgHistory[msgHistoryCount].acked = acked;
  msgHistory[msgHistoryCount].timestamp = millis();
  msgHistoryCount++;
}

void drawStatus(const String& stat) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "TX v2 @ 915 MHz");
  display.drawString(0, 12, "WiFi: " + WiFi.SSID());
  display.drawString(0, 24, "W:" + String(WiFi.RSSI()) + "dBm");
  if (lastAckRSSI != 0) {
    display.drawString(64, 24, "L:" + String(lastAckRSSI) + "dBm");
  }
  
  if (msgHistoryCount > 0) {
    MessageLog& last = msgHistory[msgHistoryCount - 1];
    String status = last.acked ? "[ACK]" : (last.sent ? "[SENT]" : "[FAIL]");
    String line = status + " " + last.msg;
    if (line.length() > 21) line = line.substring(0, 21);
    display.drawString(0, 40, line);
  }
  
  char line[64];
  snprintf(line, sizeof(line), "S:%lu A:%lu %s", (unsigned long)pktSent, (unsigned long)pktAck, stat.c_str());
  display.drawString(0, 52, line);
  display.display();
}

String htmlIndex() {
  String h;
  h.reserve(4000);
  h += F(
"<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
"<style>body{font-family:sans-serif;max-width:720px;margin:24px auto}textarea{width:100%;height:80px}"
".msg-list{max-height:300px;overflow-y:auto;border:1px solid #ccc;padding:8px;margin:12px 0}"
".msg-item{padding:4px 0;font-family:monospace;font-size:13px}"
".ack{color:green}.noack{color:orange}.fail{color:red}"
"button{padding:12px 24px;font-size:16px;cursor:pointer;background:#007bff;color:white;border:none;border-radius:4px;margin-top:8px}"
"button:hover{background:#0056b3}"
"</style>"
"<h2>LoRa Sender v2</h2>"
"<form id=f>"
"<textarea id=msg name=msg maxlength=50 placeholder='Type message here...'></textarea><br>"
"<button type=button onclick='sendMsg()'>Send Message</button>"
"</form>"
"<div id=status style='margin:12px 0;padding:8px;background:#f0f0f0'></div>"
"<h3>Message History</h3>"
"<div class=msg-list id=msgList>");

  for (int i = msgHistoryCount - 1; i >= 0; i--) {
    MessageLog& m = msgHistory[i];
    String cssClass = m.acked ? "ack" : (m.sent ? "noack" : "fail");
    String icon = m.acked ? "&#x2713;" : (m.sent ? "&#x231B;" : "&#x2717;");
    h += "<div class='msg-item " + cssClass + "'>" + icon + " " + m.msg + "</div>";
  }
  
  if (msgHistoryCount == 0) {
    h += "<div style='color:#999'>No messages sent yet</div>";
  }
  
  h += F("</div>"
"<script>"
"const t=document.getElementById('msg');"
"const status=document.getElementById('status');"
"t.focus();"
"t.addEventListener('keydown',e=>{"
"  if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();sendMsg();}"
"});"
"async function sendMsg(){"
"  const msg=t.value.trim();"
"  if(!msg){status.textContent='Enter a message first';return;}"
"  const btn=document.querySelector('button');"
"  btn.textContent='Sending...';"
"  btn.disabled=true;"
"  t.disabled=true;"
"  status.textContent='Transmitting...';"
"  try{"
"    const resp=await fetch('/send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
"      body:'msg='+encodeURIComponent(msg)});"
"    const data=await resp.json();"
"    if(data.sent){"
"      status.innerHTML=data.acked?'<b style=color:green>Sent + ACK received!</b>':'<b style=color:orange>Sent but no ACK</b>';"
"      t.value='';"
"      location.reload();"
"    }else{"
"      status.innerHTML='<b style=color:red>TX Failed: '+data.txState+'</b>';"
"    }"
"  }catch(e){status.textContent='Error: '+e;}"
"  finally{"
"    btn.textContent='Send Message';"
"    btn.disabled=false;"
"    t.disabled=false;"
"    t.focus();"
"  }"
"}"
"</script>"
"<h3>Stats</h3><table border=1 cellpadding=6>"
"<tr><td>Sent</td><td>"); h += String(pktSent);
  h += F("</td></tr><tr><td>Acked</td><td>"); h += String(pktAck);
  h += F("</td></tr><tr><td>Last TX</td><td>"); h += String(lastTxState);
  h += F("</td></tr><tr><td>RTT</td><td>"); h += String(lastRTTms);
  h += F(" ms</td></tr></table>");
  return h;
}

void handleRoot() { server.send(200, "text/html", htmlIndex()); }

void handleStatus() {
  String json = "{\"sent\":" + String(pktSent) + ",\"acked\":" + String(pktAck) + "}";
  server.send(200, "application/json", json);
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
  radio.setOutputPower(TX_POWER_DBM);
  radio.standby();
  
  Serial.println("Radio initialized:");
  Serial.print("  Freq: "); Serial.print(FREQ_MHZ); Serial.println(" MHz");
  Serial.print("  SF: "); Serial.println(SF);
  Serial.print("  BW: "); Serial.print(BW_KHZ); Serial.println(" kHz");
  Serial.print("  CR: 4/"); Serial.println(CR);
  Serial.print("  Sync: 0x"); Serial.println(SYNC_WORD, HEX);
  Serial.print("  Preamble: "); Serial.println(PREAMBLE_LEN);
  Serial.print("  TX Power: "); Serial.print(TX_POWER_DBM); Serial.println(" dBm");
}

void handleSend() {
  if (!server.hasArg("msg")) { 
    server.send(400, "application/json", "{\"error\":\"missing msg\"}"); 
    return; 
  }
  
  String msg = server.arg("msg"); 
  msg.trim();
  if (msg.isEmpty()) { 
    server.send(400, "application/json", "{\"error\":\"empty\"}"); 
    return; 
  }
  if (msg.length() > 50) msg = msg.substring(0, 50);

  Serial.println("\n================");
  Serial.print("Sending: '"); Serial.print(msg); Serial.println("'");
  Serial.print("Length: "); Serial.println(msg.length());
  
  // Print hex dump
  Serial.print("Hex: ");
  for (unsigned int i = 0; i < msg.length(); i++) {
    Serial.print((uint8_t)msg[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  
  // Reset radio state
  radio.standby();
  delay(50);
  
  pktSent++;
  uint32_t t0 = millis();
  
  int st = radio.transmit(msg);
  Serial.print("TX result: "); Serial.println(st);
  
  bool ack = false;
  if (st == RADIOLIB_ERR_NONE) {
    Serial.println("TX OK, waiting for ACK...");
    
    // Wait for ACK
    radio.standby();
    delay(100);  // Give RX time to process and send ACK
    
    String ackMsg;
    int rxState = radio.receive(ackMsg, 3000);  // 3 second timeout
    
    Serial.print("RX result: "); Serial.println(rxState);
    if (rxState == RADIOLIB_ERR_NONE) {
      Serial.print("Received: '"); Serial.print(ackMsg); Serial.println("'");
      
      // Check if it's an ACK
      if (ackMsg.startsWith("A,")) {
        int c1 = ackMsg.indexOf(',');
        int c2 = ackMsg.indexOf(',', c1 + 1);
        if (c1 > 0 && c2 > c1) {
          lastAckRSSI = ackMsg.substring(c1 + 1, c2).toInt();
          lastAckSNR = ackMsg.substring(c2 + 1).toFloat();
          ack = true;
          Serial.println("Valid ACK!");
          Serial.print("  RX RSSI: "); Serial.println(lastAckRSSI);
          Serial.print("  RX SNR: "); Serial.println(lastAckSNR);
        }
      }
    } else if (rxState == RADIOLIB_ERR_RX_TIMEOUT) {
      Serial.println("ACK timeout");
    } else {
      Serial.print("ACK receive error: "); Serial.println(rxState);
    }
  } else {
    Serial.print("TX FAILED: "); Serial.println(st);
  }
  
  lastRTTms = millis() - t0;
  lastTxState = st;
  if (ack) pktAck++;
  
  addToHistory(msg, (st == RADIOLIB_ERR_NONE), ack);
  
  String stat = (st == RADIOLIB_ERR_NONE) ? (ack ? "ACK" : "NoACK") : "FAIL";
  drawStatus(stat);
  
  // Send response
  String json = "{\"sent\":" + String(st == RADIOLIB_ERR_NONE ? "true" : "false") + 
                ",\"acked\":" + String(ack ? "true" : "false") +
                ",\"txState\":" + String(st) + "}";
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
  
  radio.standby();
  Serial.println("================\n");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== LoRa TX v2 Starting ===");

  VextON();
  delay(100);
  display.init();
  display.setContrast(200);
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "LoRa TX v2");
  display.drawString(0, 12, "Connecting WiFi...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP: "); Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/send", HTTP_POST, handleSend);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();
  
  initRadio();
  
  drawStatus("Ready");
  Serial.println("=== TX Ready ===");
  Serial.print("Web UI: http://"); Serial.println(WiFi.localIP());
  Serial.println();
}

void loop() {
  server.handleClient();
}
