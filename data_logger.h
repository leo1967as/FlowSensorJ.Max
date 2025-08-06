#ifndef DATA_LOGGER_H
#define DATA_LOGGER_H

#include <Arduino.h>
#include <vector>
#include "system_state.h"
#include "sd_manager.h"

class DataLogger {
private:
  SystemState& systemState;
  SDManager& sdManager;
  
  // Configuration
  unsigned long logInterval;
  unsigned long lastLogTime;
  bool autoResume;
  bool enableTimestamps;
  int maxBufferSize;
  
  // Data buffering
  std::vector<String> dataBuffer;
  unsigned long bufferStartTime;
  unsigned long lastBufferFlush;
  
  // File management
  String currentLogFile;
  bool fileOpen;
  
  // Statistics
  unsigned long totalDataLogged;
  unsigned long bufferFlushCount;
  unsigned long bufferOverflows;
  
  const unsigned long BUFFER_FLUSH_INTERVAL = 30000; // 30 seconds
  const int MAX_RECOVERY_ATTEMPTS = 3;
  
  bool initializeBuffer();
  bool flushBufferToSD();
  bool createNewLogFile();
  bool attemptResumeLogging();
  bool validateLogFile(const String& filename);
  
public:
  DataLogger(SystemState& state, SDManager& sdMgr) 
    : systemState(state), sdManager(sdMgr), logInterval(1000), 
      lastLogTime(0), autoResume(true), enableTimestamps(true),
      maxBufferSize(1000), bufferStartTime(0), lastBufferFlush(0),
      currentLogFile(""), fileOpen(false), totalDataLogged(0),
      bufferFlushCount(0), bufferOverflows(0) {}
  
  // Core operations
  bool initialize();
  bool logData(const String& data);
  bool update(); // Call this regularly in loop
  bool shutdown();
  
  // Configuration
  void setLogInterval(unsigned long interval) { logInterval = interval; }
  void setAutoResume(bool enable) { autoResume = enable; }
  void setEnableTimestamps(bool enable) { enableTimestamps = enable; }
  void setMaxBufferSize(int size) { maxBufferSize = size; }
  
  // File management
  bool getCurrentLogFile(String& filename);
  bool switchLogFile();
  bool closeCurrentFile();
  
  // Buffer management
  bool forceFlushBuffer();
  unsigned long getBufferSize() { return dataBuffer.size(); }
  unsigned long getBufferCapacity() { return maxBufferSize; }
  
  // Statistics
  unsigned long getTotalDataLogged() { return totalDataLogged; }
  unsigned long getBufferFlushCount() { return bufferFlushCount; }
  unsigned long getBufferOverflows() { return bufferOverflows; }
  float getAverageDataSize();
  
  // Recovery
  bool createRecoveryPoint();
  bool attemptRecovery();
  bool clearRecoveryData();
  
  // Diagnostics
  String getDiagnostics();
  bool isSystemReady();
  
  // Maintenance
  bool performMaintenance();
  void resetStatistics();
};

#endif