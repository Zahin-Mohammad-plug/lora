// ===== TX: Heltec WiFi LoRa 32 V3 (ESP32-S3 + SX1262) =====
// v2.1 - Enhanced web UI with RX preview and wrap text option
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
const uint8_t SYNC_WORD  = 0x12;
const int   PREAMBLE_LEN = 8;
const int   TX_POWER_DBM = 17;
const int   MAX_MSG_LEN  = 50;

// Metrics
uint32_t pktSent = 0, pktAck = 0;
int lastAckRSSI = 0;
float lastAckSNR = 0.0;
uint32_t lastRTTms = 0;
int lastTxState = 0;
int rxReportedMsgCount = 0;  // From RX ACK
int rxReportedLineCount = 0;  // From RX ACK

// RX Screen simulation - match RX structure
const int MAX_RX_MESSAGES = 50;
String rxMessages[MAX_RX_MESSAGES];
int rxMessageCount = 0;
bool rxWrapMode = true;

// Message history
struct MessageLog {
  String msg;
  bool acked;
  bool sent;
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
  msgHistoryCount++;
}

void updateRxScreen(const String& msg) {
  // Add message to array (newest at end, like RX)
  if (rxMessageCount >= MAX_RX_MESSAGES) {
    // Shift messages
    for (int i = 0; i < MAX_RX_MESSAGES - 1; i++) {
      rxMessages[i] = rxMessages[i + 1];
    }
    rxMessageCount = MAX_RX_MESSAGES - 1;
  }
  rxMessages[rxMessageCount++] = msg;
}

void drawStatus(const String& stat) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "TX v2.1 @ 915 MHz");
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

// Escape string for JSON
String escapeJson(const String& s) {
  String out = "";
  for (unsigned int i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "";
    else if (c >= 32 && c <= 126) out += c;
  }
  return out;
}

