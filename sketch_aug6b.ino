#include <SD.h>
#include <ModbusMaster.h>
#include <HardwareSerial.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// <<< CONFIGURATION SECTION >>>
#define MODE_AP  1
#define MODE_STA 2
#define WIFI_MODE MODE_STA

const char* STA_SSID = "LEOLEOLEO";
const char* STA_PASSWORD = "11111111";

// --- Pin Definitions ---
#define DE_RE_CTRL_PIN 4
#define RX_PIN 16
#define TX_PIN 17
#define SD_CS_PIN 21

// --- Modbus Settings ---
#define SLAVE_ID 3
#define FLOW_RATE_REG_ADDRESS 20
#define FLOW_RATE_REG_COUNT 1
#define RTC_REG_ADDRESS 30
#define RTC_REG_COUNT 6

  // --- Performance & Recording Settings ---
  #define MODBUS_BAUD_RATE 19200
  #define DATA_READ_INTERVAL_MS 5 //50, 30 , 25
  #define DATA_BUFFER_SIZE 100 //100, 170 , 200
  #define FLUSH_INTERVAL_MS 5000

// ★★★ NEW: State file for persistence ★★★
#define STATE_FILE "/state.json"

// --- Global Objects ---
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
ModbusMaster node;

struct DateTime {
  uint16_t year, month, day, hour, minute, second;
};

// --- Global State Flags & Timers ---
bool sdCardInitialized = false;
bool isRecording = false; // This will be loaded from STATE_FILE
unsigned long lastReadTime = 0;
uint16_t currentFlowRate = 0;
DateTime currentDateTime = {2025, 1, 1, 0, 0, 0};
unsigned long lastFlushTime = 0;
unsigned long recordingIndex = 0; // This will be loaded from STATE_FILE
String dataBuffer = "";
int bufferCount = 0;
String currentLogFilename = ""; // This will be loaded from STATE_FILE

bool shouldReadFlowRate = true;

// --- Function Prototypes ---
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void flushBufferToSD();
String listFiles();
void deleteFile(String path);
void downloadFile(String path);
String getDailyLogFilename();
String formatTimestamp(const DateTime& dt);
void saveState(bool recording, const String& filename); // ★★★ NEW ★★★
void loadState(); // ★★★ NEW ★★★
void sendStatusUpdate(uint8_t num = 255); // ★★★ NEW ★★★

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n--- ESP32 Modbus Data Logger (V6 - State Persistence) ---");
  
  // --- Init LittleFS first for state loading ---
  if (!LittleFS.begin(true)) { Serial.println("LittleFS Mount Failed"); return; }
  Serial.println("LittleFS Ready");

  // --- Init SD Card ---
  if (SD.begin(SD_CS_PIN)) {
    sdCardInitialized = true;
    dataBuffer.reserve(4096);
    Serial.println("SD Card Ready");
  } else {
    Serial.println("SD Card Mount Failed");
  }

  // ★★★ MODIFIED: Load previous state if available ★★★
  if (sdCardInitialized) {
    loadState();
  }

  // --- Init Modbus ---
  Serial2.begin(MODBUS_BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  pinMode(DE_RE_CTRL_PIN, OUTPUT);
  node.preTransmission([](){ digitalWrite(DE_RE_CTRL_PIN, HIGH); });
  node.postTransmission([](){ digitalWrite(DE_RE_CTRL_PIN, LOW); });
  node.begin(SLAVE_ID, Serial2);
  Serial.println("Modbus Master Ready");

  // --- WiFi Initialization ---
  WiFi.mode(WIFI_AP_STA);
  if (WIFI_MODE == MODE_AP) {
    WiFi.softAP("ESP32-AP", "12345678");
    Serial.print("AP IP address: "); Serial.println(WiFi.softAPIP());
  } else {
    WiFi.begin(STA_SSID, STA_PASSWORD);
    Serial.printf("Connecting to SSID: %s\n", STA_SSID);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts++ < 30) { delay(500); Serial.print("."); }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected!"); Serial.print("STA IP Address: "); Serial.println(WiFi.localIP());
    } else {
      Serial.println("\nFailed to connect. Falling back to AP mode.");
      WiFi.softAP("ESP32-AP", "12345678");
      Serial.print("AP IP address: "); Serial.println(WiFi.softAPIP());
    }
  }

  // --- Web Server Endpoints ---
  server.on("/", HTTP_GET, [&]() {
  File file = LittleFS.open("/main.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close(); // ★★★ แก้ไข: ย้าย file.close() มาไว้ในบล็อก if และก่อนที่ฟังก์ชันจะจบ
  } else {
    server.send(404, "text/plain", "main.html not found");
  }
});
server.on("/files", HTTP_GET, [&]() {
  File file = LittleFS.open("/files.html", "r");
  if (file) {
    server.streamFile(file, "text/html");
    file.close(); // ★★★ แก้ไข: ย้าย file.close() มาไว้ในบล็อก if และก่อนที่ฟังก์ชันจะจบ
  } else {
    server.send(404, "text/plain", "files.html not found");
  }
});

