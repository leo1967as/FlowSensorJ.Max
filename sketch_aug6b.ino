#include <SD.h>
#include <ModbusMaster.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
// #include <ArduinoJson.h> // Commented out - Not actively used
#include "database.h"
#include "robust_logger.h"

// --- Pin Definitions ---
#define DE_RE_CTRL_PIN 4
#define RX_PIN 16
#define TX_PIN 17
#define SD_CS_PIN 21 // << ใช้ขา 21 ที่ปลอดภัย (หรือเปลี่ยนเป็นขาอื่นที่ปลอดภัย)

// --- Modbus Settings ---
#define SLAVE_ID 3
#define REG_ADDRESS 20
#define REG_COUNT 1

// --- Performance & Recording Settings ---
#define MODBUS_BAUD_RATE 19200   // ความเร็วในการสื่อสาร Modbus
#define DATA_READ_INTERVAL_MS 50 // อ่านค่าจาก Modbus 20 ครั้งต่อวินาที
#define FLUSH_INTERVAL_MS 5000   // หรือเขียนลงการ์ดทุกๆ 5 วินาที

// --- Global Objects ---
// --- Global Robust Logger Instance ---
RobustLogger robustLogger;

// --- Legacy Variables (for compatibility) ---
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
ModbusMaster node;

// --- Global State Flags & Timers ---
bool sdCardInitialized = false;
bool isRecording = false; // สถานะการบันทึก ควบคุมโดย WebSocket
unsigned long lastReadTime = 0;
uint16_t currentFlowRate = 0;
unsigned long lastFlushTime = 0;

// --- SD Card Data Buffering (Legacy) ---
// String dataBuffer = "";
// int bufferCount = 0;
// String currentLogFilename = "";

// --- Memory Database ---
MemoryDatabase db("/sensor_data.db", true);


// --- Function Prototypes ---
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
// void flushBufferToSD(); // Legacy function
// String listFiles(); // Legacy function
// void deleteFile(String path); // Legacy function
// void downloadFile(String path); // Legacy function
// String createTimestampedFilename(); // Legacy function


void setup() {
  Serial.begin(115200);
  
  // --- Initialize Robust Logger System ---
  Serial.println("=== Initializing Robust Logger System ===");
  
  if (!robustLogger.initialize()) {
    Serial.println("ERROR: Failed to initialize Robust Logger System");
    while(1) {
      delay(1000);
    }
  }
  
  Serial.println("INFO: Robust Logger System initialized successfully");

  // --- Initialize Legacy Components (for compatibility) ---
  Serial.println("=== Initializing Legacy Components ===");
  
  // Modbus RTU
  Serial2.begin(MODBUS_BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  pinMode(DE_RE_CTRL_PIN, OUTPUT);
  node.preTransmission([](){ digitalWrite(DE_RE_CTRL_PIN, HIGH); });
  node.postTransmission([](){ digitalWrite(DE_RE_CTRL_PIN, LOW); });
  node.begin(SLAVE_ID, Serial2);
  Serial.println("INFO: Modbus RTU initialized");

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("ERROR: SPIFFS Mount Failed");
    return;
  }
  Serial.println("INFO: SPIFFS initialized");

  // SD Card (legacy initialization)
  if (SD.begin(SD_CS_PIN)) {
    sdCardInitialized = true;
    // dataBuffer.reserve(2048); // Legacy buffer
    Serial.println("INFO: SD Card initialized (legacy mode)");
  } else {
    Serial.println("WARNING: SD Card Mount Failed (using robust logger instead)");
  }

  // WiFi
  WiFi.softAP("ESP32-AP", "12345678");
  Serial.print("INFO: AP IP address: "); Serial.println(WiFi.softAPIP());

  // --- Legacy Web Server Setup ---
  setupLegacyWebServer();
  
  // WebSocket Server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("INFO: WebSocket server started");
  
  lastFlushTime = millis();
  
  Serial.println("=== System Initialization Complete ===");
}