String htmlIndex() {
  String h;
  h.reserve(6000);
  h += F(R"(<!doctype html>
<html><head>
<meta name=viewport content='width=device-width,initial-scale=1'>
<style>
body{font-family:system-ui,-apple-system,sans-serif;max-width:800px;margin:0 auto;padding:16px;background:#1a1a2e;color:#eee}
h2,h3{color:#0ff;margin-top:24px}
textarea{width:100%;height:100px;font-family:monospace;font-size:14px;padding:8px;border:2px solid #333;border-radius:4px;background:#0d0d1a;color:#0f0;resize:vertical}
.controls{display:flex;gap:12px;align-items:center;margin:12px 0;flex-wrap:wrap}
button{padding:12px 24px;font-size:16px;cursor:pointer;background:#007bff;color:white;border:none;border-radius:4px}
button:hover{background:#0056b3}
button:disabled{background:#666;cursor:not-allowed}
label{display:flex;align-items:center;gap:6px;cursor:pointer}
input[type=checkbox]{width:18px;height:18px;cursor:pointer}
.rx-preview{background:#000;border:3px solid #333;border-radius:4px;padding:8px;margin:12px 0;font-family:monospace;font-size:11px;color:#0f0;max-height:300px;overflow-y:auto;line-height:1.3;white-space:pre-wrap;word-wrap:break-word}
.rx-preview.nowrap{white-space:pre;overflow-x:auto}
.status{padding:12px;margin:12px 0;border-radius:4px;font-weight:bold}
.status.ok{background:#1a4d1a;color:#4f4}
.status.warn{background:#4d4d1a;color:#ff0}
.status.err{background:#4d1a1a;color:#f44}
.msg-list{max-height:250px;overflow-y:auto;border:1px solid #333;border-radius:4px;padding:8px;background:#0d0d1a}
.msg-item{padding:4px 8px;font-family:monospace;font-size:13px;border-bottom:1px solid #222}
.msg-item:last-child{border-bottom:none}
.ack{color:#4f4}.noack{color:#fa0}.fail{color:#f44}
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:8px}
.stat{background:#0d0d1a;padding:12px;border-radius:4px;border:1px solid #333}
.stat-label{font-size:12px;color:#888}
.stat-value{font-size:20px;font-weight:bold;color:#0ff}
.char-count{font-size:12px;color:#888;text-align:right}
</style>
</head><body>
<h2>LoRa Sender v2.1</h2>

<textarea id=msg placeholder='Type or paste message here... (max 50 chars per send)'></textarea>
<div class=char-count><span id=charCount>0</span>/50 chars (will send first 50)</div>

<div class=controls>
  <button id=sendBtn onclick='sendMsg()'>Send Message</button>
  <label><input type=checkbox id=wrapMode checked onchange='updatePreview()'> Wrap text on RX</label>
  <label><input type=checkbox id=autoSend> Auto-send remaining</label>
</div>

<div id=status class='status ok'>Ready to send</div>

<h3>RX Screen Preview (128x64 OLED)</h3>
<div id=rxPreview class=rx-preview>RX Ready</div>

<h3>Stats</h3>
<div class=stats>
  <div class=stat><div class=stat-label>Sent</div><div class=stat-value id=statSent>)");
  h += String(pktSent);
  h += F(R"(</div></div>
  <div class=stat><div class=stat-label>Acked</div><div class=stat-value id=statAck>)");
  h += String(pktAck);
  h += F(R"(</div></div>
  <div class=stat><div class=stat-label>RX Signal</div><div class=stat-value id=statRSSI>)");
  h += String(lastAckRSSI);
  h += F(R"( dBm</div></div>
  <div class=stat><div class=stat-label>Last RTT</div><div class=stat-value id=statRTT>)");
  h += String(lastRTTms);
  h += F(R"( ms</div></div>
</div>

<h3>Message History</h3>
<div class=msg-list id=msgList>)");

  for (int i = msgHistoryCount - 1; i >= 0; i--) {
    MessageLog& m = msgHistory[i];
    String cssClass = m.acked ? "ack" : (m.sent ? "noack" : "fail");
    const char* icon = m.acked ? "[OK]" : (m.sent ? "[..]" : "[X]");
    h += "<div class='msg-item " + cssClass + "'>" + String(icon) + " " + m.msg + "</div>";
  }
  if (msgHistoryCount == 0) {
    h += "<div style='color:#666'>No messages sent yet</div>";
  }
  
  h += F(R"(</div>

<script>
const MAX_LEN = 50;
const msgBox = document.getElementById('msg');
const charCount = document.getElementById('charCount');
const statusDiv = document.getElementById('status');
const rxPreview = document.getElementById('rxPreview');
const wrapMode = document.getElementById('wrapMode');
const autoSend = document.getElementById('autoSend');
const sendBtn = document.getElementById('sendBtn');

let rxMessages = [)");
  // Build JSON array of messages
  h += "\"";
  for (int i = 0; i < rxMessageCount; i++) {
    if (i > 0) h += "\",\"";
    h += escapeJson(rxMessages[i]);
  }
  h += F(R"("];
let rxMsgCount = 0;  // RX's reported message count
let rxLineCount = 0;  // RX's reported line count

msgBox.focus();
msgBox.addEventListener('input', () => {
  charCount.textContent = msgBox.value.length;
  updatePreview();
});
msgBox.addEventListener('keydown', e => {
  if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); sendMsg(); }
});

function updatePreview() {
  // RX always uses wrap mode (WRAP_TEXT=true), so preview should always wrap
  rxPreview.className = 'rx-preview';
  const MAX_CHARS = 20;  // 128px OLED = ~20 chars per line
  const MAX_VISIBLE = 5; // 5 lines visible (64px / 10px font = 5-6 lines)
  
  // Build lines from messages (newest first, like RX)
  let allLines = [];
  
  // Process messages in REVERSE order (newest first)
  let messagesToShow = [...rxMessages];
  if (msgBox.value.trim()) {
    messagesToShow.push(msgBox.value.trim().substring(0, MAX_LEN));
  }
  
  // RX always uses wrap mode, so preview should always wrap (ignore checkbox)
  for (let m = messagesToShow.length - 1; m >= 0; m--) {
    let msg = messagesToShow[m];
    
    // Always use wrap mode to match RX behavior
    {
      // Wrap mode: Try to append to last line if it fits, otherwise wrap
      if (allLines.length > 0) {
        // Try to append to last line
        let candidate = allLines[allLines.length - 1] + ' ' + msg;
        if (candidate.length <= MAX_CHARS) {
          // Fits on last line!
          allLines[allLines.length - 1] = candidate;
          continue;
        }
      }
      // Doesn't fit or no previous line - word wrap this message
      let pos = 0;
      while (pos < msg.length && allLines.length < 100) {
        let end = Math.min(pos + MAX_CHARS, msg.length);
        // Find last space if not at end
        if (end < msg.length && msg[end] !== ' ') {
          let lastSpace = msg.lastIndexOf(' ', end);
          if (lastSpace > pos) end = lastSpace;
        }
        let line = msg.substring(pos, end).trim();
        if (line) allLines.push(line);
        pos = end;
        if (pos < msg.length && msg[pos] === ' ') pos++;
      }
    }
  }
  
  // Add status line at the end (like RX does)
  let msgCount = rxMsgCount > 0 ? rxMsgCount : (rxMessages.length + (msgBox.value.trim() ? 1 : 0));
  allLines.push('Msgs:' + msgCount + ' RSSI:-- Scr:0');
  
  // Use RX's reported line count if available, otherwise use calculated
  let totalLines = rxLineCount > 0 ? rxLineCount : allLines.length;
  
  // Limit to actual lines (not all 50) - but show all existing lines
  if (allLines.length < totalLines) {
    // Pad with empty lines if RX reports more (shouldn't happen, but handle it)
    while (allLines.length < totalLines && allLines.length < 50) {
      allLines.push('');
    }
  } else if (allLines.length > totalLines && totalLines > 0) {
    // Trim to RX's reported count
    allLines = allLines.slice(0, totalLines);
  }
  
  // Show ALL lines (up to 50) with color coding
  // Green for visible (first 5 - newest at top), yellow for older (scrollable)
  let html = '';
  // Newest lines are at index 0, so visible lines are 0 to MAX_VISIBLE-1
  let visibleEnd = Math.min(MAX_VISIBLE, allLines.length);
  
  for (let i = 0; i < allLines.length && i < 50; i++) {
    let line = allLines[i];
    let isVisible = i < visibleEnd;  // First 5 lines are visible (newest)
    let color = isVisible ? '#0f0' : '#ff0';  // Green for visible, yellow for older
    // Escape HTML
    let escaped = line.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    html += '<span style=\"color:' + color + '\">' + escaped + '</span>';
    if (i < allLines.length - 1 && i < 49) html += '<br>';
  }
  
  rxPreview.innerHTML = html;
}

async function sendMsg() {
  let fullText = msgBox.value.trim();
  if (!fullText) { setStatus('Enter a message first', 'warn'); return; }
  
  // Take first MAX_LEN chars
  let toSend = fullText.substring(0, MAX_LEN);
  let remaining = fullText.substring(MAX_LEN);
  
  sendBtn.disabled = true;
  sendBtn.textContent = 'Sending...';
  setStatus('Transmitting...', 'warn');
  
  try {
    const resp = await fetch('/send', {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: 'msg=' + encodeURIComponent(toSend) + '&wrap=' + (wrapMode.checked ? '1' : '0')
    });
    const data = await resp.json();
    
    if (data.sent) {
      // Always add message to local array (optimistic update)
      if (!rxMessages.includes(toSend)) {
        rxMessages.push(toSend);
      }
      
      if (data.acked) {
        setStatus('✓ Sent + ACK received!', 'ok');
        // Update from RX's reported data
        if (data.rxMsgCount !== undefined) {
          rxMsgCount = data.rxMsgCount;
        }
        if (data.rxLineCount !== undefined) {
          rxLineCount = data.rxLineCount;
        }
        // Sync with RX's reported message array if provided
        if (data.rxMessages && Array.isArray(data.rxMessages) && data.rxMessages.length > 0) {
          // Use RX's message array (most accurate - reflects what RX actually has)
          rxMessages = data.rxMessages;
          // Trim to RX's reported count if needed
          if (rxMsgCount > 0 && rxMessages.length > rxMsgCount) {
            rxMessages = rxMessages.slice(-rxMsgCount);
          }
        } else {
          // No server array provided - trim local array to RX count if known
          if (rxMsgCount > 0 && rxMessages.length > rxMsgCount) {
            rxMessages = rxMessages.slice(-rxMsgCount);
          }
        }
      } else {
        setStatus('⏳ Sent but no ACK (RX may have received it)', 'warn');
      }
      
      // Update stats
      document.getElementById('statSent').textContent = data.pktSent || '-';
      document.getElementById('statAck').textContent = data.pktAck || '-';
      document.getElementById('statRSSI').textContent = (data.rssi || 0) + ' dBm';
      document.getElementById('statRTT').textContent = (data.rtt || 0) + ' ms';
      
      // Update textbox with remaining text
      msgBox.value = remaining;
      charCount.textContent = remaining.length;
      updatePreview();
      
      // Refresh history
      refreshHistory();
      
      // Auto-send remaining if enabled and there's more text
      if (autoSend.checked && remaining.length > 0) {
        setTimeout(sendMsg, 500);
      }
    } else {
      setStatus('✗ TX Failed: ' + data.txState, 'err');
    }
  } catch(e) {
    setStatus('Error: ' + e, 'err');
  } finally {
    sendBtn.disabled = false;
    sendBtn.textContent = 'Send Message';
    msgBox.focus();
  }
}

function setStatus(text, type) {
  statusDiv.textContent = text;
  statusDiv.className = 'status ' + type;
}

async function refreshHistory() {
  try {
    const resp = await fetch('/history');
    if (resp.ok) {
      document.getElementById('msgList').innerHTML = await resp.text();
    }
  } catch(e) {}
}

updatePreview();
</script>
</body></html>)");
  return h;
}

void handleRoot() { server.send(200, "text/html", htmlIndex()); }

void handleHistory() {
  String h = "";
  for (int i = msgHistoryCount - 1; i >= 0; i--) {
    MessageLog& m = msgHistory[i];
    String cssClass = m.acked ? "ack" : (m.sent ? "noack" : "fail");
    String icon = m.acked ? "✓" : (m.sent ? "⏳" : "✗");
    h += "<div class='msg-item " + cssClass + "'>" + icon + " " + m.msg + "</div>";
  }
  if (msgHistoryCount == 0) {
    h += "<div style='color:#666'>No messages sent yet</div>";
  }
  server.send(200, "text/html", h);
}

void handleStatus() {
  String json = "{\"sent\":" + String(pktSent) + 
                ",\"acked\":" + String(pktAck) + 
                ",\"rssi\":" + String(lastAckRSSI) +
                ",\"rtt\":" + String(lastRTTms) + "}";
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
  
  Serial.println("Radio initialized");
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
  if (msg.length() > MAX_MSG_LEN) msg = msg.substring(0, MAX_MSG_LEN);
  
  // Get wrap mode preference
  if (server.hasArg("wrap")) {
    rxWrapMode = server.arg("wrap") == "1";
  }

  Serial.println("\n================");
  Serial.print("Sending: '"); Serial.print(msg); Serial.println("'");
  
  radio.standby();
  delay(50);
  
  pktSent++;
  uint32_t t0 = millis();
  
  int st = radio.transmit(msg);
  Serial.print("TX result: "); Serial.println(st);
  
  bool ack = false;
  if (st == RADIOLIB_ERR_NONE) {
    Serial.println("TX OK, waiting for ACK...");
    
    radio.standby();
    delay(30);
    
    uint8_t ackBuffer[32];
    memset(ackBuffer, 0, sizeof(ackBuffer));
    int rxState = radio.receive(ackBuffer, 32);
    
    if (rxState == RADIOLIB_ERR_NONE) {
      size_t ackLen = radio.getPacketLength();
      String ackMsg = "";
      for (size_t i = 0; i < ackLen && i < 31; i++) {
        ackMsg += (char)ackBuffer[i];
      }
      
      Serial.print("Received: '"); Serial.print(ackMsg); Serial.println("'");
      
      if (ackMsg.startsWith("A,")) {
        // Parse enhanced ACK: "A,RSSI,SNR,MSG_COUNT,LINE_COUNT"
        int c1 = ackMsg.indexOf(',');
        int c2 = ackMsg.indexOf(',', c1 + 1);
        int c3 = ackMsg.indexOf(',', c2 + 1);
        int c4 = ackMsg.indexOf(',', c3 + 1);
        
        if (c1 > 0 && c2 > c1) {
          lastAckRSSI = ackMsg.substring(c1 + 1, c2).toInt();
          if (c3 > c2) {
            lastAckSNR = ackMsg.substring(c2 + 1, c3).toFloat();
            if (c4 > c3) {
              // Enhanced ACK with counts
              rxReportedMsgCount = ackMsg.substring(c3 + 1, c4).toInt();
              rxReportedLineCount = ackMsg.substring(c4 + 1).toInt();
              Serial.print("Enhanced ACK: msgs="); Serial.print(rxReportedMsgCount);
              Serial.print(" lines="); Serial.println(rxReportedLineCount);
            } else {
              // Old format - just RSSI and SNR
              lastAckSNR = ackMsg.substring(c2 + 1).toFloat();
              rxReportedMsgCount = 0;
              rxReportedLineCount = 0;
            }
          } else {
            // Fallback for old format
            lastAckSNR = ackMsg.substring(c2 + 1).toFloat();
            rxReportedMsgCount = 0;
            rxReportedLineCount = 0;
          }
          ack = true;
          Serial.println("Valid ACK!");
        }
      }
    } else {
      Serial.println("ACK timeout or error");
    }
  }
  
  lastRTTms = millis() - t0;
  lastTxState = st;
  if (ack) pktAck++;

  addToHistory(msg, (st == RADIOLIB_ERR_NONE), ack);
  
  // Update RX screen simulation
  if (st == RADIOLIB_ERR_NONE) {
    updateRxScreen(msg);
  }
  
  String stat = (st == RADIOLIB_ERR_NONE) ? (ack ? "ACK" : "NoACK") : "FAIL";
  drawStatus(stat);
  
  // Build JSON array of messages
  String rxMessagesJson = "[";
  for (int i = 0; i < rxMessageCount; i++) {
    if (i > 0) rxMessagesJson += ",";
    rxMessagesJson += "\"" + escapeJson(rxMessages[i]) + "\"";
  }
  rxMessagesJson += "]";
  
  // Send response with all data
  String json = "{\"sent\":" + String(st == RADIOLIB_ERR_NONE ? "true" : "false") + 
                ",\"acked\":" + String(ack ? "true" : "false") +
                ",\"txState\":" + String(st) +
                ",\"pktSent\":" + String(pktSent) +
                ",\"pktAck\":" + String(pktAck) +
                ",\"rssi\":" + String(lastAckRSSI) +
                ",\"rtt\":" + String(lastRTTms) +
                ",\"rxMsgCount\":" + String(rxReportedMsgCount) +
                ",\"rxLineCount\":" + String(rxReportedLineCount) +
                ",\"rxMessages\":" + rxMessagesJson + "}";
  
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
  
  radio.standby();
  Serial.println("================\n");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== LoRa TX v2.1 Starting ===");

  VextON();
  delay(100);
  display.init();
  display.setContrast(200);
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 0, "LoRa TX v2.1");
  display.drawString(0, 12, "Connecting WiFi...");
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP: "); Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/send", HTTP_POST, handleSend);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/history", HTTP_GET, handleHistory);
  server.begin();
  
  initRadio();
  
  drawStatus("Ready");
  Serial.println("=== TX Ready ===");
  Serial.print("Web UI: http://"); Serial.println(WiFi.localIP());
}

void loop() {
  server.handleClient();
}