// ★★★ SIMPLE FIX: Replace your existing endpoints with these ★★★

// Replace the /download_file endpoint in setup() with this:
server.on("/download_file", HTTP_GET, [&]() {
  if (!server.hasArg("filename")) { 
    server.send(400, "text/plain", "No filename specified"); 
    return; 
  }
  
  if (!sdCardInitialized) { 
    server.send(503, "text/plain", "SD Card not available"); 
    return; 
  }
  
  String filename = server.arg("filename");
  Serial.println("Download request for: " + filename);
  
  flushBufferToSD();
  downloadFile(filename);
});

// Replace the /list_files endpoint in setup() with this:
server.on("/list_files", HTTP_GET, [&]() {
  if (!sdCardInitialized) { 
    server.send(503, "application/json", "{\"error\":\"SD Card not available\"}"); 
    return; 
  }
  flushBufferToSD();
  server.send(200, "application/json", listFiles());
});
server.on("/delete_file", HTTP_GET, [&]() {
  if (!sdCardInitialized || !server.hasArg("filename")) { 
    server.send(400, "text/plain", "SD Card not available or bad request"); 
    return; 
  }
  String filename = server.arg("filename");
  Serial.println("Delete request for: " + filename);
  deleteFile(filename);
  server.send(200, "text/plain", "Delete command sent.");
});
server.on("/download_file", HTTP_GET, [&]() {
  if (!server.hasArg("filename")) { 
    server.send(400, "text/plain", "No filename specified"); 
    return; 
  }
  
  if (!sdCardInitialized) { 
    server.send(503, "text/plain", "SD Card not available"); 
    return; 
  }
  
  String filename = server.arg("filename");
  Serial.println("Download request for: " + filename);
  
  flushBufferToSD();
  downloadFile(filename);
});
  
  server.begin();
  Serial.println("HTTP server started");

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started");
  lastFlushTime = millis();
}

void loop() {
  webSocket.loop();
  server.handleClient();

  if (millis() - lastReadTime > DATA_READ_INTERVAL_MS) {
    lastReadTime = millis();
    uint8_t result;

    if (shouldReadFlowRate) {
      result = node.readHoldingRegisters(FLOW_RATE_REG_ADDRESS, FLOW_RATE_REG_COUNT);
      if (result == node.ku8MBSuccess) currentFlowRate = node.getResponseBuffer(0);
    } else {
      result = node.readHoldingRegisters(RTC_REG_ADDRESS, RTC_REG_COUNT);
      if (result == node.ku8MBSuccess) {
        currentDateTime.year   = node.getResponseBuffer(0);
        currentDateTime.month  = node.getResponseBuffer(1);
        currentDateTime.day    = node.getResponseBuffer(2);
        currentDateTime.hour   = node.getResponseBuffer(3);
        currentDateTime.minute = node.getResponseBuffer(4);
        currentDateTime.second = node.getResponseBuffer(5);
        currentDateTime.second = node.getResponseBuffer(6); // <-- เพิ่มการอ่านวินาทีเข้ามา

      }
    }
    
    if (result == node.ku8MBSuccess) {
        JsonDocument doc;
        doc["flow"] = currentFlowRate;
        doc["ts"] = formatTimestamp(currentDateTime);
        String payload;
        serializeJson(doc, payload);
        webSocket.broadcastTXT(payload);
    }

    if (isRecording && sdCardInitialized) {
      String logLine = String(recordingIndex) + "," + String(currentFlowRate);
      dataBuffer += logLine + "\n";
      recordingIndex++;
      bufferCount++;
      if (bufferCount >= DATA_BUFFER_SIZE) flushBufferToSD();
    }
    
    shouldReadFlowRate = !shouldReadFlowRate;
  }

  if (isRecording && sdCardInitialized && bufferCount > 0 && (millis() - lastFlushTime > FLUSH_INTERVAL_MS)) {
      flushBufferToSD();
  }

  delay(5);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] Connected from %s\n", num, ip.toString().c_str());
      sendStatusUpdate(num); // ★★★ MODIFIED: Send current status on connect
      break;
    }
    // case WStype_TEXT: ในฟังก์ชัน webSocketEvent (ฉบับแก้ไข)