void setupLegacyWebServer() {
  Serial.println("INFO: Setting up legacy web server endpoints");
  
  // Root endpoint
  server.on("/", HTTP_GET, [&]() {
    File file = SPIFFS.open("/main.html", "r");
    if (file) { server.streamFile(file, "text/html"); file.close(); }
    else { server.send(404, "text/plain", "main.html not found"); }
  });

  // SD Card Operations
  server.on("/list_files", HTTP_GET, [&]() {
    if (!sdCardInitialized) {
      server.send(503, "text/plain", "SD Card not available");
      return;
    }
    
    String fileList = "[";
    File root = SD.open("/");
    if (!root) {
      server.send(500, "text/plain", "Failed to open SD card directory");
      return;
    }
    
    File file = root.openNextFile();
    bool firstFile = true;
    while (file) {
      if (!file.isDirectory() && String(file.name()).endsWith(".csv")) {
        if (!firstFile) fileList += ",";
        fileList += "\"" + String(file.name()) + "\"";
        firstFile = false;
      }
      file.close();
      file = root.openNextFile();
    }
    root.close();
    fileList += "]";
    
    server.send(200, "application/json", fileList);
  });

  server.on("/delete_file", HTTP_GET, [&]() {
    if (!sdCardInitialized || !server.hasArg("filename")) {
      server.send(400, "text/plain", "Bad Request");
      return;
    }
    String filename = server.arg("filename");
    if (filename.startsWith("/")) {
      filename = filename.substring(1); // Remove leading slash for compatibility
    }
    
    if (SD.remove(filename.c_str())) {
      server.send(200, "text/plain", "File deleted successfully");
    } else {
      server.send(500, "text/plain", "Failed to delete file");
    }
  });

  server.on("/download_file", HTTP_GET, [&]() {
    if (!sdCardInitialized || !server.hasArg("filename")) {
      server.send(400, "text/plain", "Bad Request");
      return;
    }
    // Use robust logger for download
    String filename = server.arg("filename");
    if (filename.startsWith("/")) {
      filename = filename.substring(1); // Remove leading slash for compatibility
    }
    
    // Check if file exists
    File file = SD.open(filename);
    if (!file) {
      server.send(404, "text/plain", "File not found");
      return;
    }
    
    // Send file with proper headers
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    server.sendHeader("Content-Type", "text/csv");
    server.sendHeader("Content-Length", String(file.size()));
    server.streamFile(file, "application/octet-stream");
    file.close();
  });

  // Database Operations
  server.on("/db_clear", HTTP_GET, [&]() {
    if (db.clear()) {
      server.send(200, "text/plain", "Database cleared successfully");
    } else {
      server.send(500, "text/plain", "Failed to clear database");
    }
  });

  server.on("/db_load", HTTP_GET, [&]() {
    String filename = server.arg("filename");
    if (filename.isEmpty()) {
      server.send(400, "text/plain", "Bad Request: filename parameter required");
      return;
    }
    
    if (db.loadFromFile(filename)) {
      server.send(200, "text/plain", "Database loaded successfully from: " + filename);
    } else {
      server.send(500, "text/plain", "Failed to load database from: " + filename);
    }
  });

  server.on("/db_save", HTTP_GET, [&]() {
    String filename = server.arg("filename");
    if (filename.isEmpty()) {
      server.send(400, "text/plain", "Bad Request: filename parameter required");
      return;
    }
    
    if (db.saveToFile(filename)) {
      server.send(200, "text/plain", "Database saved successfully to: " + filename);
    } else {
      server.send(500, "text/plain", "Failed to save database to: " + filename);
    }
  });

  server.on("/db_info", HTTP_GET, [&]() {
    String info = "Database Information:\n";
    info += "Keys: " + String(db.getKeyCount()) + "\n";
    info += "Memory Usage: " + String(db.getMemoryUsage()) + " bytes\n";
    info += "Total Data Size: " + String(db.getTotalDataSize()) + " bytes\n";
    info += "Auto Save: " + String(db.getAutoSave() ? "Enabled" : "Disabled") + "\n";
    
    server.send(200, "text/plain", info);
  });

  server.on("/db_export", HTTP_GET, [&]() {
    String filename = server.arg("filename");
    if (filename.isEmpty()) filename = "/database_export.csv";
    
    if (db.exportToCSV(filename)) {
      server.send(200, "text/plain", "Database exported successfully to: " + filename);
    } else {
      server.send(500, "text/plain", "Failed to export database to: " + filename);
    }
  });

  server.on("/db_import", HTTP_GET, [&]() {
    String filename = server.arg("filename");
    if (filename.isEmpty()) {
      server.send(400, "text/plain", "Bad Request: filename parameter required");
      return;
    }
    
    if (db.importFromCSV(filename)) {
      server.send(200, "text/plain", "Database imported successfully from: " + filename);
    } else {
      server.send(500, "text/plain", "Failed to import database from: " + filename);
    }
  });

  server.on("/db_get", HTTP_GET, [&]() {
    String key = server.arg("key");
    String value = db.get(key);
    server.send(200, "text/plain", value);
  });

  server.on("/db_set", HTTP_GET, [&]() {
    String key = server.arg("key");
    String value = server.arg("value");
    if (db.set(key, value)) {
      server.send(200, "text/plain", "OK");
    } else {
      server.send(400, "text/plain", "Bad Request");
    }
  });
  
  server.begin();
  Serial.println("INFO: Legacy HTTP server started");
}

