#include "services/NvsStore.h"
#include <Preferences.h>
#include "config.h"

namespace stkchan {

NvsStore nvs;
static Preferences prefs;
static bool g_open = false;

bool NvsStore::begin() {
  if (g_open) return true;
  g_open = prefs.begin(kNvsNamespace, /*readOnly=*/false);
  return g_open;
}

void NvsStore::end() {
  if (g_open) {
    prefs.end();
    g_open = false;
  }
}

String NvsStore::getString(const char* key, const char* fallback) {
  if (!g_open) return String(fallback);
  return prefs.getString(key, fallback);
}

bool NvsStore::putString(const char* key, const String& value) {
  if (!g_open) return false;
  return prefs.putString(key, value.c_str()) > 0;
}

bool NvsStore::eraseKey(const char* key) {
  if (!g_open) return false;
  return prefs.remove(key);
}

bool NvsStore::hasKey(const char* key) {
  if (!g_open) return false;
  return prefs.isKey(key);
}

}  // namespace stkchan