case WStype_TEXT: 
  String command = String((char*)payload);
  Serial.printf("[%u] Received command: %s\n", num, command.c_str());

  if (command == "START_REC" && !isRecording) {
    // 1. ตรวจสอบว่า SD Card พร้อมใช้งานหรือไม่
    if (!sdCardInitialized) {
      Serial.println("Cannot start recording: SD card not available.");
      // ส่งสถานะแจ้งเตือนกลับไปที่หน้าเว็บ (แนะนำ)
      JsonDocument doc;
      doc["status"] = "IDLE";
      doc["message"] = "Error: SD Card not available!";
      String payload;
      serializeJson(doc, payload);
      webSocket.sendTXT(num, payload);
      break; // ออกจากการทำงาน
    }

    // 2. ตั้งค่าสถานะการบันทึกเป็น "กำลังบันทึก"
    isRecording = true;
    
    // 3. สร้างชื่อไฟล์ใหม่สำหรับ Log ของเซสชันนี้
    currentLogFilename = getDailyLogFilename();
    recordingIndex = 1; // เริ่มนับ Index ใหม่สำหรับไฟล์ใหม่
    dataBuffer = "";    // เคลียร์บัฟเฟอร์ข้อมูลเก่า
    bufferCount = 0;    // รีเซ็ตตัวนับบัฟเฟอร์

    Serial.println("Starting new recording session to: " + currentLogFilename);

    // 4. (แนะนำ) เขียน Header เริ่มต้นให้กับไฟล์ใหม่
    File dataFile = SD.open(currentLogFilename, FILE_WRITE);
    if (dataFile) {
        dataFile.println("Index,FlowRate,Timestamp"); // เพิ่ม Timestamp เข้าไปใน Header ด้วย
        dataFile.println("--- SESSION START: " + formatTimestamp(currentDateTime) + " ---");
        dataFile.close();
    }
    
    // 5. บันทึกสถานะใหม่ (กำลังบันทึก, ชื่อไฟล์) ลงใน LittleFS
    saveState(true, currentLogFilename);
    
    // 6. (สำคัญที่สุด) ส่งสถานะอัปเดตไปยังหน้าเว็บทั้งหมด
    sendStatusUpdate();

  } else if (command == "STOP_REC" && isRecording) {
    isRecording = false;
    flushBufferToSD(); // เขียนข้อมูลที่ค้างในบัฟเฟอร์ลงไฟล์ก่อน
    File dataFile = SD.open(currentLogFilename, FILE_APPEND);
    if (dataFile) {
        dataFile.println("--- SESSION STOP: " + formatTimestamp(currentDateTime) + " ---");
        dataFile.close();
    }
    saveState(false, ""); // บันทึกสถานะ "หยุด"
    sendStatusUpdate();   // ส่งสถานะอัปเดต
  }
  break;
}
}

// ★★★ NEW FUNCTION ★★★
// Sends a JSON status update to the client(s).
void sendStatusUpdate(uint8_t num) {
  JsonDocument doc;
  String message;
  
  if (isRecording) {
    doc["status"] = "RECORDING";
    message = "Recording to " + currentLogFilename;
  } else {
    // Check if a log file exists to determine if the last state was STOPPED or truly IDLE
    if (currentLogFilename != "") {
      doc["status"] = "STOPPED";
      message = "Last recording stopped. Ready to start a new session.";
    } else {
      doc["status"] = "IDLE";
      message = "System is idle. Ready to record.";
    }
  }
  doc["message"] = message;
  
  String payload;
  serializeJson(doc, payload);
  
  if (num == 255) { // 255 is the default, means broadcast
    webSocket.broadcastTXT(payload);
    Serial.println("Broadcasting status update: " + payload);
  } else { // Send to a specific client
    webSocket.sendTXT(num, payload);
    Serial.println("Sending status update to client " + String(num) + ": " + payload);
  }
}

// ★★★ NEW FUNCTION ★★★
// Saves the current recording state to a file on LittleFS.
void saveState(bool recording, const String& filename) {
  JsonDocument doc;
  doc["isRecording"] = recording;
  doc["logFile"] = filename;
  doc["recIndex"] = recordingIndex; // ★★★ เพิ่ม: บันทึก Index ปัจจุบัน

  File stateFile = LittleFS.open(STATE_FILE, "w");
  if (!stateFile) {
    Serial.println("Failed to open state file for writing");
    return;
  }
  serializeJson(doc, stateFile);
  stateFile.close();
  Serial.println("System state saved.");
}

