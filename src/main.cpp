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

String setupSsid() {
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
  return s;
}

String pageHtml() {
  String html = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32 SelfConfig</title>
<style>body{font-family:system-ui,sans-serif;background:#f4f6f8;margin:0;padding:18px;color:#17202a}.card{max-width:650px;margin:auto;background:white;padding:24px;border-radius:16px;box-shadow:0 4px 20px #0001}h1{margin-top:0}label{display:block;font-weight:600;margin-top:16px}input,select{box-sizing:border-box;width:100%;padding:12px;margin-top:6px;border:1px solid #ccd3da;border-radius:9px;font-size:16px}button{width:100%;padding:13px;margin-top:22px;border:0;border-radius:9px;font-size:16px;font-weight:700;cursor:pointer}.primary{background:#17202a;color:white}.danger{background:#eee;color:#a00}.muted{color:#65727e;font-size:14px}.status{padding:10px;border-radius:9px;background:#f0f3f5;margin:14px 0}</style></head><body><div class="card">
<h1>ESP32 SelfConfig</h1><div class="muted">Self-provisioning and GitHub OTA framework</div><div class="status">Device: <b>)HTML";
  html += htmlEscape(config.deviceName);
  html += R"HTML(</b><br>Firmware: <b>)HTML";
  html += APP_VERSION;
  html += R"HTML(</b></div>
<form method="POST" action="/save">
<label>Device Name</label><input name="device" maxlength="48" value=")HTML";
  html += htmlEscape(config.deviceName);
  html += R"HTML(">
<label>Wi-Fi Network</label><select id="ssid" name="ssid"><option value="">Select network...</option></select>
<label>Wi-Fi Password</label><input type="password" name="pass" value=")HTML";
  html += htmlEscape(config.wifiPassword);
  html += R"HTML(">
<label>GitHub Owner</label><input name="owner" placeholder="binesheb" value=")HTML";
  html += htmlEscape(config.githubOwner);
  html += R"HTML(">
<label>GitHub Repository</label><input name="repo" placeholder="my-esp32-firmware" value=")HTML";
  html += htmlEscape(config.githubRepo);
  html += R"HTML(">
<label>Firmware Channel</label><select name="channel"><option value="stable" )HTML";
  html += config.channel == "stable" ? "selected" : "";
  html += R"HTML(>Stable</option><option value="beta" )HTML";
  html += config.channel == "beta" ? "selected" : "";
  html += R"HTML(>Beta / Pre-release</option></select>
<label><input style="width:auto" type="checkbox" name="autoupdate" )HTML";
  html += config.autoUpdate ? "checked" : "";
  html += R"HTML(> Automatically check for updates after boot</label>
