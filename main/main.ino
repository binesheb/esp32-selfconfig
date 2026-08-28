#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <esp_system.h>
#include <esp_ota_ops.h>

// AppHooks.h is provided by the PlatformIO project. When this file is copied
// directly into Arduino IDE, provide the same empty application hooks locally.
#if __has_include("AppHooks.h")
  #include "AppHooks.h"
#else
  inline void applicationSetup() {}
  inline void applicationLoop() {}
#endif

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

static const uint16_t DNS_PORT = 53;
static const uint8_t SETUP_BUTTON_PIN = 0;
static const unsigned long WIFI_TIMEOUT_MS = 15000;
static const unsigned long SETUP_HOLD_MS = 5000;
static const unsigned long FACTORY_RESET_HOLD_MS = 10000;

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

struct Config {
  String deviceName;
  String wifiSsid;
  String wifiPassword;
  String githubOwner;
  String githubRepo;
  String channel;
  bool autoUpdate = true;
};

Config config;
bool setupMode = false;
unsigned long setupButtonStart = 0;

String deviceId() {
  uint64_t chipId = ESP.getEfuseMac();
  char id[24];
  snprintf(id, sizeof(id), "ESP32-%04X", (uint16_t)(chipId & 0xFFFF));
  return String(id);
}

String configurationApSsid() {
  return String("ESP32-SETUP-") + String((uint32_t)(ESP.getEfuseMac() & 0xFFFF), HEX);
}

bool isConfigured() {
  return config.wifiSsid.length() > 0 && config.githubOwner.length() > 0 && config.githubRepo.length() > 0;
}

void loadConfig() {
  preferences.begin("selfconfig", true);
  config.deviceName = preferences.getString("device", deviceId());
  config.wifiSsid = preferences.getString("ssid", "");
  config.wifiPassword = preferences.getString("pass", "");
  config.githubOwner = preferences.getString("owner", "");
  config.githubRepo = preferences.getString("repo", "");
  config.channel = preferences.getString("channel", "stable");
  config.autoUpdate = preferences.getBool("autoupdate", true);
  preferences.end();
}

void saveConfig() {
  preferences.begin("selfconfig", false);
  preferences.putString("device", config.deviceName);
  preferences.putString("ssid", config.wifiSsid);
  preferences.putString("pass", config.wifiPassword);
  preferences.putString("owner", config.githubOwner);
  preferences.putString("repo", config.githubRepo);
  preferences.putString("channel", config.channel == "beta" ? "beta" : "stable");
  preferences.putBool("autoupdate", config.autoUpdate);
  preferences.end();
}

void factoryReset() {
  Serial.println("[CONFIG] Factory reset requested");
  preferences.begin("selfconfig", false);
  preferences.clear();
  preferences.end();
  delay(500);
  ESP.restart();
}

String htmlEscape(const String &value) {
  String s = value;
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  s.replace("'", "&#39;");
  return s;
}

void startSetupPortal() {
  setupMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(configurationApSsid().c_str());
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, []() {
    String page = "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'><title>ESP32 Setup</title></head><body>";
    page += "<h1>ESP32 SelfConfig</h1><form method='post' action='/save'>";
    page += "Device name <input name='device' value='" + htmlEscape(config.deviceName) + "'><br>";
    page += "Wi-Fi SSID <input name='ssid' value='" + htmlEscape(config.wifiSsid) + "'><br>";
    page += "Wi-Fi password <input type='password' name='pass'><br>";
    page += "GitHub owner <input name='owner' value='" + htmlEscape(config.githubOwner) + "'><br>";
    page += "GitHub repository <input name='repo' value='" + htmlEscape(config.githubRepo) + "'><br>";
    page += "Channel <select name='channel'><option value='stable'" + String(config.channel == "stable" ? " selected" : "") + ">Stable</option><option value='beta'" + String(config.channel == "beta" ? " selected" : "") + ">Beta</option></select><br>";
    page += "Auto update <input type='checkbox' name='autoupdate' value='1'" + String(config.autoUpdate ? " checked" : "") + "><br>";
    page += "<button type='submit'>Save & Reboot</button></form></body></html>";
    server.send(200, "text/html", page);
  });

  server.on("/save", HTTP_POST, []() {
    config.deviceName = server.arg("device");
    config.wifiSsid = server.arg("ssid");
    if (server.arg("pass").length() > 0) config.wifiPassword = server.arg("pass");
    config.githubOwner = server.arg("owner");
    config.githubRepo = server.arg("repo");
    config.channel = server.arg("channel") == "beta" ? "beta" : "stable";
    config.autoUpdate = server.hasArg("autoupdate");
    saveConfig();
    server.send(200, "text/html", "<h1>Saved</h1><p>Rebooting…</p>");
    delay(500);
    ESP.restart();
  });

  server.onNotFound([]() { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); });
  server.begin();
}

bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

String releaseApiUrl() {
  if (config.channel == "beta") {
    return "https://api.github.com/repos/" + config.githubOwner + "/" + config.githubRepo + "/releases";
  }
  return "https://api.github.com/repos/" + config.githubOwner + "/" + config.githubRepo + "/releases/latest";
}