// ★★★ NEW FUNCTION ★★★
// Loads the recording state from LittleFS on boot.
void loadState() {
  if (LittleFS.exists(STATE_FILE)) {
    File stateFile = LittleFS.open(STATE_FILE, "r");
    if (stateFile) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, stateFile);
      if (error) {
        Serial.println("Failed to parse state file, assuming IDLE.");
        isRecording = false;
      } else {
        isRecording = doc["isRecording"] | false;
        if (isRecording) {
          currentLogFilename = doc["logFile"].as<String>();
          recordingIndex = doc["recIndex"] | 1; // ★★★ เพิ่ม: โหลด Index ล่าสุด (ถ้าไม่มีให้เริ่มที่ 1)

          Serial.println("State loaded: System was RECORDING.");
          Serial.println("Resuming recording to file: " + currentLogFilename);
          
          // Add a separator to indicate a reboot happened during recording
          File dataFile = SD.open(currentLogFilename, FILE_APPEND);
          if (dataFile) {
            dataFile.println("--- REBOOT DETECTED: RESUMING SESSION ---");
            dataFile.close();
          }
        } else {
          Serial.println("State loaded: System was IDLE/STOPPED.");
        }
      }
      stateFile.close();
    }
  } else {
    Serial.println("No state file found, starting fresh.");
    isRecording = false;
  }
}

// --- Other functions (flushBufferToSD, listFiles, etc.) ---
// (These functions remain the same as your last version)
void flushBufferToSD() {
    if (bufferCount == 0 || !sdCardInitialized) return;

    File dataFile = SD.open(currentLogFilename, FILE_APPEND);
    if (dataFile) {
        dataFile.print(dataBuffer);
        dataFile.close();
        // เพิ่ม Log เพื่อให้เห็นว่ามีการเขียนข้อมูลและ Index ปัจจุบันคือเท่าไหร่
        Serial.printf("Flushed %d records to SD. Current index: %lu\n", bufferCount, recordingIndex);
    }

    dataBuffer = "";
    bufferCount = 0;
    lastFlushTime = millis();

    // ★★★ จุดที่แก้ไขสำคัญ ★★★
    // บันทึกสถานะ (isRecording, ชื่อไฟล์, และ Index ล่าสุด) ลง LittleFS
    // ทุกครั้งที่มีการเขียนข้อมูลลง SD Card
    if (isRecording) {
        saveState(true, currentLogFilename);
    }
}
String listFiles() {
  String fileList = "["; File root = SD.open("/"); if (!root) return "[]";
  File file = root.openNextFile(); bool firstFile = true;
  while (file) {
    if (!file.isDirectory() && String(file.name()).endsWith(".csv")) {
      if (!firstFile) fileList += ",";
      fileList += "\"" + String(file.name()) + "\""; firstFile = false;
    }
    file.close(); file = root.openNextFile();
  }
  root.close(); fileList += "]"; return fileList;
}
void deleteFile(String path) {
  // Normalize path - add leading slash if missing
  if (path.length() > 0 && !path.startsWith("/")) {
    path = "/" + path;
  }
  
  Serial.println("Attempting to delete: " + path);
  
  if (SD.remove(path.c_str())) {
    Serial.println("File deleted successfully: " + path);
  } else {
    Serial.println("Delete failed: " + path);
  }
}
void downloadFile(String path) {
  // Normalize path - add leading slash if missing
  if (path.length() > 0 && !path.startsWith("/")) {
    path = "/" + path;
  }
  
  Serial.println("Attempting to download: " + path);
  
  File file = SD.open(path.c_str(), "r");
  if (!file) { 
    Serial.println("File not found: " + path);
    server.send(404, "text/plain", "File not found on SD Card"); 
    return; 
  }
  
  // Get file size
  size_t fileSize = file.size();
  Serial.println("File size: " + String(fileSize) + " bytes");
  
  if (fileSize == 0) {
    Serial.println("File is empty");
    file.close();
    server.send(400, "text/plain", "File is empty");
    return;
  }
  
  // Set headers
  String filename = path.substring(1); // Remove leading slash for filename
  server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server.sendHeader("Content-Length", String(fileSize));
  server.sendHeader("Content-Type", "text/csv");
  
  // Stream the file
  server.streamFile(file, "text/csv");
  file.close();
  
  Serial.println("Download completed: " + path);
}
String formatTimestamp(const DateTime& dt) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    return String(buf);
}
String getDailyLogFilename() {
    uint8_t result = node.readHoldingRegisters(RTC_REG_ADDRESS, RTC_REG_COUNT);
    if (result == node.ku8MBSuccess) {
        DateTime dt;
        dt.year = node.getResponseBuffer(0); dt.month = node.getResponseBuffer(1); dt.day = node.getResponseBuffer(2);
        dt.minute = node.getResponseBuffer(4); dt.hour = node.getResponseBuffer(3);
        char filename[25];
        snprintf(filename, sizeof(filename), "/log_%02d%02d%04d-%02d-%02d.csv", dt.day, dt.month, dt.year , dt.hour, dt.minute);
        return String(filename);
    } else {
        return "/log_no_rtc.csv";
    }
}