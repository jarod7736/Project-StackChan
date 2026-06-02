#pragma once
#include <Arduino.h>

namespace stkchan {

class NvsStore {
 public:
  bool begin();   // false if NVS can't be opened
  void end();

  String getString(const char* key, const char* fallback = "");
  // Returns true iff bytes were written. An empty string is treated by
  // the underlying ESP32 Preferences API as a 0-byte write and reports
  // false here — use eraseKey() when you want to delete a key.
  bool   putString(const char* key, const String& value);
  bool   eraseKey(const char* key);

  bool hasKey(const char* key);
};

extern NvsStore nvs;  // global singleton

}  // namespace stkchan