void loop() {
  // Update robust logger system
  robustLogger.update();

  // Handle legacy web server and WebSocket
  webSocket.loop();
  server.handleClient();
  
  // Update database (handles auto-save)
  db.update();

  // --- Sensor Data Reading ---
  if (millis() - lastReadTime > DATA_READ_INTERVAL_MS) {
    lastReadTime = millis();
    uint8_t result = node.readHoldingRegisters(REG_ADDRESS, REG_COUNT);
    if (result == node.ku8MBSuccess) {
      uint16_t newFlowRate = node.getResponseBuffer(0);
      if (newFlowRate != currentFlowRate) {
        currentFlowRate = newFlowRate;
        String payload = String(currentFlowRate);
        
        // Broadcast via WebSocket
        webSocket.broadcastTXT(payload);

        // --- Core Recording Logic ---
        if (isRecording) {
          // Store data in database
          String timestampKey = "flow_" + String(millis());
          String flowValue = String(currentFlowRate);
          db.set(timestampKey, flowValue);
          
          // Use robust logger for data logging
          robustLogger.logData(String(currentFlowRate));
          
          // Legacy logging (for compatibility)
          if (sdCardInitialized) {
            // ใช้ millis() เป็น timestamp (เวลาตั้งแต่เปิดเครื่อง)
            // String logLine = String(millis()) + "," + String(currentFlowRate);
            // dataBuffer += logLine + "\n";
            // bufferCount++;
            // if (bufferCount >= DATA_BUFFER_SIZE) {
            //   flushBufferToSD();
            // }
          }
        }
      }
    }
  }

  // --- Legacy Buffer Management ---
  // if (isRecording && sdCardInitialized && bufferCount > 0 && (millis() - lastFlushTime > FLUSH_INTERVAL_MS)) {
  //     flushBufferToSD();
  // }

  // --- System Health Check ---
  checkSystemHealth();

  delay(2);
}

void checkSystemHealth() {
  static unsigned long lastHealthCheck = 0;
  const unsigned long HEALTH_CHECK_INTERVAL = 30000; // 30 seconds
  
  if (millis() - lastHealthCheck > HEALTH_CHECK_INTERVAL) {
    lastHealthCheck = millis();
    
    // Check robust logger health
    if (!robustLogger.isSystemReady()) {
      Serial.println("WARNING: Robust logger system not ready");
    }
    
    // Check SD card health
    if (sdCardInitialized && !SD.cardPresent()) {
      Serial.println("WARNING: SD card not present");
      sdCardInitialized = false;
    }
    
    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WARNING: WiFi connection lost");
    }
    
    // Check Modbus connection
    uint8_t result = node.readHoldingRegisters(REG_ADDRESS, REG_COUNT);
    if (result != node.ku8MBSuccess) {
      Serial.println("WARNING: Modbus communication failed");
    }
  }
}

