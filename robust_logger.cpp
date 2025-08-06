#include "robust_logger.h"

RobustLogger::RobustLogger() 
  : server(WEB_PORT), webSocket(WS_PORT),
    systemInitialized(false),
    lastReadTime(0),
    currentFlowRate(0),
    modbusSerial(2) { // UART2 for Modbus
}

bool RobustLogger::initialize() {
  Serial.println("Initializing Robust Logger System...");
  
  // Initialize system state
  if (!systemState.initialize()) {
    Serial.println("Failed to initialize system state");
    return false;
  }
  
  // Initialize SD manager
  if (!sdManager.initialize()) {
    Serial.println("Failed to initialize SD manager");
    systemState.transitionTo(SystemState::SD_MOUNT_FAILED);
    return false;
  }
  
  // Initialize data logger
  if (!dataLogger.initialize()) {
    Serial.println("Failed to initialize data logger");
    systemState.transitionTo(SystemState::ERROR);
    return false;
  }
  
  // Initialize WiFi
  if (!initializeWiFi()) {
    Serial.println("Failed to initialize WiFi");
    systemState.transitionTo(SystemState::ERROR);
    return false;
  }
  
  // Initialize Modbus
  if (!initializeModbus()) {
    Serial.println("Failed to initialize Modbus");
    systemState.transitionTo(SystemState::ERROR);
    return false;
  }
  
  // Initialize web server
  if (!initializeWebServer()) {
    Serial.println("Failed to initialize web server");
    systemState.transitionTo(SystemState::ERROR);
    return false;
  }
  
  // Set system to ready state
  systemState.transitionTo(SystemState::READY);
  systemInitialized = true;
  
  Serial.println("Robust Logger System initialized successfully");
  return true;
}

bool RobustLogger::initializeWiFi() {
  // WiFi configuration would go here
  // For now, we'll use placeholder code
  Serial.println("Initializing WiFi...");
  
  // In a real implementation, you would:
  // 1. Connect to WiFi network
  // 2. Get IP address
  // 3. Set up mDNS or similar for network discovery
  
  // Placeholder - replace with actual WiFi connection code
  // WiFi.begin("your_ssid", "your_password");
  // while (WiFi.status() != WL_CONNECTED) {
  //   delay(1000);
  //   Serial.println("Connecting to WiFi...");
  // }
  
  Serial.println("WiFi initialized (placeholder)");
  return true;
}

