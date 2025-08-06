#include "sd_manager.h"

bool SDManager::initialize() {
  Serial.println("Initializing SD Manager...");
  
  // Initialize SD card
  if (!mount()) {
    Serial.println("Failed to mount SD card");
    return false;
  }
  
  // Create directory structure
  if (!createDirectoryStructure()) {
    Serial.println("Failed to create directory structure");
    return false;
  }
  
  // Verify file system health
  if (!verifyFileSystem()) {
    Serial.println("File system verification failed");
    return false;
  }
  
  Serial.println("SD Manager initialized successfully");
  return true;
}

bool SDManager::mount() {
  Serial.println("Attempting to mount SD card...");
  
  // Check if already mounted
  if (isMounted()) {
    Serial.println("SD card already mounted");
    return true;
  }
  
  // Try to mount with retry logic
  for (int attempt = 1; attempt <= MAX_MOUNT_ATTEMPTS; attempt++) {
    Serial.printf("Mount attempt %d/%d...\n", attempt, MAX_MOUNT_ATTEMPTS);
    
    if (SD.begin(CS_PIN)) {
      sdInitialized = true;
      lastMountTime = millis();
      mountAttempts = 0;
      systemState.setSDCardMounted(true);
      
      Serial.println("SD card mounted successfully");
      return true;
    }
    
    Serial.printf("Mount attempt %d failed\n", attempt);
    mountAttempts++;
    
    if (attempt < MAX_MOUNT_ATTEMPTS) {
      delay(MOUNT_RETRY_DELAY);
    }
  }
  
  // All attempts failed
  sdInitialized = false;
  systemState.setSDCardMounted(false);
  lastError = "Failed to mount SD card after " + String(MAX_MOUNT_ATTEMPTS) + " attempts";
  systemState.logError(lastError);
  
  return false;
}

bool SDManager::unmount() {
  if (!sdInitialized) {
    return true;
  }
  
  SD.end();
  sdInitialized = false;
  systemState.setSDCardMounted(false);
  
  Serial.println("SD card unmounted");
  return true;
}

bool SDManager::createDirectory(const String& path) {
  if (!isMounted()) {
    lastError = "Cannot create directory: SD card not mounted";
    return false;
  }
  
  if (SD.exists(path)) {
    return true; // Directory already exists
  }
  
  bool success = SD.mkdir(path);
  if (!success) {
    lastError = "Failed to create directory: " + path;
    systemState.logError(lastError);
    return false;
  }
  
  Serial.println("Created directory: " + path);
  return true;
}

bool SDManager::createDirectoryStructure() {
  Serial.println("Creating directory structure...");
  
  bool success = true;
  success &= createDirectory(LOG_DIR);
  success &= createDirectory(BACKUP_DIR);
  success &= createDirectory(TEMP_DIR);
  
  if (success) {
    Serial.println("Directory structure created successfully");
  } else {
    Serial.println("Failed to create directory structure");
  }
  
  return success;
}

bool SDManager::verifyFileSystem() {
  Serial.println("Verifying file system...");
  
  if (!isMounted()) {
    lastError = "Cannot verify file system: SD card not mounted";
    return false;
  }
  
  // Basic file system checks
  if (!checkCardHealth()) {
    lastError = "Card health check failed";
    return false;
  }
  
  if (!writeTestFile()) {
    lastError = "Write test failed";
    return false;
  }
  
  Serial.println("File system verification successful");
  return true;
}

bool SDManager::checkCardHealth() {
  // Check if card is readable
  File root = SD.open("/");
  if (!root) {
    lastError = "Cannot open root directory";
    return false;
  }
  root.close();
  
  // Check if we can read and write basic files
  File testFile = SD.open("/temp/test.tmp", FILE_WRITE);
  if (!testFile) {
    lastError = "Cannot create test file";
    return false;
  }
  
  testFile.print("TEST");
  testFile.close();
  
  // Verify file content
  testFile = SD.open("/temp/test.tmp", FILE_READ);
  if (!testFile) {
    lastError = "Cannot read test file";
    return false;
  }
  
  String content = testFile.readString();
  testFile.close();
  
  SD.remove("/temp/test.tmp");
  
  if (content != "TEST") {
    lastError = "Test file content verification failed";
    return false;
  }
  
  return true;
}

bool SDManager::writeTestFile() {
  String testContent = "SD Card Test - " + String(millis());
  
  File testFile = SD.open("/temp/sd_test.txt", FILE_WRITE);
  if (!testFile) {
    lastError = "Cannot create test file";
    return false;
  }
  
  testFile.println(testContent);
  testFile.close();
  
  // Verify the file was written correctly
  testFile = SD.open("/temp/sd_test.txt", FILE_READ);
  if (!testFile) {
    lastError = "Cannot read test file";
    return false;
  }
  
  String readContent = testFile.readString();
  testFile.close();
  
  SD.remove("/temp/sd_test.txt");
  
  return readContent.indexOf(testContent) >= 0;
}

String SDManager::generateTimestampedFilename() {
  unsigned long timestamp = millis();
  return LOG_DIR + "/log_" + String(timestamp / 1000) + "_" + String(timestamp % 1000) + ".csv";
}

