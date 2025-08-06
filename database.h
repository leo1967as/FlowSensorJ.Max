#ifndef DATABASE_H
#define DATABASE_H

#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <vector>
#include <map>

class MemoryDatabase {
private:
  std::map<String, String> dataMap;
  String databasePath;
  bool autoSave;
  unsigned long lastSaveTime;
  const unsigned long saveInterval = 30000; // 30 seconds
  
  bool loadFromSPIFFS();
  bool saveToSPIFFS();

public:
  MemoryDatabase(const String& path = "/database.db", bool autoSaveEnabled = true);
  ~MemoryDatabase();
  
  // Basic operations
  bool set(const String& key, const String& value);
  String get(const String& key);
  bool remove(const String& key);
  bool exists(const String& key);
  int size();
  
  // File operations
  bool exportToCSV(const String& filename);
  bool importFromCSV(const String& filename);
  bool clearFile();
  bool loadFromFile(const String& filename);
  bool saveToFile(const String& filename);
  
  // Memory management
  void clear();
  unsigned long getMemoryUsage();
  
  // Auto-save functionality
  void update();
  void forceSave();
  void setAutoSave(bool enabled);
  bool getAutoSave();
  
  // Statistics
  int getKeyCount();
  long getTotalDataSize();
  
  // Utility functions
  std::vector<String> getKeys();
  std::vector<String> getValues();
  std::map<String, String> getAll();
};

#endif