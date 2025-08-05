#include <SD.h>
#include <ModbusMaster.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h> // ไลบรารีสำหรับ WebSocket
#include <ArduinoJson.h>

// --- Pin Definitions ---
#define DE_RE_CTRL_PIN 4
#define RX_PIN 16
#define TX_PIN 17
#define SD_CS_PIN 21

// --- Modbus Settings ---
#define SLAVE_ID 3
#define REG_ADDRESS 20
#define REG_COUNT 1

// --- Performance Tuning ---
#define MODBUS_BAUD_RATE 19200      // ความเร็วในการสื่อสาร Modbus (ตรวจสอบว่า Slave รองรับ)
#define DATA_READ_INTERVAL_MS 50     // อ่านค่าจาก Modbus ทุกๆ 50ms (20 ครั้งต่อวินาที)

// --- Global Objects ---
WebServer server(80);                     // WebServer สำหรับ HTTP ที่พอร์ต 80
WebSocketsServer webSocket = WebSocketsServer(81); // WebSocket Server ที่พอร์ต 81

ModbusMaster node;

// --- Global State Flags & Timers ---
bool sdCardInitialized = false;
unsigned long lastReadTime = 0;
uint16_t currentFlowRate = 0; // เก็บค่าล่าสุด

// --- SD Card Data Buffering ---
String dataBuffer = "";
int bufferCount = 0;
#define DATA_BUFFER_SIZE 50      // จำนวนข้อมูลที่จะเก็บใน RAM ก่อนเขียนลง SD Card
#define FLUSH_INTERVAL_MS 5000   // หรือเขียนลงการ์ดทุกๆ 5 วินาที
unsigned long lastFlushTime = 0;


// --- Function Prototypes ---
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
void flushBufferToSD();
String listFiles();
void deleteFile(String path);
void downloadFile(String path);


