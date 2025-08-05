#include <SD.h>
#include <ModbusMaster.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

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
#define DATA_BUFFER_SIZE 50      // จำนวนข้อมูลที่จะเก็บใน RAM ก่อนเขียนลง SD Card
#define FLUSH_INTERVAL_MS 5000   // หรือเขียนลงการ์ดทุกๆ 5 วินาที

// --- Global Objects ---
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
ModbusMaster node;

// --- Global State Flags & Timers ---
bool sdCardInitialized = false;
bool isRecording = false; // สถานะการบันทึก ควบคุมโดย WebSocket
unsigned long lastReadTime = 0;
uint16_t currentFlowRate = 0;
unsigned long lastFlushTime = 0;

// --- SD Card Data Buffering ---
String dataBuffer = "";
int bufferCount = 0;
String currentLogFilename = "";


// --- Function Prototypes ---
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void flushBufferToSD();
String listFiles();
void deleteFile(String path);
void downloadFile(String path);
String createTimestampedFilename();


void setup() {
  Serial.begin(115200);
  
  // --- Initialization (Modbus, SPIFFS, SD Card, WiFi) ---
  Serial2.begin(MODBUS_BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  pinMode(DE_RE_CTRL_PIN, OUTPUT);
  node.preTransmission([](){ digitalWrite(DE_RE_CTRL_PIN, HIGH); });
  node.postTransmission([](){ digitalWrite(DE_RE_CTRL_PIN, LOW); });
  node.begin(SLAVE_ID, Serial2);
  Serial.println("Modbus Master Ready");

  if (!SPIFFS.begin(true)) { Serial.println("SPIFFS Mount Failed"); return; }
  Serial.println("SPIFFS Ready");
  
  if (SD.begin(SD_CS_PIN)) {
    sdCardInitialized = true;
    dataBuffer.reserve(2048);
    Serial.println("SD Card Ready");
  } else {
    Serial.println("SD Card Mount Failed");
  }

  WiFi.softAP("ESP32-AP", "12345678");
  Serial.print("AP IP address: "); Serial.println(WiFi.softAPIP());

  // --- Web Server (HTTP) Endpoints ---
  server.on("/", HTTP_GET, [&]() {
    File file = SPIFFS.open("/main.html", "r");
    if (file) { server.streamFile(file, "text/html"); file.close(); } 
    else { server.send(404, "text/plain", "main.html not found"); }
  });

  server.on("/list_files", HTTP_GET, [&]() {
    if (!sdCardInitialized) { server.send(503, "text/plain", "SD Card not available"); return; }
    flushBufferToSD();
    server.send(200, "application/json", listFiles());
  });

  server.on("/delete_file", HTTP_GET, [&]() {
    if (!sdCardInitialized || !server.hasArg("filename")) { server.send(400, "text/plain", "Bad Request"); return; }
    deleteFile(server.arg("filename"));
    server.send(200, "text/plain", "Delete command sent.");
  });

  server.on("/download_file", HTTP_GET, [&]() {
    if (!sdCardInitialized || !server.hasArg("filename")) { server.send(400, "text/plain", "Bad Request"); return; }
    flushBufferToSD();
    downloadFile(server.arg("filename"));
  });
  
  server.begin();
  Serial.println("HTTP server started");

  // --- WebSocket Server Initialization ---
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
    uint8_t result = node.readHoldingRegisters(REG_ADDRESS, REG_COUNT);
    if (result == node.ku8MBSuccess) {
      uint16_t newFlowRate = node.getResponseBuffer(0);
      if (newFlowRate != currentFlowRate) {
        currentFlowRate = newFlowRate;
        String payload = String(currentFlowRate);
        webSocket.broadcastTXT(payload);

        // --- Core Recording Logic ---
        if (isRecording && sdCardInitialized) {
          // ใช้ millis() เป็น timestamp (เวลาตั้งแต่เปิดเครื่อง)
          String logLine = String(millis()) + "," + String(currentFlowRate);
          dataBuffer += logLine + "\n";
          bufferCount++;
          if (bufferCount >= DATA_BUFFER_SIZE) {
            flushBufferToSD();
          }
        }
      }
    }
  }

  if (isRecording && sdCardInitialized && bufferCount > 0 && (millis() - lastFlushTime > FLUSH_INTERVAL_MS)) {
      flushBufferToSD();
  }

  delay(2);
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
        if (sdCardInitialized) {
          isRecording = true;
          currentLogFilename = createTimestampedFilename(); // สร้างชื่อไฟล์ใหม่เมื่อเริ่มบันทึก
          webSocket.broadcastTXT("STATUS:Recording Started");
        } else {
          webSocket.sendTXT(num, "ERROR:SD Card not ready");
        }
      } else if (command == "STOP_REC") {
        isRecording = false;
        flushBufferToSD(); // เขียนข้อมูลที่เหลือใน buffer ลงไฟล์
        webSocket.broadcastTXT("STATUS:Recording Stopped");
      }
      break;
  }
}

// --- SD Card Functions ---
void flushBufferToSD() {
    if (bufferCount == 0 || !sdCardInitialized) return;
    
    Serial.printf("Flushing %d records to %s...\n", bufferCount, currentLogFilename.c_str());
    File dataFile = SD.open(currentLogFilename, FILE_APPEND);
    if (dataFile) {
        dataFile.print(dataBuffer);
        dataFile.close();
        Serial.println("Flush successful.");
    } else {
        Serial.printf("Failed to open %s for appending.\n", currentLogFilename.c_str());
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
  if (!sdCardInitialized || path.length() == 0 || !path.startsWith("/")) return;
  Serial.printf("Deleting file: %s\n", path.c_str());
  if (SD.remove(path.c_str())) Serial.println("- file deleted");
  else Serial.println("- delete failed");
}

void downloadFile(String path) {
  if (!sdCardInitialized || path.length() == 0 || !path.startsWith("/")) return;
  File file = SD.open(path.c_str(), "r");
  if (!file) { server.send(404, "text/plain", "File not found"); return; }
  server.sendHeader("Content-Disposition", "attachment; filename=" + path.substring(1));
  server.streamFile(file, "application/octet-stream");
  file.close();
}

String createTimestampedFilename() {
    // สร้างชื่อไฟล์จากเวลาที่เปิดเครื่อง (millis) เพื่อให้ไม่ซ้ำกัน
    // หากมี RTC สามารถเปลี่ยนเป็นวันที่และเวลาจริงได้
    return "/log_" + String(millis() / 1000) + ".csv";
}
