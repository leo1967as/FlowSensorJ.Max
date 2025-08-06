#include "database.h"

MemoryDatabase::MemoryDatabase(const String& path, bool autoSaveEnabled) 
  : databasePath(path), autoSave(autoSaveEnabled), lastSaveTime(millis()) {
  loadFromSPIFFS();
}

MemoryDatabase::~MemoryDatabase() {
  if (autoSave) {
    saveToSPIFFS();
  }
}

bool MemoryDatabase::set(const String& key, const String& value) {
  if (key.isEmpty()) return false;
  
  dataMap[key] = value;
  
  if (autoSave && (millis() - lastSaveTime > saveInterval)) {
    saveToSPIFFS();
    lastSaveTime = millis();
  }
  
  return true;
}

String MemoryDatabase::get(const String& key) {
  auto it = dataMap.find(key);
  if (it != dataMap.end()) {
    return it->second;
  }
  return String();
}

bool MemoryDatabase::remove(const String& key) {
  auto it = dataMap.find(key);
  if (it != dataMap.end()) {
    dataMap.erase(it);
    if (autoSave) {
      saveToSPIFFS();
      lastSaveTime = millis();
    }
    return true;
  }
  return false;
}

bool MemoryDatabase::exists(const String& key) {
  return dataMap.find(key) != dataMap.end();
}

int MemoryDatabase::size() {
  return dataMap.size();
}

bool MemoryDatabase::exportToCSV(const String& filename) {
  if (!SD.begin()) return false;
  
  File file = SD.open(filename.c_str(), FILE_WRITE);
  if (!file) return false;
  
  // Write CSV header
  file.println("Key,Value,Timestamp");
  
  // Write data rows
  unsigned long timestamp = millis();
  for (const auto& pair : dataMap) {
    file.println(pair.first + "," + pair.second + "," + String(timestamp));
  }
  
  file.close();
  return true;
}

bool MemoryDatabase::importFromCSV(const String& filename) {
  if (!SD.begin()) return false;
  
  File file = SD.open(filename.c_str(), FILE_READ);
  if (!file) return false;
  
  String line;
  while (file.available()) {
    line = file.readStringUntil('\n');
    line.trim();
    
    if (line.startsWith("Key,Value")) continue; // Skip header
    
    int comma1 = line.indexOf(',');
    int comma2 = line.indexOf(',', comma1 + 1);
    
    if (comma1 > 0 && comma2 > comma1) {
      String key = line.substring(0, comma1);
      String value = line.substring(comma1 + 1, comma2);
      dataMap[key] = value;
    }
  }
  
  file.close();
  if (autoSave) {
    saveToSPIFFS();
    lastSaveTime = millis();
  }
  return true;
}

bool MemoryDatabase::clearFile() {
  if (!SPIFFS.begin(true)) return false;
  
  File file = SPIFFS.open(databasePath, "w");
  if (!file) return false;
  
  file.close();
  dataMap.clear();
  return true;
}

bool MemoryDatabase::loadFromFile(const String& filename) {
  if (!SPIFFS.begin(true)) return false;
  
  File file = SPIFFS.open(filename, "r");
  if (!file) return false;
  
  dataMap.clear();
  
  String line;
  while (file.available()) {
    line = file.readStringUntil('\n');
    line.trim();
    
    int equalSign = line.indexOf('=');
    if (equalSign > 0) {
      String key = line.substring(0, equalSign);
      String value = line.substring(equalSign + 1);
      dataMap[key] = value;
    }
  }
  
  file.close();
  return true;
}

bool MemoryDatabase::saveToFile(const String& filename) {
  if (!SPIFFS.begin(true)) return false;
  
  File file = SPIFFS.open(filename, "w");
  if (!file) return false;
  
  for (const auto& pair : dataMap) {
    file.println(pair.first + "=" + pair.second);
  }
  
  file.close();
  return true;
}

void MemoryDatabase::clear() {
  dataMap.clear();
  if (autoSave) {
    saveToSPIFFS();
    lastSaveTime = millis();
  }
}

unsigned long MemoryDatabase::getMemoryUsage() {
  unsigned long total = 0;
  for (const auto& pair : dataMap) {
    total += pair.first.length() + pair.second.length();
  }
  return total;
}

void MemoryDatabase::update() {
  if (autoSave && (millis() - lastSaveTime > saveInterval)) {
    saveToSPIFFS();
    lastSaveTime = millis();
  }
}

void MemoryDatabase::forceSave() {
  saveToSPIFFS();
  lastSaveTime = millis();
}

void MemoryDatabase::setAutoSave(bool enabled) {
  autoSave = enabled;
}

bool MemoryDatabase::getAutoSave() {
  return autoSave;
}

int MemoryDatabase::getKeyCount() {
  return dataMap.size();
}

long MemoryDatabase::getTotalDataSize() {
  long total = 0;
  for (const auto& pair : dataMap) {
    total += pair.first.length() + pair.second.length();
  }
  return total;
}

std::vector<String> MemoryDatabase::getKeys() {
  std::vector<String> keys;
  for (const auto& pair : dataMap) {
    keys.push_back(pair.first);
  }
  return keys;
}

std::vector<String> MemoryDatabase::getValues() {
  std::vector<String> values;
  for (const auto& pair : dataMap) {
    values.push_back(pair.second);
  }
  return values;
}

std::map<String, String> MemoryDatabase::getAll() {
  return dataMap;
}

bool MemoryDatabase::loadFromSPIFFS() {
  if (!SPIFFS.begin(true)) return false;
  
  File file = SPIFFS.open(databasePath, "r");
  if (!file) return false;
  
  dataMap.clear();
  
  String line;
  while (file.available()) {
    line = file.readStringUntil('\n');
    line.trim();
    
    int equalSign = line.indexOf('=');
    if (equalSign > 0) {
      String key = line.substring(0, equalSign);
      String value = line.substring(equalSign + 1);
      dataMap[key] = value;
    }
  }
  
  file.close();
  return true;
}

bool MemoryDatabase::saveToSPIFFS() {
  if (!SPIFFS.begin(true)) return false;
  
  File file = SPIFFS.open(databasePath, "w");
  if (!file) return false;
  
  for (const auto& pair : dataMap) {
    file.println(pair.first + "=" + pair.second);
  }
  
  file.close();
  return true;
}