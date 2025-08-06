#include "data_logger.h"

bool DataLogger::initialize() {
  Serial.println("Initializing Data Logger...");
  
  // Initialize buffer
  if (!initializeBuffer()) {
    Serial.println("Failed to initialize buffer");
    return false;
  }
  
  // Attempt to resume logging if enabled
  if (autoResume) {
    if (!attemptResumeLogging()) {
      Serial.println("Failed to resume logging, starting fresh");
      if (!createNewLogFile()) {
        Serial.println("Failed to create new log file");
        return false;
      }
    }
  } else {
    if (!createNewLogFile()) {
      Serial.println("Failed to create new log file");
      return false;
    }
  }
  
  // Set initial state
  bufferStartTime = millis();
  lastBufferFlush = millis();
  lastLogTime = millis();
  
  Serial.println("Data Logger initialized successfully");
  return true;
}

bool DataLogger::initializeBuffer() {
  dataBuffer.clear();
  dataBuffer.reserve(maxBufferSize);
  
  Serial.println("Buffer initialized with capacity: " + String(maxBufferSize));
  return true;
}

bool DataLogger::createNewLogFile() {
  String newLogFile;
  if (!sdManager.createNewLogFile(newLogFile)) {
    systemState.logError("Failed to create new log file");
    return false;
  }
  
  currentLogFile = newLogFile;
  fileOpen = true;
  
  // Update system state
  systemState.setCurrentLogFile(currentLogFile);
  systemState.setLoggingActive(true);
  
  Serial.println("New log file created: " + currentLogFile);
  return true;
}

bool DataLogger::attemptResumeLogging() {
  Serial.println("Attempting to resume logging...");
  
  // Get the current log file from system state
  String resumeFile = systemState.getCurrentLogFile();
  
  if (resumeFile.isEmpty()) {
    Serial.println("No resume file specified, starting fresh");
    return false;
  }
  
  // Verify the file exists and is accessible
  if (!sdManager.isMounted()) {
    Serial.println("Cannot resume: SD card not mounted");
    return false;
  }
  
  // Check if file exists
  if (!SD.exists(resumeFile.c_str())) {
    Serial.println("Resume file not found: " + resumeFile);
    return false;
  }
  
  // Validate the file
  if (!validateLogFile(resumeFile)) {
    Serial.println("Resume file validation failed: " + resumeFile);
    return false;
  }
  
  // Set current log file
  currentLogFile = resumeFile;
  fileOpen = true;
  
  // Update system state
  systemState.setLoggingActive(true);
  
  Serial.println("Logging resumed from: " + currentLogFile);
  return true;
}

bool DataLogger::validateLogFile(const String& filename) {
  File file = SD.open(filename.c_str(), FILE_READ);
  if (!file) {
    return false;
  }
  
  // Check if file has proper header
  String header = file.readStringUntil('\n');
  file.close();
  
  // Basic header validation
  return header.indexOf("timestamp") >= 0 || header.indexOf("flow_rate") >= 0;
}

bool DataLogger::logData(const String& data) {
  // Check if system is ready for logging
  if (!isSystemReady()) {
    return false;
  }
  
  // Check if it's time to log
  if (millis() - lastLogTime < logInterval) {
    return false;
  }
  
  lastLogTime = millis();
  
  // Add timestamp if enabled
  String logData = data;
  if (enableTimestamps) {
    logData = String(millis()) + "," + logData;
  }
  
  // Add to buffer
  dataBuffer.push_back(logData);
  totalDataLogged += logData.length();
  
  // Check if buffer is full
  if (dataBuffer.size() >= maxBufferSize) {
    bufferOverflows++;
    if (!flushBufferToSD()) {
      systemState.incrementFailedWrites();
      return false;
    }
  }
  
  // Check if it's time to flush buffer
  if (millis() - lastBufferFlush > BUFFER_FLUSH_INTERVAL) {
    if (!flushBufferToSD()) {
      systemState.incrementFailedWrites();
      return false;
    }
  }
  
  return true;
}

bool DataLogger::flushBufferToSD() {
  if (dataBuffer.empty()) {
    return true; // Nothing to flush
  }
  
  if (!fileOpen || currentLogFile.isEmpty()) {
    systemState.logError("Cannot flush: No file open");
    return false;
  }
  
  // Append all buffered data to SD card
  for (const String& data : dataBuffer) {
    if (!sdManager.appendData(data, currentLogFile)) {
      systemState.logError("Failed to append data to: " + currentLogFile);
      return false;
    }
  }
  
  // Clear buffer
  dataBuffer.clear();
  bufferFlushCount++;
  lastBufferFlush = millis();
  
  // Create recovery point
  createRecoveryPoint();
  
  return true;
}

bool DataLogger::update() {
  // Check if we need to flush buffer
  if (!dataBuffer.empty()) {
    // Check if buffer is full or flush interval has passed
    if (dataBuffer.size() >= maxBufferSize || 
        millis() - lastBufferFlush > BUFFER_FLUSH_INTERVAL) {
      if (!flushBufferToSD()) {
        systemState.incrementFailedWrites();
        return false;
      }
    }
  }
  
  // Check system health
  if (!isSystemReady()) {
    systemState.transitionTo(systemState.ERROR);
    return false;
  }
  
  return true;
}

