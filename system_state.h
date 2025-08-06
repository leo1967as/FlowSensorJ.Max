#ifndef SYSTEM_STATE_H
#define SYSTEM_STATE_H

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>

class SystemState {
private:
  enum SystemStatus { 
    INITIALIZING, 
    READY, 
    LOGGING, 
    ERROR, 
    RECOVERING,
    SD_MOUNT_FAILED,
    FILESYSTEM_ERROR
  };
  
  SystemStatus currentStatus;
  unsigned long lastGoodWriteTime;
  unsigned long systemStartTime;
  String currentLogFile;
  bool sdCardMounted;
  bool loggingActive;
  unsigned long totalWrites;
  unsigned long failedWrites;
  String lastError;
  
  const String STATE_FILE = "/system.state";
  
  bool saveStateToSPIFFS();
  bool loadStateFromSPIFFS();

public:
  SystemState();
  
  // Core operations
  bool initialize();
  bool checkIntegrity();
  void saveState();
  void loadState();
  bool transitionTo(SystemStatus newStatus);
  SystemStatus getStatus() { return currentStatus; }
  String getStatusString();
  
  // File management
  void setCurrentLogFile(const String& filename);
  String getCurrentLogFile() { return currentLogFile; }
  
  // SD card operations
  void setSDCardMounted(bool mounted) { sdCardMounted = mounted; }
  bool isSDCardMounted() { return sdCardMounted; }
  
  // Logging operations
  void setLoggingActive(bool active) { loggingActive = active; }
  bool isLoggingActive() { return loggingActive; }
  
  // Error handling
  void logError(const String& error);
  String getLastError() { return lastError; }
  void clearLastError() { lastError = ""; }
  
  // Statistics
  void incrementWrites() { totalWrites++; }
  void incrementFailedWrites() { failedWrites++; }
  unsigned long getTotalWrites() { return totalWrites; }
  unsigned long getFailedWrites() { return failedWrites; }
  
  // Time operations
  unsigned long getSystemUptime() { return millis() - systemStartTime; }
  void setLastGoodWriteTime(unsigned long time) { lastGoodWriteTime = time; }
  unsigned long getLastGoodWriteTime() { return lastGoodWriteTime; }
  
  // Recovery
  bool createRecoveryPoint();
  bool attemptRecovery();
};

#endif