#ifndef ROBUST_LOGGER_H
#define ROBUST_LOGGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <ModbusMaster.h>
#include <HardwareSerial.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "system_state.h"
#include "sd_manager.h"
#include "data_logger.h"

class RobustLogger {
private:
  // System components
  SystemState systemState;
  SDManager sdManager;
  DataLogger dataLogger;
  
  // Web server and WebSocket
  WebServer server;
  WebSocketsServer webSocket;
  
  // Configuration
  const int CS_PIN = 21;
  const int DE_RE_CTRL_PIN = 4;
  const int RX_PIN = 16;
  const int TX_PIN = 17;
  const int WEB_PORT = 80;
  const int WS_PORT = 81;
  
  // Modbus settings
  const int SLAVE_ID = 3;
  const int REG_ADDRESS = 20;
  const int REG_COUNT = 1;
  const int MODBUS_BAUD_RATE = 19200;
  const int DATA_READ_INTERVAL_MS = 50;
  
  // State variables
  bool systemInitialized;
  unsigned long lastReadTime;
  uint16_t currentFlowRate;
  
  // Modbus
  ModbusMaster node;
  HardwareSerial modbusSerial;
  
  // Web endpoints
  void setupWebEndpoints();
  
  // System endpoints
  void handleSystemInfo();
  void handleSystemStatus();
  void handleSystemRestart();
  void handleSystemShutdown();
  
  // Logging endpoints
  void handleLoggerStart();
  void handleLoggerStop();
  void handleLoggerStatus();
  void handleLoggerDiagnostics();
  
  // SD card endpoints
  void handleSDInfo();
  void handleSDFiles();
  void handleSDDownload();
  void handleSDDelete();
  void handleSDFormat();
  
  // Data endpoints
  void handleDataLog();
  void handleDataGet();
  void handleDataSet();
  void handleDataExport();
  void handleDataImport();
  
  // WebSocket events
  void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);
  
  // System initialization
  bool initializeSystem();
  bool initializeWiFi();
  bool initializeModbus();
  bool initializeWebServer();
  
  // Main system loop
  void readSensorData();
  void updateSystem();
  
public:
  RobustLogger();
  
  // Core operations
  bool initialize();
  void update();
  void shutdown();
  
  // Configuration
  void setModbusSettings(int slaveId, int regAddress, int regCount, int baudRate);
  void setDataLogInterval(int interval);
  void setAutoResume(bool enable);
  
  // Diagnostics
  String getSystemReport();
  String getErrorReport();
  
  // System status
  bool isSystemReady();
  String getSystemStatus();
  
  // Control functions
  bool startLogging();
  bool stopLogging();
  bool restartSystem();
};

#endif