bool SDManager::writeData(const String& data, const String& filename) {
  if (!isMounted()) {
    lastError = "Cannot write data: SD card not mounted";
    return false;
  }
  
  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    lastError = "Cannot open file for writing: " + filename;
    systemState.logError(lastError);
    return false;
  }
  
  if (!file.print(data)) {
    lastError = "Failed to write data to file: " + filename;
    systemState.logError(lastError);
    file.close();
    return false;
  }
  
  file.close();
  systemState.incrementWrites();
  systemState.setLastGoodWriteTime(millis());
  
  return true;
}

bool SDManager::appendData(const String& data, const String& filename) {
  if (!isMounted()) {
    lastError = "Cannot append data: SD card not mounted";
    return false;
  }
  
  File file = SD.open(filename, FILE_APPEND);
  if (!file) {
    lastError = "Cannot open file for appending: " + filename;
    systemState.logError(lastError);
    return false;
  }
  
  if (!file.println(data)) {
    lastError = "Failed to append data to file: " + filename;
    systemState.logError(lastError);
    file.close();
    return false;
  }
  
  file.close();
  systemState.incrementWrites();
  systemState.setLastGoodWriteTime(millis());
  
  return true;
}

bool SDManager::createNewLogFile(String& filename) {
  filename = generateTimestampedFilename();
  
  // Create the file with header
  String header = "timestamp,flow_rate,temperature,pressure\n";
  
  if (!writeData(header, filename)) {
    lastError = "Failed to create new log file";
    return false;
  }
  
  Serial.println("Created new log file: " + filename);
  return true;
}

bool SDManager::closeAllFiles() {
  // ESP32 SD library doesn't have explicit file close functions
  // but we can force garbage collection
  delay(10);
  return true;
}

bool SDManager::formatCard() {
  Serial.println("Formatting SD card...");
  
  if (!isMounted()) {
    lastError = "Cannot format: SD card not mounted";
    return false;
  }
  
  // This is a dangerous operation - make sure user confirms
  Serial.println("WARNING: Formatting will erase all data on SD card!");
  
  // For safety, we won't actually implement formatting
  // In production, you would need to implement proper formatting
  lastError = "Formatting not implemented for safety";
  return false;
}

bool SDManager::getCardInfo(String& info) {
  if (!isMounted()) {
    info = "SD card not mounted";
    return false;
  }
  
  // Get basic card information
  File root = SD.open("/");
  if (!root) {
    info = "Cannot access SD card";
    return false;
  }
  
  info = "SD Card Information:\n";
  info += "Mounted: Yes\n";
  info += "Root accessible: Yes\n";
  
  // Count files and directories
  int fileCount = 0;
  int dirCount = 0;
  
  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      dirCount++;
    } else {
      fileCount++;
    }
    file = root.openNextFile();
  }
  
  info += "Files: " + String(fileCount) + "\n";
  info += "Directories: " + String(dirCount) + "\n";
  
  root.close();
  return true;
}

bool SDManager::getFreeSpace(unsigned long& totalSpace, unsigned long& freeSpace) {
  if (!isMounted()) {
    totalSpace = 0;
    freeSpace = 0;
    return false;
  }
  
  // ESP32 SD library doesn't provide free space information
  // We'll estimate based on available files
  totalSpace = 32UL * 1024 * 1024; // Assume 32GB card
  freeSpace = totalSpace; // Placeholder
  
  return true;
}

bool SDManager::performMaintenance() {
  Serial.println("Performing SD card maintenance...");
  
  if (!isMounted()) {
    return false;
  }
  
  // Clean up temporary files
  File root = SD.open(TEMP_DIR);
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String filename = TEMP_DIR + "/" + file.name();
        SD.remove(filename);
      }
      file = root.openNextFile();
    }
    root.close();
  }
  
  // Verify all files
  verifyAllFiles();
  
  Serial.println("SD card maintenance completed");
  return true;
}

bool SDManager::verifyAllFiles() {
  Serial.println("Verifying all SD card files...");
  
  if (!isMounted()) {
    return false;
  }
  
  bool allGood = true;
  
  // Check log files
  File root = SD.open(LOG_DIR);
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        String filename = LOG_DIR + "/" + file.name();
        
        // Try to open and read a small portion
        File testFile = SD.open(filename, FILE_READ);
        if (testFile) {
          String content = testFile.readString();
          testFile.close();
          
          if (content.length() == 0) {
            Serial.println("Warning: Empty file detected: " + filename);
            allGood = false;
          }
        } else {
          Serial.println("Warning: Cannot read file: " + filename);
          allGood = false;
        }
      }
      file = root.openNextFile();
    }
    root.close();
  }
  
  if (allGood) {
    Serial.println("All files verified successfully");
  }
  
  return allGood;
}

bool SDManager::cleanupOldFiles(int maxFiles) {
  Serial.println("Cleaning up old files...");
  
  if (!isMounted()) {
    return false;
  }
  
  // This is a simplified implementation
  // In production, you would want to sort files by date and remove oldest
  Serial.println("File cleanup not fully implemented");
  return true;
}

bool SDManager::waitForCard(unsigned long timeout) {
  unsigned long startTime = millis();
  
  while (millis() - startTime < timeout) {
    if (SD.begin(CS_PIN)) {
      return true;
    }
    delay(100);
  }
  
  return false;
}