<button class="primary" type="submit">Save &amp; Reboot</button></form>
<button class="danger" onclick="if(confirm('Erase all saved configuration?'))location.href='/factory-reset'">Factory Reset</button>
<script>
async function scan(){try{let r=await fetch('/api/scan');let a=await r.json();let s=document.getElementById('ssid');s.innerHTML='<option value="">Select network...</option>';a.forEach(x=>{let o=document.createElement('option');o.value=x.ssid;o.textContent=x.ssid+' ('+x.rssi+' dBm)';if(x.ssid===decodeURIComponent(')HTML";
  html += WiFi.SSID();
  html += R"HTML('))o.selected=true;s.appendChild(o)})}catch(e){console.log(e)}}scan();
</script></div></body></html>)HTML";
  return html;
}

void handleRoot() { server.send(200, "text/html", pageHtml()); }

void handleScan() {
  int n = WiFi.scanNetworks(false, true);
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
  }
  String output;
  serializeJson(doc, output);
  WiFi.scanDelete();
  server.send(200, "application/json", output);
}

void handleSave() {
  if (server.hasArg("device")) config.deviceName = server.arg("device");
  if (server.hasArg("ssid")) config.wifiSsid = server.arg("ssid");
  if (server.hasArg("pass")) config.wifiPassword = server.arg("pass");
  if (server.hasArg("owner")) config.githubOwner = server.arg("owner");
  if (server.hasArg("repo")) config.githubRepo = server.arg("repo");
  if (server.hasArg("channel")) config.channel = server.arg("channel");
  config.autoUpdate = server.hasArg("autoupdate");
  saveConfig();
  server.send(200, "text/html", "<html><body><h2>Saved.</h2><p>Rebooting...</p></body></html>");
  delay(1200);
  ESP.restart();
}

void handleFactoryReset() {
  server.send(200, "text/html", "<html><body><h2>Factory reset</h2><p>Rebooting...</p></body></html>");
  delay(700);
  factoryReset();
}

void handleNotFound() {
  if (setupMode) {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "Redirecting");
  } else server.send(404, "text/plain", "Not found");
}

void startSetupPortal() {
  setupMode = true;
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.mode(WIFI_AP);
  String ssid = setupSsid();
  WiFi.softAP(ssid.c_str());
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/factory-reset", HTTP_GET, handleFactoryReset);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("[SETUP] Configuration portal started");
  Serial.print("[SETUP] SSID: "); Serial.println(ssid);
  Serial.print("[SETUP] IP:   "); Serial.println(WiFi.softAPIP());
}

bool connectWiFi() {
  if (config.wifiSsid.isEmpty()) return false;
  Serial.printf("[WIFI] Connecting to %s", config.wifiSsid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS) { delay(250); Serial.print("."); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[WIFI] Connected. IP: "); Serial.println(WiFi.localIP());
    return true;
  }
  Serial.println("[WIFI] Connection failed");
  return false;
}

int compareVersions(String a, String b) {
  a.replace("v", ""); b.replace("v", "");
  int a1=0,a2=0,a3=0,b1=0,b2=0,b3=0;
  sscanf(a.c_str(), "%d.%d.%d", &a1,&a2,&a3);
  sscanf(b.c_str(), "%d.%d.%d", &b1,&b2,&b3);
  if (a1 != b1) return a1 > b1 ? 1 : -1;
  if (a2 != b2) return a2 > b2 ? 1 : -1;
  if (a3 != b3) return a3 > b3 ? 1 : -1;
  return 0;
}

bool httpsGetString(const String &url, String &payload) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("X-GitHub-Api-Version", "2026-03-10");
  int code = http.GET();
  if (code == HTTP_CODE_OK) payload = http.getString();
  http.end();
  return code == HTTP_CODE_OK;
}

bool findRelease(String &releaseJson) {
  String base = "https://api.github.com/repos/" + config.githubOwner + "/" + config.githubRepo;
  if (config.channel == "stable") return httpsGetString(base + "/releases/latest", releaseJson);
  if (!httpsGetString(base + "/releases?per_page=20", releaseJson)) return false;
  JsonDocument listDoc;
  if (deserializeJson(listDoc, releaseJson)) return false;
  JsonArray releases = listDoc.as<JsonArray>();
  for (JsonObject r : releases) {
    if (!r["draft"].as<bool>()) { releaseJson = ""; serializeJson(r, releaseJson); return true; }
  }
  return false;
}

bool downloadAndInstall(const String &firmwareUrl, size_t expectedSize, const String &expectedSha) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  Serial.println("[OTA] Downloading firmware...");
  if (!http.begin(client, firmwareUrl)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { Serial.printf("[OTA] Download HTTP error: %d\n", code); http.end(); return false; }
  int total = http.getSize();
  if (expectedSize > 0 && total > 0 && expectedSize != (size_t)total) { Serial.printf("[OTA] Size mismatch: manifest=%u download=%d\n", (unsigned)expectedSize, total); http.end(); return false; }
  if (!Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN)) { Serial.printf("[OTA] Update.begin failed: %s\n", Update.errorString()); http.end(); return false; }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts(&sha, 0);
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[2048];
  size_t written = 0;
  int lastPercent = -1;

  while (http.connected() && (total < 0 || written < (size_t)total)) {
    size_t available = stream->available();
    if (!available) { delay(1); continue; }
    size_t toRead = available > sizeof(buffer) ? sizeof(buffer) : available;
    int len = stream->readBytes(buffer, toRead);
    if (len <= 0) break;
    if (Update.write(buffer, len) != (size_t)len) { Serial.printf("[OTA] Write failed: %s\n", Update.errorString()); Update.abort(); mbedtls_sha256_free(&sha); http.end(); return false; }
    mbedtls_sha256_update(&sha, buffer, len);
    written += len;
    if (total > 0) {
      int percent = (int)((written * 100ULL) / total);
      if (percent != lastPercent && percent % 5 == 0) { Serial.printf("[OTA] Progress: %d%%\n", percent); lastPercent = percent; }
    }
  }

  uint8_t digest[32];
  mbedtls_sha256_finish(&sha, digest);
  mbedtls_sha256_free(&sha);
  String actualSha;
  char hex[3];
  for (uint8_t b : digest) { snprintf(hex, sizeof(hex), "%02x", b); actualSha += hex; }
  String expected = expectedSha;
  expected.toLowerCase();
  Serial.print("[OTA] SHA-256: "); Serial.println(actualSha);
  if (expected.length() != 64 || actualSha != expected) { Serial.println("[OTA] SHA-256 verification FAILED"); Update.abort(); http.end(); return false; }
  if (!Update.end(true)) { Serial.printf("[OTA] Update.end failed: %s\n", Update.errorString()); http.end(); return false; }
  http.end();
  Serial.println("[OTA] Firmware installed successfully");
  return true;
}