bool parseManifest(const String &manifestUrl, String &version, String &firmwareUrl, String &sha256, size_t &size) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, manifestUrl)) return false;
  http.addHeader("User-Agent", "ESP32-SelfConfig");
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, http.getString());
  http.end();
  if (error) return false;
  version = doc["version"].as<String>();
  firmwareUrl = doc["firmware_url"].as<String>();
  sha256 = doc["sha256"].as<String>();
  size = doc["size"].as<size_t>();
  return version.length() > 0 && firmwareUrl.length() > 0 && sha256.length() == 64 && size > 0;
}

bool fetchLatestRelease(String &manifestUrl) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, releaseApiUrl())) return false;
  http.addHeader("User-Agent", "ESP32-SelfConfig");
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  DynamicJsonDocument doc(8192);
  DeserializationError error = deserializeJson(doc, http.getString());
  http.end();
  if (error) return false;
  if (config.channel == "beta") {
    for (JsonObject release : doc.as<JsonArray>()) {
      if (release["prerelease"].as<bool>() && !release["draft"].as<bool>()) {
        for (JsonObject asset : release["assets"].as<JsonArray>()) {
          if (asset["name"].as<String>() == "manifest.json") { manifestUrl = asset["browser_download_url"].as<String>(); return true; }
        }
      }
    }
    return false;
  }
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    if (asset["name"].as<String>() == "manifest.json") { manifestUrl = asset["browser_download_url"].as<String>(); return true; }
  }
  return false;
}

bool isNewerVersion(const String &candidate) {
  int current[3] = {0, 1, 0};
  int next[3] = {0, 0, 0};
  sscanf(APP_VERSION, "%d.%d.%d", &current[0], &current[1], &current[2]);
  sscanf(candidate.c_str(), "v%d.%d.%d", &next[0], &next[1], &next[2]);
  for (int i = 0; i < 3; ++i) {
    if (next[i] != current[i]) return next[i] > current[i];
  }
  return false;
}

bool updateFirmware(const String &url, const String &expectedSha256, size_t expectedSize) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  if (!http.begin(client, url)) return false;
  http.addHeader("User-Agent", "ESP32-SelfConfig");
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }
  if (http.getSize() > 0 && (size_t)http.getSize() != expectedSize) { http.end(); return false; }
  if (!Update.begin(expectedSize, U_FLASH)) { http.end(); return false; }
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[1024];
  size_t total = 0;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  if (mbedtls_sha256_starts_ret(&sha, 0) != 0) { mbedtls_sha256_free(&sha); Update.abort(); http.end(); return false; }
  while (http.connected() || stream->available()) {
    size_t available = stream->available();
    if (!available) { delay(1); continue; }
    size_t read = stream->readBytes(buffer, min(sizeof(buffer), available));
    if (!read) continue;
    if (mbedtls_sha256_update_ret(&sha, buffer, read) != 0 || Update.write(buffer, read) != read) {
      mbedtls_sha256_free(&sha); Update.abort(); http.end(); return false;
    }
    total += read;
  }
  uint8_t digest[32];
  if (mbedtls_sha256_finish_ret(&sha, digest) != 0) { mbedtls_sha256_free(&sha); Update.abort(); http.end(); return false; }
  mbedtls_sha256_free(&sha);
  if (total != expectedSize) { Update.abort(); http.end(); return false; }
  char actualSha256[65];
  for (size_t i = 0; i < sizeof(digest); ++i) sprintf(actualSha256 + (i * 2), "%02x", digest[i]);
  actualSha256[64] = '\0';
  if (!expectedSha256.equalsIgnoreCase(actualSha256)) { Update.abort(); http.end(); return false; }
  bool complete = Update.end();
  http.end();
  return complete && Update.isFinished();
}

void checkForUpdate() {
  if (!config.autoUpdate || WiFi.status() != WL_CONNECTED || !isConfigured()) return;
  String manifestUrl, version, firmwareUrl, sha256;
  size_t size = 0;
  if (!fetchLatestRelease(manifestUrl)) return;
  if (!parseManifest(manifestUrl, version, firmwareUrl, sha256, size)) return;
  if (!isNewerVersion(version)) return;
  Serial.printf("[OTA] Updating to %s\n", version.c_str());
  if (updateFirmware(firmwareUrl, sha256, size)) {
    Serial.println("[OTA] Update verified, rebooting");
    delay(500);
    ESP.restart();
  }
  Serial.println("[OTA] Update failed; continuing current firmware");
}

void handleSetupButton() {
  bool pressed = digitalRead(SETUP_BUTTON_PIN) == LOW;
  if (pressed && setupButtonStart == 0) setupButtonStart = millis();
  if (!pressed && setupButtonStart != 0) {
    unsigned long held = millis() - setupButtonStart;
    setupButtonStart = 0;
    if (held >= FACTORY_RESET_HOLD_MS) factoryReset();
    else if (held >= SETUP_HOLD_MS) startSetupPortal();
  }
}

void setup() {
  pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  loadConfig();
  if (!isConfigured() || !connectWifi()) {
    startSetupPortal();
    return;
  }
  checkForUpdate();
  applicationSetup();
}

void loop() {
  handleSetupButton();
  if (setupMode) {
    dnsServer.processNextRequest();
    server.handleClient();
  } else {
    applicationLoop();
  }
}
