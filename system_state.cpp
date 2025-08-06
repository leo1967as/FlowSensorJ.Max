#include "system_state.h"

SystemState::SystemState() 
  : currentStatus(INITIALIZING), 
    lastGoodWriteTime(0), 
    systemStartTime(millis()), 
    sdCardMounted(false),
    loggingActive(false),
    totalWrites(0),
    failedWrites(0),
    lastError("") {
}

bool SystemState::initialize() {
  Serial.println("Initializing System State...");
  
  // Initialize SPIFFS for state persistence
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to initialize SPIFFS");
    return false;
  }
  
  // Load previous state if available
  loadState();
  
  // Check system integrity
  if (!checkIntegrity()) {
    Serial.println("System integrity check failed, resetting state");
    currentStatus = INITIALIZING;
  }
  
  // Set initial state
  systemStartTime = millis();
  lastGoodWriteTime = 0;
  totalWrites = 0;
  failedWrites = 0;
  
  Serial.println("System State initialized successfully");
  return true;
}

bool SystemState::checkIntegrity() {
  // Basic integrity checks
  if (currentStatus < INITIALIZING || currentStatus > FILESYSTEM_ERROR) {
    return false;
  }
  
  // Check if current logfile exists (if we're in logging state)
  if (currentStatus == LOGGING && !currentLogFile.isEmpty()) {
    // This will be checked when SD card is available
    return true;
  }
  
  return true;
}

bool SystemState::saveStateToSPIFFS() {
  File stateFile = SPIFFS.open(STATE_FILE, "w");
  if (!stateFile) {
    Serial.println("Failed to open state file for writing");
    return false;
  }
  
  // Save state data in a simple format
  stateFile.println("status=" + String(currentStatus));
  stateFile.println("lastWriteTime=" + String(lastGoodWriteTime));
  stateFile.println("startTime=" + String(systemStartTime));
  stateFile.println("currentLogFile=" + currentLogFile);
  stateFile.println("sdMounted=" + String(sdCardMounted ? "1" : "0"));
  stateFile.println("loggingActive=" + String(loggingActive ? "1" : "0"));
  stateFile.println("totalWrites=" + String(totalWrites));
  stateFile.println("failedWrites=" + String(failedWrites));
  stateFile.println("lastError=" + lastError);
  
  stateFile.close();
  return true;
}

bool SystemState::loadStateFromSPIFFS() {
  File stateFile = SPIFFS.open(STATE_FILE, "r");
  if (!stateFile) {
    Serial.println("No previous state file found, starting fresh");
    return false;
  }
  
  String line;
  while (stateFile.available()) {
    line = stateFile.readStringUntil('\n');
    line.trim();
    
    int equalSign = line.indexOf('=');
    if (equalSign > 0) {
      String key = line.substring(0, equalSign);
      String value = line.substring(equalSign + 1);
      
      if (key == "status") {
        currentStatus = (SystemStatus)value.toInt();
      } else if (key == "lastWriteTime") {
        lastGoodWriteTime = value.toInt();
      } else if (key == "startTime") {
        systemStartTime = value.toInt();
      } else if (key == "currentLogFile") {
        currentLogFile = value;
      } else if (key == "sdMounted") {
        sdCardMounted = (value == "1");
      } else if (key == "loggingActive") {
        loggingActive = (value == "1");
      } else if (key == "totalWrites") {
        totalWrites = value.toInt();
      } else if (key == "failedWrites") {
        failedWrites = value.toInt();
      } else if (key == "lastError") {
        lastError = value;
      }
    }
  }
  
  stateFile.close();
  Serial.println("State loaded from SPIFFS");
  return true;
}

void SystemState::saveState() {
  if (!saveStateToSPIFFS()) {
    Serial.println("Failed to save state to SPIFFS");
  }
}

void SystemState::loadState() {
  loadStateFromSPIFFS();
}