bool checkForUpdate() {
  if (!isConfigured() || WiFi.status() != WL_CONNECTED) return false;
  Serial.printf("[OTA] Checking %s/%s (%s)\n", config.githubOwner.c_str(), config.githubRepo.c_str(), config.channel.c_str());
  String releaseJson;
  if (!findRelease(releaseJson)) { Serial.println("[OTA] No usable GitHub release found"); return false; }
  JsonDocument releaseDoc;
  if (deserializeJson(releaseDoc, releaseJson)) { Serial.println("[OTA] Invalid release JSON"); return false; }
  String tag = releaseDoc["tag_name"] | "";
  String releaseChannel = releaseDoc["prerelease"].as<bool>() ? "beta" : "stable";
  if (config.channel == "stable" && releaseChannel != "stable") return false;

  JsonArray assets = releaseDoc["assets"].as<JsonArray>();
  String manifestUrl;
  for (JsonObject asset : assets) {
    String name = asset["name"] | "";
    if (name == "manifest.json") { manifestUrl = asset["browser_download_url"] | ""; break; }
  }
  if (manifestUrl.isEmpty()) { Serial.println("[OTA] Release has no manifest.json asset"); return false; }
  String manifestJson;
  if (!httpsGetString(manifestUrl, manifestJson)) return false;
  JsonDocument manifest;
  if (deserializeJson(manifest, manifestJson)) return false;
  String latest = manifest["version"] | tag;
  String sha = manifest["sha256"] | "";
  String firmwareUrl = manifest["firmware_url"] | "";
  size_t firmwareSize = manifest["size"] | 0;
  if (firmwareUrl.isEmpty()) firmwareUrl = "https://github.com/" + config.githubOwner + "/" + config.githubRepo + "/releases/download/" + tag + "/firmware.bin";
  Serial.printf("[OTA] Current=%s Latest=%s\n", APP_VERSION, latest.c_str());
  if (compareVersions(latest, APP_VERSION) <= 0) { Serial.println("[OTA] Firmware is up to date"); return false; }
  if (sha.length() != 64) { Serial.println("[OTA] Invalid SHA-256 in manifest"); return false; }
  if (downloadAndInstall(firmwareUrl, firmwareSize, sha)) { delay(1000); ESP.restart(); return true; }
  return false;
}

void checkSetupButton() {
  bool pressed = digitalRead(SETUP_BUTTON_PIN) == LOW;
  if (!pressed) { setupButtonStart = 0; return; }
  if (setupButtonStart == 0) setupButtonStart = millis();
  unsigned long held = millis() - setupButtonStart;
  if (held >= FACTORY_RESET_HOLD_MS) factoryReset();
  else if (held >= SETUP_HOLD_MS && !setupMode) startSetupPortal();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);
  loadConfig();
  Serial.println();
  Serial.println("========================================");
  Serial.println("ESP32 SelfConfig");
  Serial.printf("Firmware: %s\n", APP_VERSION);
  Serial.printf("Device:   %s\n", config.deviceName.c_str());
  Serial.printf("ID:       %s\n", deviceId().c_str());
  Serial.println("========================================");
  if (!isConfigured() || !connectWiFi()) startSetupPortal();
  else { if (config.autoUpdate) checkForUpdate(); applicationSetup(); }
}

void loop() {
  checkSetupButton();
  if (setupMode) { dnsServer.processNextRequest(); server.handleClient(); }
  else applicationLoop();
  delay(2);
}
