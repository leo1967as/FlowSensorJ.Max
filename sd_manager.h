#ifndef SD_MANAGER_H
#define SD_MANAGER_H

#include <Arduino.h>
#include <SD.h>
#include <FS.h>
#include "system_state.h"

class SDManager {
private:
  const int CS_PIN = 21;
  const String LOG_DIR = "/logs";
  const String BACKUP_DIR = "/backups";
  const String TEMP_DIR = "/temp";
  
  SystemState& systemState;
  bool sdInitialized;
  unsigned long lastMountTime;
  int mountAttempts;
  const int MAX_MOUNT_ATTEMPTS = 3;
  const unsigned long MOUNT_RETRY_DELAY = 5000; // 5 seconds
  
  bool createDirectory(const String& path);
  bool verifyFileSystem();
  String generateTimestampedFilename();
  bool checkCardHealth();
  bool writeTestFile();
  
public:
  SDManager(SystemState& state) : systemState(state), sdInitialized(false), 
                                  lastMountTime(0), mountAttempts(0) {}
  
  // Core operations
  bool initialize();
  bool mount();
  bool unmount();
  bool isMounted() { return sdInitialized && SD.begin(CS_PIN); }
  
  // File operations
  bool writeData(const String& data, const String& filename);
  bool appendData(const String& data, const String& filename);
  bool createNewLogFile(String& filename);
  bool closeAllFiles();
  
  // Directory operations
  bool createDirectoryStructure();
  bool getLogFileList(StringList& fileList);
  bool cleanupOldFiles(int maxFiles);
  
  // Card operations
  bool formatCard();
  bool getCardInfo(String& info);
  bool getFreeSpace(unsigned long& totalSpace, unsigned long& freeSpace);
  
  // Error handling
  String getLastError() { return lastError; }
  void clearLastError() { lastError = ""; }
  
  // Maintenance
  bool performMaintenance();
  bool verifyAllFiles();
  
private:
  String lastError;
  bool waitForCard(unsigned long timeout);
};

#endif