#include <SD.h>
#include <ModbusMaster.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

#define DE_RE_CTRL_PIN 4
#define RX_PIN 16
#define TX_PIN 17

#define SLAVE_ID 3
#define REG_ADDRESS 1
#define REG_COUNT 1

ModbusMaster node;
WebServer server(80);

void preTransmission();
void postTransmission();

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  pinMode(DE_RE_CTRL_PIN, OUTPUT);
  digitalWrite(DE_RE_CTRL_PIN, LOW);
  node.preTransmission(preTransmission);
  node.postTransmission(postTransmission);
  node.begin(SLAVE_ID, Serial2);
  Serial.println("ESP32 Modbus RTU Master Ready");

  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  // SD Card Initialization
  const int chipSelect = 5;
  if (!SD.begin(chipSelect)) {
    Serial.println("SD Card Mount Failed");
    return;
  }
  Serial.println("SD Card Ready");

  // --- Start: WiFi Access Point Setup ---
  const char* ssid = "ESP32-AP"; // ชื่อ WiFi ที่จะให้ ESP32 สร้างขึ้น
  const char* password = "12345678"; // รหัสผ่านสำหรับ WiFi (อย่างน้อย 8 ตัวอักษร)

  // Start Access Point
  WiFi.softAP(ssid, password);
  Serial.println("Access Point Started");
  Serial.print("IP address: ");
  Serial.println(WiFi.softAPIP()); // แสดง IP Address ของ ESP32 AP
  // --- End: WiFi Access Point Setup ---

  server.on("/", HTTP_GET, [&]() {
    File file = SPIFFS.open("/main.html", "r");
    if (!file) {
      Serial.println("Failed to open main.html");
      server.send(404, "text/plain", "File not found");
      return;
    }
    server.streamFile(file, "text/html");
    file.close();
  });
  server.on("/data", HTTP_GET, [&]() {
    uint8_t result = node.readHoldingRegisters(REG_ADDRESS, REG_COUNT);
    if (result == node.ku8MBSuccess) {
      uint16_t sensorValue = node.getResponseBuffer(0);
      JsonDocument doc;
      doc["flow_rate"] = sensorValue;
      String jsonString;
      serializeJson(doc, jsonString);
      server.send(200, "application/json", jsonString);
    } else {
      server.send(500, "text/plain", "Failed to read sensor data");
    }
  });

  server.on("/list_files", HTTP_GET, [&]() {
    String fileList = listFiles();
    server.send(200, "application/json", fileList);
  });

  server.on("/delete_file", HTTP_GET, [&]() {
    String filename = server.arg("filename");
    if (filename.length() > 0) {
      deleteFile(filename);
      server.send(200, "text/plain", "File deleted");
    } else {
      server.send(400, "text/plain", "Filename not provided");
    }
  });

  server.on("/download_file", HTTP_GET, [&]() {
    String filename = server.arg("filename");
    if (filename.length() > 0) {
      downloadFile(filename);
    } else {
      server.send(400, "text/plain", "Filename not provided");
    }
  });

  server.on("/write_file", HTTP_POST, [&]() {
    String filename = server.arg("filename");
    String content = server.arg("plain");
    if (filename.length() > 0 && content.length() > 0) {
      writeFile(filename, content);
      server.send(200, "text/plain", "File written successfully");
    } else {
      server.send(400, "text/plain", "Filename or content not provided");
    }
  });

  server.begin();
  Serial.println("Web server started");
}

void loop() {
  server.handleClient();
  delay(1000);
}

void preTransmission() {
  digitalWrite(DE_RE_CTRL_PIN, HIGH);
}

void postTransmission() {
  digitalWrite(DE_RE_CTRL_PIN, LOW);
}

String readFile(String path) {
  Serial.printf("Reading file: %s\r\n", path.c_str());

  File file = SD.open(path.c_str());
  if (!file || file.isDirectory()) {
    Serial.println("- failed to open file for reading");
    return String();
  }

  String fileContent;
  while (file.available()) {
    fileContent += String(file.readStringUntil('\n'));
  }
  Serial.println("- read from file: " + path);
  file.close();
  return fileContent;
}

void writeFile(String path, String content) {
  Serial.printf("Writing file: %s\r\n", path.c_str());

  File file = SD.open(path.c_str(), FILE_WRITE);
  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(content)) {
    Serial.println("- wrote to file: " + path);
  } else {
    Serial.println("- write failed");
  }
  file.close();
}

String listFiles() {
  String fileList = "[";
  File root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open directory");
    return "[]";
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.println("  DIR : " + String(file.name()));
    } else {
      String fileName = String(file.name());
      if (fileName.endsWith(".csv")) {
        Serial.println("  FILE: " + fileName);
        fileList += "\"" + fileName + "\",";
      }
    }
    file = root.openNextFile();
  }
  if (fileList.length() > 1) {
    fileList.remove(fileList.length() - 1);
  }
  fileList += "]";
  return fileList;
}

void deleteFile(String path) {
  Serial.printf("Deleting file: %s\r\n", path.c_str());
  if (SD.remove(path.c_str())) {
    Serial.println("- file deleted");
  } else {
    Serial.println("- delete failed");
  }
}

void downloadFile(String path) {
  Serial.printf("Downloading file: %s\r\n", path.c_str());

  File file = SD.open(path.c_str());
  if (!file) {
    Serial.println("- failed to open file for reading");
    return;
  }

  server.streamFile(file, "application/octet-stream");
  file.close();
}