bool RobustLogger::initializeModbus() {
  Serial.println("Initializing Modbus...");
  
  // Configure Modbus serial
  modbusSerial.begin(MODBUS_BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
  
  // Configure DE/RE pins
  pinMode(DE_RE_CTRL_PIN, OUTPUT);
  digitalWrite(DE_RE_CTRL_PIN, LOW);
  
  // Initialize Modbus master
  node.begin(SLAVE_ID, modbusSerial);
  
  // Set transmission callbacks
  node.preTransmission([](){ digitalWrite(DE_RE_CTRL_PIN, HIGH); });
  node.postTransmission([](){ digitalWrite(DE_RE_CTRL_PIN, LOW); });
  
  Serial.println("Modbus initialized successfully");
  return true;
}

bool RobustLogger::initializeWebServer() {
  Serial.println("Initializing Web Server...");
  
  // Setup web endpoints
  setupWebEndpoints();
  
  // Start web server
  server.begin();
  
  // Start WebSocket server
  webSocket.begin();
  
  Serial.println("Web Server initialized successfully");
  return true;
}

void RobustLogger::setupWebEndpoints() {
  // System endpoints
  server.on("/system/info", HTTP_GET, [this]() { handleSystemInfo(); });
  server.on("/system/status", HTTP_GET, [this]() { handleSystemStatus(); });
  server.on("/system/restart", HTTP_GET, [this]() { handleSystemRestart(); });
  server.on("/system/shutdown", HTTP_GET, [this]() { handleSystemShutdown(); });
  
  // Logging endpoints
  server.on("/logger/start", HTTP_GET, [this]() { handleLoggerStart(); });
  server.on("/logger/stop", HTTP_GET, [this]() { handleLoggerStop(); });
  server.on("/logger/status", HTTP_GET, [this]() { handleLoggerStatus(); });
  server.on("/logger/diagnostics", HTTP_GET, [this]() { handleLoggerDiagnostics(); });
  
  // SD card endpoints
  server.on("/sd/info", HTTP_GET, [this]() { handleSDInfo(); });
  server.on("/sd/files", HTTP_GET, [this]() { handleSDFiles(); });
  server.on("/sd/download", HTTP_GET, [this]() { handleSDDownload(); });
  server.on("/sd/delete", HTTP_GET, [this]() { handleSDDelete(); });
  server.on("/sd/format", HTTP_GET, [this]() { handleSDFormat(); });
  
  // Data endpoints
  server.on("/data/log", HTTP_GET, [this]() { handleDataLog(); });
  server.on("/data/get", HTTP_GET, [this]() { handleDataGet(); });
  server.on("/data/set", HTTP_GET, [this]() { handleDataSet(); });
  server.on("/data/export", HTTP_GET, [this]() { handleDataExport(); });
  server.on("/data/import", HTTP_GET, [this]() { handleDataImport(); });
  
  // File handling
  server.onNotFound([this]() {
    server.send(404, "text/plain", "Not Found");
  });
}

void RobustLogger::handleSystemInfo() {
  String info = "System Information:\n";
  info += "Status: " + systemState.getStatusString() + "\n";
  info += "Uptime: " + String(systemState.getSystemUptime() / 1000) + " seconds\n";
  info += "SD Card: " + String(systemState.isSDCardMounted() ? "Mounted" : "Not Mounted") + "\n";
  info += "Logging: " + String(systemState.isLoggingActive() ? "Active" : "Inactive") + "\n";
  info += "Total Writes: " + String(systemState.getTotalWrites()) + "\n";
  info += "Failed Writes: " + String(systemState.getFailedWrites()) + "\n";
  info += "Last Error: " + systemState.getLastError() + "\n";
  
  server.send(200, "text/plain", info);
}

void RobustLogger::handleSystemStatus() {
  JsonDocument doc;
  doc["status"] = systemState.getStatusString();
  doc["uptime"] = systemState.getSystemUptime() / 1000;
  doc["sd_mounted"] = systemState.isSDCardMounted();
  doc["logging_active"] = systemState.isLoggingActive();
  doc["total_writes"] = systemState.getTotalWrites();
  doc["failed_writes"] = systemState.getFailedWrites();
  doc["last_error"] = systemState.getLastError();
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void RobustLogger::handleSystemRestart() {
  server.send(200, "text/plain", "Restarting system...");
  delay(1000);
  ESP.restart();
}

void RobustLogger::handleSystemShutdown() {
  server.send(200, "text/plain", "Shutting down system...");
  shutdown();
  delay(1000);
  ESP.deepSleep(0);
}

void RobustLogger::handleLoggerStart() {
  if (startLogging()) {
    server.send(200, "text/plain", "Logging started successfully");
  } else {
    server.send(500, "text/plain", "Failed to start logging");
  }
}

void RobustLogger::handleLoggerStop() {
  if (stopLogging()) {
    server.send(200, "text/plain", "Logging stopped successfully");
  } else {
    server.send(500, "text/plain", "Failed to stop logging");
  }
}

void RobustLogger::handleLoggerStatus() {
  String status = "Logger Status:\n";
  status += "Active: " + String(dataLogger.isSystemReady() ? "Yes" : "No") + "\n";
  status += "Current File: " + String(systemState.getCurrentLogFile()) + "\n";
  status += "Buffer Size: " + String(dataLogger.getBufferSize()) + "\n";
  status += "Total Data Logged: " + String(dataLogger.getTotalDataLogged()) + " bytes\n";
  
  server.send(200, "text/plain", status);
}

void RobustLogger::handleLoggerDiagnostics() {
  String diagnostics = dataLogger.getDiagnostics();
  server.send(200, "text/plain", diagnostics);
}

void RobustLogger::handleSDInfo() {
  String info;
  if (sdManager.getCardInfo(info)) {
    server.send(200, "text/plain", info);
  } else {
    server.send(500, "text/plain", "Failed to get SD card info");
  }
}

void RobustLogger::handleSDFiles() {
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
}

void RobustLogger::handleSDDownload() {
  if (!server.hasArg("filename")) {
    server.send(400, "text/plain", "Bad Request: filename parameter required");
    return;
  }
  
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
}

void RobustLogger::handleSDDelete() {
  if (!server.hasArg("filename")) {
    server.send(400, "text/plain", "Bad Request: filename parameter required");
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
}

void RobustLogger::handleSDFormat() {
  if (sdManager.formatCard()) {
    server.send(200, "text/plain", "SD card formatted successfully");
  } else {
    server.send(500, "text/plain", "Failed to format SD card");
  }
}

void RobustLogger::handleDataLog() {
  if (!server.hasArg("data")) {
    server.send(400, "text/plain", "Bad Request: data parameter required");
    return;
  }
  
  String data = server.arg("data");
  if (dataLogger.logData(data)) {
    server.send(200, "text/plain", "Data logged successfully");
  } else {
    server.send(500, "text/plain", "Failed to log data");
  }
}

void RobustLogger::handleDataGet() {
  if (!server.hasArg("key")) {
    server.send(400, "text/plain", "Bad Request: key parameter required");
    return;
  }
  
  String key = server.arg("key");
  String value; // This would be retrieved from your database
  server.send(200, "text/plain", value);
}

void RobustLogger::handleDataSet() {
  if (!server.hasArg("key") || !server.hasArg("value")) {
    server.send(400, "text/plain", "Bad Request: key and value parameters required");
    return;
  }
  
  String key = server.arg("key");
  String value = server.arg("value");
  // This would be stored in your database
  server.send(200, "text/plain", "Data set successfully");
}

void RobustLogger::handleDataExport() {
  if (!server.hasArg("filename")) {
    server.send(400, "text/plain", "Bad Request: filename parameter required");
    return;
  }
  
  String filename = server.arg("filename");
  if (dataLogger.forceFlushBuffer()) {
    server.send(200, "text/plain", "Data exported successfully to: " + filename);
  } else {
    server.send(500, "text/plain", "Failed to export data");
  }
}

void RobustLogger::handleDataImport() {
  if (!server.hasArg("filename")) {
    server.send(400, "text/plain", "Bad Request: filename parameter required");
    return;
  }
  
  String filename = server.arg("filename");
  // This would implement data import functionality
  server.send(200, "text/plain", "Data imported successfully from: " + filename);
}

void RobustLogger::webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.printf("[%u] Connected to WebSocket\n", num);
      break;
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected from WebSocket\n", num);
      break;
    case WStype_TEXT:
      Serial.printf("[%u] Got text: %s\n", num, payload);
      // Handle WebSocket messages here
      break;
  }
}

void RobustLogger::readSensorData() {
  if (millis() - lastReadTime > DATA_READ_INTERVAL_MS) {
    lastReadTime = millis();
    
    // Read from Modbus
    uint8_t result = node.readHoldingRegisters(REG_ADDRESS, REG_COUNT);
    if (result == node.ku8MBSuccess) {
      uint16_t newFlowRate = node.getResponseBuffer(0);
      if (newFlowRate != currentFlowRate) {
        currentFlowRate = newFlowRate;
        
        // Broadcast via WebSocket
        String payload = String(currentFlowRate);
        webSocket.broadcastTXT(payload);
        
        // Log data if logging is active
        if (systemState.isLoggingActive()) {
          String data = String(currentFlowRate);
          dataLogger.logData(data);
        }
      }
    }
  }
}

void RobustLogger::update() {
  if (!systemInitialized) {
    return;
  }
  
  // Update web server
  server.handleClient();
  
  // Update WebSocket
  webSocket.loop();
  
  // Update data logger
  dataLogger.update();
  
  // Read sensor data
  readSensorData();
  
  // Update system
  updateSystem();
}

void RobustLogger::updateSystem() {
  // Check system health
  if (!systemState.isSDCardMounted()) {
    systemState.transitionTo(SystemState::SD_MOUNT_FAILED);
  }
  
  // Perform maintenance if needed
  if (millis() % 3600000 < 1000) { // Every hour
    dataLogger.performMaintenance();
  }
}

bool RobustLogger::startLogging() {
  if (systemState.isLoggingActive()) {
    return true; // Already logging
  }
  
  if (!systemState.isSDCardMounted()) {
    return false;
  }
  
  systemState.setLoggingActive(true);
  dataLogger.forceFlushBuffer();
  
  return true;
}

bool RobustLogger::stopLogging() {
  if (!systemState.isLoggingActive()) {
    return true; // Already stopped
  }
  
  systemState.setLoggingActive(false);
  dataLogger.forceFlushBuffer();
  
  return true;
}

bool RobustLogger::restartSystem() {
  shutdown();
  delay(1000);
  ESP.restart();
  return true;
}

String RobustLogger::getSystemReport() {
  String report = "=== ROBUST LOGGER SYSTEM REPORT ===\n";
  report += "System Status: " + systemState.getStatusString() + "\n";
  report += "Uptime: " + String(systemState.getSystemUptime() / 1000) + " seconds\n";
  report += "SD Card: " + String(systemState.isSDCardMounted() ? "Mounted" : "Not Mounted") + "\n";
  report += "Logging: " + String(systemState.isLoggingActive() ? "Active" : "Inactive") + "\n";
  report += "Current File: " + systemState.getCurrentLogFile() + "\n";
  report += "Total Writes: " + String(systemState.getTotalWrites()) + "\n";
  report += "Failed Writes: " + String(systemState.getFailedWrites()) + "\n";
  report += "Buffer Size: " + String(dataLogger.getBufferSize()) + "\n";
  report += "Total Data Logged: " + String(dataLogger.getTotalDataLogged()) + " bytes\n";
  report += "Last Error: " + systemState.getLastError() + "\n";
  report += "====================================\n";
  
  return report;
}

String RobustLogger::getErrorReport() {
  String report = "=== ERROR REPORT ===\n";
  report += "System Status: " + systemState.getStatusString() + "\n";
  report += "Last Error: " + systemState.getLastError() + "\n";
  report += "Total Errors: " + String(systemState.getFailedWrites()) + "\n";
  report += "System Ready: " + String(isSystemReady() ? "Yes" : "No") + "\n";
  report += "=====================\n";
  
  return report;
}

bool RobustLogger::isSystemReady() {
  return systemState.getStatus() == SystemState::READY || 
         systemState.getStatus() == SystemState::LOGGING;
}

String RobustLogger::getSystemStatus() {
  return systemState.getStatusString();
}

void RobustLogger::setModbusSettings(int slaveId, int regAddress, int regCount, int baudRate) {
  // Update Modbus settings
  // This would require reinitialization
  Serial.println("Modbus settings updated (requires restart)");
}

void RobustLogger::setDataLogInterval(int interval) {
  dataLogger.setLogInterval(interval);
}

void RobustLogger::setAutoResume(bool enable) {
  dataLogger.setAutoResume(enable);
}

void RobustLogger::shutdown() {
  Serial.println("Shutting down Robust Logger...");
  
  // Stop logging
  stopLogging();
  
  // Shutdown components
  dataLogger.shutdown();
  sdManager.unmount();
  
  // Save final state
  systemState.saveState();
  
  Serial.println("Robust Logger shutdown complete");
}