bool SystemState::transitionTo(SystemStatus newStatus) {
  Serial.print("Transitioning from ");
  Serial.print(getStatusString());
  Serial.print(" to ");
  Serial.println(getStatusString(newStatus));
  
  // Validate state transitions
  switch (newStatus) {
    case INITIALIZING:
      // Can transition to initializing from any state
      break;
    case READY:
      // Can only transition to ready from initializing or error
      if (currentStatus != INITIALIZING && currentStatus != ERROR && currentStatus != RECOVERING) {
        return false;
      }
      break;
    case LOGGING:
      // Can only transition to logging from ready
      if (currentStatus != READY && currentStatus != RECOVERING) {
        return false;
      }
      break;
    case ERROR:
      // Can transition to error from any state
      break;
    case RECOVERING:
      // Can only transition to recovering from error
      if (currentStatus != ERROR) {
        return false;
      }
      break;
    case SD_MOUNT_FAILED:
      // Can only transition to SD mount failed from ready or logging
      if (currentStatus != READY && currentStatus != LOGGING) {
        return false;
      }
      break;
    case FILESYSTEM_ERROR:
      // Can only transition to filesystem error from ready or logging
      if (currentStatus != READY && currentStatus != LOGGING) {
        return false;
      }
      break;
    default:
      return false;
  }
  
  currentStatus = newStatus;
  saveState();
  return true;
}

String SystemState::getStatusString() {
  return getStatusString(currentStatus);
}

String SystemState::getStatusString(SystemStatus status) {
  switch (status) {
    case INITIALIZING: return "INITIALIZING";
    case READY: return "READY";
    case LOGGING: return "LOGGING";
    case ERROR: return "ERROR";
    case RECOVERING: return "RECOVERING";
    case SD_MOUNT_FAILED: return "SD_MOUNT_FAILED";
    case FILESYSTEM_ERROR: return "FILESYSTEM_ERROR";
    default: return "UNKNOWN";
  }
}

void SystemState::setCurrentLogFile(const String& filename) {
  currentLogFile = filename;
  saveState();
}

void SystemState::logError(const String& error) {
  lastError = error;
  Serial.println("System Error: " + error);
  saveState();
}

bool SystemState::createRecoveryPoint() {
  // Create a recovery point with current state
  File recoveryFile = SPIFFS.open("/recovery.point", "w");
  if (!recoveryFile) {
    logError("Failed to create recovery point");
    return false;
  }
  
  recoveryFile.println("lastWriteTime=" + String(lastGoodWriteTime));
  recoveryFile.println("currentLogFile=" + currentLogFile);
  recoveryFile.println("totalWrites=" + String(totalWrites));
  recoveryFile.println("timestamp=" + String(millis()));
  
  recoveryFile.close();
  return true;
}

bool SystemState::attemptRecovery() {
  Serial.println("Attempting system recovery...");
  
  File recoveryFile = SPIFFS.open("/recovery.point", "r");
  if (!recoveryFile) {
    Serial.println("No recovery point found, starting fresh");
    return false;
  }
  
  String line;
  bool recoverySuccessful = false;
  
  while (recoveryFile.available()) {
    line = recoveryFile.readStringUntil('\n');
    line.trim();
    
    int equalSign = line.indexOf('=');
    if (equalSign > 0) {
      String key = line.substring(0, equalSign);
      String value = line.substring(equalSign + 1);
      
      if (key == "lastWriteTime") {
        lastGoodWriteTime = value.toInt();
      } else if (key == "currentLogFile") {
        currentLogFile = value;
        recoverySuccessful = true;
      } else if (key == "totalWrites") {
        totalWrites = value.toInt();
      }
    }
  }
  
  recoveryFile.close();
  
  if (recoverySuccessful) {
    Serial.println("Recovery successful, resuming from: " + currentLogFile);
    transitionTo(RECOVERING);
    return true;
  } else {
    Serial.println("Recovery failed, starting fresh");
    return false;
  }
}