// --- WebSocket Event Handler ---
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] Connected from %s\n", num, ip.toString().c_str());
      String initialPayload = String(currentFlowRate);
      webSocket.sendTXT(num, initialPayload);
      break;
    }
    case WStype_TEXT:
      // --- รับคำสั่ง Start/Stop จาก Client ---
      String command = String((char*)payload);
      Serial.printf("[%u] Received command: %s\n", num, command.c_str());
      
      if (command == "START_REC") {
        // Use robust logger for recording start
        if (robustLogger.startLogging()) {
          isRecording = true;
          // currentLogFilename = createTimestampedFilename(); // สร้างชื่อไฟล์ใหม่เมื่อเริ่มบันทึก
          webSocket.broadcastTXT("STATUS:Recording Started");
          Serial.println("INFO: Recording started via robust logger");
        } else {
          webSocket.sendTXT(num, "ERROR:Failed to start recording");
          Serial.println("ERROR: Failed to start recording");
        }
      } else if (command == "STOP_REC") {
        // Use robust logger for recording stop
        isRecording = false;
        robustLogger.stopLogging();
        // flushBufferToSD(); // เขียนข้อมูลที่เหลือใน buffer ลงไฟล์
        webSocket.broadcastTXT("STATUS:Recording Stopped");
        Serial.println("INFO: Recording stopped via robust logger");
      }
      break;
  }
}

// --- Legacy SD Card Functions (Commented out) ---
/*
void flushBufferToSD() {
    if (bufferCount == 0 || !sdCardInitialized) return;
    
    Serial.printf("Flushing %d records to %s...\n", bufferCount, currentLogFilename.c_str());
    
    // Use robust logger for SD operations
    if (robustLogger.getSystemStatus() == "LOGGING") {
        // Flush buffer using robust logger
        if (!dataBuffer.isEmpty()) {
            File dataFile = SD.open(currentLogFilename, FILE_APPEND);
            if (dataFile) {
                dataFile.print(dataBuffer);
                dataFile.close();
                Serial.println("INFO: Buffer flushed successfully using robust logger");
            } else {
                Serial.println("ERROR: Failed to open file for appending");
            }
        }
    } else {
        // Fallback to direct SD operation
        File dataFile = SD.open(currentLogFilename, FILE_APPEND);
        if (dataFile) {
            dataFile.print(dataBuffer);
            dataFile.close();
            Serial.println("INFO: Buffer flushed successfully using direct SD operation");
        } else {
            Serial.println("ERROR: Failed to open file for appending");
        }
    }
    
    dataBuffer = "";
    bufferCount = 0;
    lastFlushTime = millis();
}

String listFiles() {
  String fileList = "[";
  File root = SD.open("/");
  if (!root) return "[]";
  File file = root.openNextFile();
  bool firstFile = true;
  while (file) {
    if (!file.isDirectory() && String(file.name()).endsWith(".csv")) {
      if (!firstFile) fileList += ",";
      fileList += "\"" + String(file.name()) + "\"";
      firstFile = false;
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  fileList += "]";
  return fileList;
}

void deleteFile(String path) {
  if (!sdCardInitialized || path.length() == 0) return;
  
  // Remove leading slash if present for SD library compatibility
  if (path.startsWith("/")) path = path.substring(1);
  
  Serial.printf("Deleting file: %s\n", path.c_str());
  if (SD.remove(path.c_str())) {
    Serial.println("- file deleted");
  } else {
    Serial.println("- delete failed");
  }
}

void downloadFile(String path) {
  if (!sdCardInitialized || path.length() == 0) return;
  // Remove leading slash if present for SD library compatibility
  if (path.startsWith("/")) path = path.substring(1);
  File file = SD.open(path.c_str(), "r");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=" + path);
  server.sendHeader("Content-Type", "text/csv");
  server.sendHeader("Content-Length", String(file.size()));
  server.streamFile(file, "text/csv");
  file.close();
}

String createTimestampedFilename() {
    // สร้างชื่อไฟล์จากเวลาที่เปิดเครื่อง (millis) เพื่อให้ไม่ซ้ำกัน
    // หากมี RTC สามารถเปลี่ยนเป็นวันที่และเวลาจริงได้
    return "/log_" + String(millis() / 1000) + ".csv";
}
*/
