#pragma once
#include <Arduino.h>

namespace stkchan {

class NvsStore {
 public:
  bool begin();   // false if NVS can't be opened
  void end();

  String getString(const char* key, const char* fallback = "");
  bool   putString(const char* key, const String& value);
  bool   eraseKey(const char* key);

  bool hasKey(const char* key);
};

extern NvsStore nvs;  // global singleton

}  // namespace stkchan