void setup() {
  Serial.begin(115200);
  
  // --- Modbus Initialization ---
  Serial2.begin(MODBUS_BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  pinMode(DE_RE_CTRL_PIN, OUTPUT);
  digitalWrite(DE_RE_CTRL_PIN, LOW);
  node.preTransmission([](){ digitalWrite(DE_RE_CTRL_PIN, HIGH); });
  node.postTransmission([](){ digitalWrite(DE_RE_CTRL_PIN, LOW); });
  node.begin(SLAVE_ID, Serial2);
  Serial.println("ESP32 Modbus RTU Master Ready");

  // --- SPIFFS & SD Card Initialization ---
  if (!SPIFFS.begin(true)) { 
    Serial.println("SPIFFS Mount Failed"); 
    return; 
  }
  Serial.println("SPIFFS Ready");
  
  if (SD.begin(SD_CS_PIN)) {
    sdCardInitialized = true;
    dataBuffer.reserve(2048); // จองหน่วยความจำสำหรับ Buffer ล่วงหน้า
    Serial.println("SD Card Ready");
  } else {
    Serial.println("SD Card Mount Failed. Continuing without SD card functionality.");
  }

  // --- WiFi Access Point Setup ---
  WiFi.softAP("ESP32-AP", "12345678");
  Serial.print("AP IP address: "); 
  Serial.println(WiFi.softAPIP());

  // --- Web Server (HTTP) Endpoints ---
  server.on("/", HTTP_GET, [&]() {
    File file = SPIFFS.open("/main.html", "r");
    if (file) {
      server.streamFile(file, "text/html");
      file.close();
    } else {
      server.send(404, "text/plain", "main.html not found");
    }
  });

  server.on("/list_files", HTTP_GET, [&]() {
    if (!sdCardInitialized) { server.send(503, "text/plain", "SD Card not available"); return; }
    flushBufferToSD(); // เขียนข้อมูลที่ค้างอยู่ลงการ์ดก่อนแสดงรายการ
    server.send(200, "application/json", listFiles());
  });

  server.on("/delete_file", HTTP_GET, [&]() {
    if (!sdCardInitialized) { server.send(503, "text/plain", "SD Card not available"); return; }
    if (server.hasArg("filename")) {
      deleteFile(server.arg("filename"));
      server.send(200, "text/plain", "File deletion command sent.");
    } else {
      server.send(400, "text/plain", "Filename not provided");
    }
  });

  server.on("/download_file", HTTP_GET, [&]() {
    if (!sdCardInitialized) { server.send(503, "text/plain", "SD Card not available"); return; }
    flushBufferToSD(); // เขียนข้อมูลที่ค้างอยู่ลงการ์ดก่อนดาวน์โหลด
    if (server.hasArg("filename")) {
      downloadFile(server.arg("filename"));
    } else {
      server.send(400, "text/plain", "Filename not provided");
    }
  });

  // Endpoint สำหรับรับ Log จากหน้าเว็บเพื่อมาเก็บใน Buffer
  server.on("/write_log", HTTP_POST, [&]() {
    if (!sdCardInitialized) { server.send(503, "text/plain", "SD Card not available"); return; }
    if (server.hasArg("plain")) {
      dataBuffer += server.arg("plain") + "\n";
      bufferCount++;
      if (bufferCount >= DATA_BUFFER_SIZE) { 
        flushBufferToSD(); 
      }
      server.send(200, "text/plain", "Log received");
    } else {
      server.send(400, "text/plain", "No data provided");
    }
  });
  
  server.begin();
  Serial.println("HTTP server started");

  // --- WebSocket Server Initialization ---
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  Serial.println("WebSocket server started on port 81");
  lastFlushTime = millis();
}

void loop() {
  webSocket.loop();      // จัดการการเชื่อมต่อ WebSocket
  server.handleClient(); // จัดการการเชื่อมต่อ HTTP

  // --- Non-blocking Modbus Read and WebSocket Push ---
  if (millis() - lastReadTime > DATA_READ_INTERVAL_MS) {
    lastReadTime = millis();
    uint8_t result = node.readHoldingRegisters(REG_ADDRESS, REG_COUNT);
    if (result == node.ku8MBSuccess) {
      uint16_t newFlowRate = node.getResponseBuffer(0);
      if (newFlowRate != currentFlowRate) {
        currentFlowRate = newFlowRate;
        
        // --- จุดที่แก้ไข 1 ---
        // สร้างตัวแปร String ขึ้นมาก่อน
        String payload = String(currentFlowRate);
        // แล้วค่อยส่งตัวแปรนั้นเข้าไป
        webSocket.broadcastTXT(payload);
      }
    }
  }

  // --- Non-blocking SD Card Buffer Flush ---
  if (sdCardInitialized && bufferCount > 0 && (millis() - lastFlushTime > FLUSH_INTERVAL_MS)) {
      flushBufferToSD();
  }

  delay(2); // ให้เวลา Task อื่นๆ ของ ESP32 ทำงาน (สำคัญมาก)
}

// --- ฟังก์ชันจัดการ Event ของ WebSocket ---
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {
      IPAddress ip = webSocket.remoteIP(num);
      Serial.printf("[%u] Connected from %s\n", num, ip.toString().c_str());

      // --- จุดที่แก้ไข 2 ---
      // สร้างตัวแปร String ขึ้นมาก่อน
      String payload = String(currentFlowRate);
      // แล้วค่อยส่งตัวแปรนั้นเข้าไป
      webSocket.sendTXT(num, payload);
      break;
    }
    case WStype_TEXT:
      Serial.printf("[%u] get Text: %s\n", num, payload);
      break;
    case WStype_BIN:
    case WStype_ERROR:
    case WStype_FRAGMENT_TEXT_START:
    case WStype_FRAGMENT_BIN_START:
    case WStype_FRAGMENT:
    case WStype_FRAGMENT_FIN:
      break;
  }
}

// --- ฟังก์ชันจัดการไฟล์บน SD Card (เหมือนเดิม) ---
void flushBufferToSD() {
    if (bufferCount == 0 || !sdCardInitialized) {
        return;
    }
    Serial.printf("Flushing %d records to SD card...\n", bufferCount);
    File dataFile = SD.open("/datalog.csv", FILE_APPEND);
    if (dataFile) {
        dataFile.print(dataBuffer);
        dataFile.close();
        Serial.println("Flush successful.");
    } else {
        Serial.println("Failed to open datalog.csv for appending.");
    }
    dataBuffer = "";
    bufferCount = 0;
    lastFlushTime = millis();
}

String listFiles() {
  String fileList = "[";
  File root = SD.open("/");
  if (!root) {
    return "[]";
  }
  File file = root.openNextFile();
  bool firstFile = true;
  while (file) {
    if (!file.isDirectory() && String(file.name()).endsWith(".csv")) {
      if (!firstFile) {
        fileList += ",";
      }
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
  Serial.printf("Deleting file: %s\n", path.c_str());
  if (SD.remove(path.c_str())) {
    Serial.println("- file deleted");
  } else {
    Serial.println("- delete failed");
  }
}

void downloadFile(String path) {
  if (!sdCardInitialized || path.length() == 0) return;
  File file = SD.open(path.c_str(), "r");
  if (!file) {
    server.send(404, "text/plain", "File not found on SD card");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=" + path);
  server.streamFile(file, "application/octet-stream");
  file.close();
}