bool DataLogger::forceFlushBuffer() {
  if (dataBuffer.empty()) {
    return true;
  }
  
  bool success = flushBufferToSD();
  if (!success) {
    systemState.incrementFailedWrites();
  }
  return success;
}

bool DataLogger::shutdown() {
  Serial.println("Shutting down Data Logger...");
  
  // Force flush any remaining data
  forceFlushBuffer();
  
  // Close current file
  closeCurrentFile();
  
  // Save recovery point
  createRecoveryPoint();
  
  // Update system state
  systemState.setLoggingActive(false);
  
  Serial.println("Data Logger shutdown complete");
  return true;
}

bool DataLogger::closeCurrentFile() {
  if (!fileOpen) {
    return true;
  }
  
  fileOpen = false;
  currentLogFile = "";
  
  // Clear system state
  systemState.setCurrentLogFile("");
  
  Serial.println("Current log file closed");
  return true;
}

bool DataLogger::switchLogFile() {
  Serial.println("Switching to new log file...");
  
  // Flush current buffer
  if (!forceFlushBuffer()) {
    return false;
  }
  
  // Close current file
  closeCurrentFile();
  
  // Create new file
  if (!createNewLogFile()) {
    return false;
  }
  
  Serial.println("Log file switched successfully");
  return true;
}

bool DataLogger::getCurrentLogFile(String& filename) {
  filename = currentLogFile;
  return !currentLogFile.isEmpty();
}

bool DataLogger::createRecoveryPoint() {
  return systemState.createRecoveryPoint();
}

bool DataLogger::attemptRecovery() {
  Serial.println("Attempting data logger recovery...");
  
  // Try to resume from system state
  if (attemptResumeLogging()) {
    Serial.println("Data logger recovery successful");
    return true;
  }
  
  // If resume fails, create new file
  if (!createNewLogFile()) {
    Serial.println("Data logger recovery failed");
    return false;
  }
  
  Serial.println("Data logger recovery successful with new file");
  return true;
}

bool DataLogger::clearRecoveryData() {
  // Clear recovery data from system state
  systemState.loadState(); // This will clear the recovery point
  
  // Clear any temporary files
  if (sdManager.isMounted()) {
    sdManager.performMaintenance();
  }
  
  return true;
}

float DataLogger::getAverageDataSize() {
  if (bufferFlushCount == 0) {
    return 0;
  }
  
  return (float)totalDataLogged / bufferFlushCount;
}

String DataLogger::getDiagnostics() {
  String diagnostics = "Data Logger Diagnostics:\n";
  diagnostics += "Status: " + systemState.getStatusString() + "\n";
  diagnostics += "Current File: " + currentLogFile + "\n";
  diagnostics += "Buffer Size: " + String(dataBuffer.size()) + "/" + String(maxBufferSize) + "\n";
  diagnostics += "Buffer Utilization: " + String((float)dataBuffer.size() / maxBufferSize * 100, 1) + "%\n";
  diagnostics += "Total Data Logged: " + String(totalDataLogged) + " bytes\n";
  diagnostics += "Buffer Flushes: " + String(bufferFlushCount) + "\n";
  diagnostics += "Buffer Overflows: " + String(bufferOverflows) + "\n";
  diagnostics += "Average Data Size: " + String(getAverageDataSize(), 2) + " bytes\n";
  diagnostics += "Last Flush: " + String(millis() - lastBufferFlush) + " ms ago\n";
  diagnostics += "System Ready: " + String(isSystemReady() ? "Yes" : "No") + "\n";
  
  return diagnostics;
}

bool DataLogger::isSystemReady() {
  // Check system state
  if (systemState.getStatus() != SystemState::READY && 
      systemState.getStatus() != SystemState::LOGGING &&
      systemState.getStatus() != SystemState::RECOVERING) {
    return false;
  }
  
  // Check SD card
  if (!sdManager.isMounted()) {
    return false;
  }
  
  // Check if we have a current file
  if (currentLogFile.isEmpty()) {
    return false;
  }
  
  return true;
}

bool DataLogger::performMaintenance() {
  Serial.println("Performing Data Logger maintenance...");
  
  // Flush buffer
  forceFlushBuffer();
  
  // Switch to new log file if needed
  if (currentLogFile.length() > 50) { // Prevent very long filenames
    switchLogFile();
  }
  
  // Clean up old files
  if (sdManager.isMounted()) {
    sdManager.cleanupOldFiles(10); // Keep last 10 log files
  }
  
  // Reset statistics if needed
  if (bufferOverflows > 10) {
    resetStatistics();
  }
  
  Serial.println("Data Logger maintenance completed");
  return true;
}

void DataLogger::resetStatistics() {
  totalDataLogged = 0;
  bufferFlushCount = 0;
  bufferOverflows = 0;
  
  Serial.println("Data Logger statistics reset");
}