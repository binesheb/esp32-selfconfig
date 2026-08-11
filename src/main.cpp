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
#include "AppHooks.h"

#ifndef APP_VERSION
#define APP_VERSION "0.1.0"
#endif

constexpr uint8_t SETUP_BUTTON = 0;
constexpr unsigned long WIFI_TIMEOUT = 15000;
constexpr unsigned long SETUP_HOLD = 5000;
constexpr unsigned long RESET_HOLD = 10000;

WebServer server(80);
DNSServer dns;
Preferences prefs;

struct Config {
  String deviceName;
  String ssid;
  String password;
  String owner;
  String repo;
  String channel;
  bool autoUpdate;
} cfg;

bool setupMode = false;
unsigned long buttonStarted = 0;

String chipId() {
  uint64_t mac = ESP.getEfuseMac();
  char out[20];
  snprintf(out, sizeof(out), "%04X", (uint16_t)(mac & 0xFFFF));
  return String(out);
}

String setupSsid() { return "ESP32-SETUP-" + chipId(); }

void loadConfig() {
  prefs.begin("selfconfig", true);
  cfg.deviceName = prefs.getString("device", "ESP32-" + chipId());
  cfg.ssid = prefs.getString("ssid", "");
  cfg.password = prefs.getString("pass", "");
  cfg.owner = prefs.getString("owner", "");
  cfg.repo = prefs.getString("repo", "");
  cfg.channel = prefs.getString("channel", "stable");
  cfg.autoUpdate = prefs.getBool("update", true);
  prefs.end();
}

void saveConfig() {
  prefs.begin("selfconfig", false);
  prefs.putString("device", cfg.deviceName);
  prefs.putString("ssid", cfg.ssid);
  prefs.putString("pass", cfg.password);
  prefs.putString("owner", cfg.owner);
  prefs.putString("repo", cfg.repo);
  prefs.putString("channel", cfg.channel == "beta" ? "beta" : "stable");
  prefs.putBool("update", cfg.autoUpdate);
  prefs.end();
}

void factoryReset() {
  Serial.println("[CONFIG] Factory reset");
  prefs.begin("selfconfig", false);
  prefs.clear();
  prefs.end();
  delay(500);
  ESP.restart();
}

String esc(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  return s;
}

String setupPage() {
  String h = R"HTML(<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1"><title>ESP32 SelfConfig</title><style>body{font-family:Arial,sans-serif;background:#f3f5f7;margin:0;padding:18px}.box{max-width:620px;margin:auto;background:#fff;padding:24px;border-radius:16px;box-shadow:0 4px 20px #0001}h1{margin:0 0 5px}label{display:block;font-weight:bold;margin-top:16px}input,select{width:100%;box-sizing:border-box;padding:12px;margin-top:6px;border:1px solid #ccd2d8;border-radius:9px;font-size:16px}button{width:100%;padding:13px;margin-top:22px;border:0;border-radius:9px;font-weight:bold;font-size:16px}.save{background:#17202a;color:white}.reset{background:#eee;color:#a00}.info{background:#f1f3f5;padding:12px;border-radius:9px;margin:16px 0;font-size:14px}</style></head><body><div class="box"><h1>ESP32 SelfConfig</h1><div>Self-provisioning & GitHub OTA</div><div class="info">Device: <b>)HTML";
  h += esc(cfg.deviceName);
  h += R"HTML(</b><br>Firmware: <b>)HTML";
  h += APP_VERSION;
  h += R"HTML(</b><br>Chip: <b>)HTML";
  h += chipId();
  h += R"HTML(</b></div><form method="POST" action="/save"><label>Device Name</label><input name="device" value=")HTML";
  h += esc(cfg.deviceName);
  h += R"HTML("><label>Wi-Fi Network</label><select id="ssid" name="ssid"><option value="">Scanning...</option></select><label>Wi-Fi Password</label><input type="password" name="pass" value=")HTML";
  h += esc(cfg.password);
  h += R"HTML("><label>GitHub Owner</label><input name="owner" placeholder="binesheb" value=")HTML";
  h += esc(cfg.owner);
  h += R"HTML("><label>GitHub Repository</label><input name="repo" placeholder="my-esp32-firmware" value=")HTML";
  h += esc(cfg.repo);
  h += R"HTML("><label>Firmware Channel</label><select name="channel"><option value="stable">Stable</option><option value="beta">Beta / Pre-release</option></select><label><input type="checkbox" name="update" style="width:auto" )HTML";
  h += cfg.autoUpdate ? "checked" : "";
  h += R"HTML(> Check for firmware updates on boot</label><button class="save" type="submit">SAVE & REBOOT</button></form><button class="reset" onclick="if(confirm('Erase all configuration?'))location='/factory-reset'">FACTORY RESET</button><script>fetch('/api/scan').then(r=>r.json()).then(a=>{let s=document.getElementById('ssid');s.innerHTML='<option value="">Select network...</option>';a.forEach(x=>{let o=document.createElement('option');o.value=x.ssid;o.textContent=x.ssid+' ('+x.rssi+' dBm)';if(x.ssid===)HTML";
  h += "JSON.stringify(cfg.ssid)";
  h += R"HTML()o.selected=true;s.appendChild(o)})});</script></div></body></html>)HTML";
  return h;
}

void scanNetworks() {
  int n = WiFi.scanNetworks(false, true);
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; ++i) {
    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
  }
  String out;
  serializeJson(doc, out);
  WiFi.scanDelete();
  server.send(200, "application/json", out);
}

void startPortal() {
  setupMode = true;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(setupSsid().c_str());
  dns.start(53, "*", WiFi.softAPIP());
  server.on("/", HTTP_GET, [](){ server.send(200, "text/html", setupPage()); });
  server.on("/api/scan", HTTP_GET, scanNetworks);
  server.on("/save", HTTP_POST, [](){
    if (server.hasArg("device")) cfg.deviceName = server.arg("device");
    if (server.hasArg("ssid")) cfg.ssid = server.arg("ssid");
    if (server.hasArg("pass")) cfg.password = server.arg("pass");
    if (server.hasArg("owner")) cfg.owner = server.arg("owner");
    if (server.hasArg("repo")) cfg.repo = server.arg("repo");
    if (server.hasArg("channel")) cfg.channel = server.arg("channel");
    cfg.autoUpdate = server.hasArg("update");
    saveConfig();
    server.send(200, "text/html", "<h2>Saved</h2><p>Rebooting...</p>");
    delay(1000);
    ESP.restart();
  });
  server.on("/factory-reset", HTTP_GET, [](){
    server.send(200, "text/html", "<h2>Factory reset</h2><p>Rebooting...</p>");
    delay(500);
    factoryReset();
  });
  server.onNotFound([](){ server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", "Redirecting"); });
  server.begin();
  Serial.printf("[SETUP] SSID: %s\n", setupSsid().c_str());
  Serial.println("[SETUP] IP: 192.168.4.1");
}

bool connectWiFi() {
  if (cfg.ssid.isEmpty()) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid.c_str(), cfg.password.c_str());
  Serial.printf("[WIFI] Connecting to %s", cfg.ssid.c_str());
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT) { delay(250); Serial.print('.'); }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) { Serial.printf("[WIFI] IP: %s\n", WiFi.localIP().toString().c_str()); return true; }
  Serial.println("[WIFI] Connection failed");
  return false;
}

int versionCompare(String a, String b) {
  a.replace("v", ""); b.replace("v", "");
  int a1=0,a2=0,a3=0,b1=0,b2=0,b3=0;
  sscanf(a.c_str(), "%d.%d.%d", &a1,&a2,&a3);
  sscanf(b.c_str(), "%d.%d.%d", &b1,&b2,&b3);
  if(a1!=b1)return a1>b1?1:-1; if(a2!=b2)return a2>b2?1:-1; if(a3!=b3)return a3>b3?1:-1; return 0;
}

bool httpsGet(const String &url, String &body) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  http.addHeader("Accept", "application/vnd.github+json");
  http.addHeader("X-GitHub-Api-Version", "2026-03-10");
  int code = http.GET();
  if (code == HTTP_CODE_OK) body = http.getString();
  http.end();
  return code == HTTP_CODE_OK;
}

bool getRelease(String &release) {
  String base = "https://api.github.com/repos/" + cfg.owner + "/" + cfg.repo;
  if (cfg.channel == "stable") return httpsGet(base + "/releases/latest", release);
  if (!httpsGet(base + "/releases?per_page=20", release)) return false;
  JsonDocument doc;
  if (deserializeJson(doc, release)) return false;
  for (JsonObject r : doc.as<JsonArray>()) {
    if (!r["draft"].as<bool>()) { release.clear(); serializeJson(r, release); return true; }
  }
  return false;
}

bool installFirmware(const String &url, size_t expectedSize, String expectedSha) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  if (code != HTTP_CODE_OK) { Serial.printf("[OTA] HTTP %d\n", code); http.end(); return false; }
  int total = http.getSize();
  if (expectedSize && total > 0 && expectedSize != (size_t)total) { Serial.println("[OTA] Size mismatch"); http.end(); return false; }
  if (!Update.begin(total > 0 ? total : UPDATE_SIZE_UNKNOWN)) { Serial.printf("[OTA] Begin failed: %s\n", Update.errorString()); http.end(); return false; }

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[2048];
  size_t written = 0;
  while (http.connected() && (total < 0 || written < (size_t)total)) {
    size_t available = stream->available();
    if (!available) { delay(1); continue; }
    size_t n = available > sizeof(buffer) ? sizeof(buffer) : available;
    int got = stream->readBytes(buffer, n);
    if (got <= 0) break;
    if (Update.write(buffer, got) != (size_t)got) { Update.abort(); mbedtls_sha256_free(&ctx); http.end(); return false; }
    mbedtls_sha256_update(&ctx, buffer, got);
    written += got;
    if (total > 0 && (written % 32768 < (size_t)got)) Serial.printf("[OTA] %u%%\n", (unsigned)((written * 100ULL) / total));
  }
  uint8_t digest[32];
  mbedtls_sha256_finish(&ctx, digest);
  mbedtls_sha256_free(&ctx);
  String actual;
  char b[3];
  for (uint8_t v : digest) { snprintf(b, sizeof(b), "%02x", v); actual += b; }
  expectedSha.toLowerCase();
  Serial.printf("[OTA] SHA256 %s\n", actual.c_str());
  if (actual != expectedSha || expectedSha.length() != 64) { Serial.println("[OTA] SHA verification failed"); Update.abort(); http.end(); return false; }
  bool ok = Update.end(true);
  http.end();
  if (!ok) { Serial.printf("[OTA] End failed: %s\n", Update.errorString()); return false; }
  Serial.println("[OTA] Update installed");
  return true;
}

bool checkForUpdate() {
  if (cfg.owner.isEmpty() || cfg.repo.isEmpty() || WiFi.status() != WL_CONNECTED) return false;
  String release;
  if (!getRelease(release)) { Serial.println("[OTA] No release available"); return false; }
  JsonDocument doc;
  if (deserializeJson(doc, release)) return false;
  String tag = doc["tag_name"] | "";
  String version = tag;
  String manifestUrl;
  for (JsonObject asset : doc["assets"].as<JsonArray>()) {
    if ((asset["name"] | "") == "manifest.json") { manifestUrl = asset["browser_download_url"] | ""; break; }
  }
  if (manifestUrl.isEmpty()) { Serial.println("[OTA] manifest.json missing"); return false; }

  String manifestText;
  if (!httpsGet(manifestUrl, manifestText)) return false;
  JsonDocument manifest;
  if (deserializeJson(manifest, manifestText)) return false;
  version = manifest["version"] | tag;
  String sha = manifest["sha256"] | "";
  size_t size = manifest["size"] | 0;
  String firmwareUrl = manifest["firmware_url"] | "";
  if (firmwareUrl.isEmpty()) firmwareUrl = "https://github.com/" + cfg.owner + "/" + cfg.repo + "/releases/download/" + tag + "/firmware.bin";

  Serial.printf("[OTA] Current %s / Latest %s\n", APP_VERSION, version.c_str());
  if (versionCompare(version, APP_VERSION) <= 0) return false;
  if (sha.length() != 64) { Serial.println("[OTA] Invalid manifest hash"); return false; }
  if (installFirmware(firmwareUrl, size, sha)) { delay(1000); ESP.restart(); return true; }
  return false;
}

void buttonTask() {
  if (digitalRead(SETUP_BUTTON) != LOW) { buttonStarted = 0; return; }
  if (!buttonStarted) buttonStarted = millis();
  unsigned long held = millis() - buttonStarted;
  if (held >= RESET_HOLD) factoryReset();
  else if (held >= SETUP_HOLD && !setupMode) startPortal();
}

void setup() {
  Serial.begin(115200);
  pinMode(SETUP_BUTTON, INPUT_PULLUP);
  delay(500);
  loadConfig();
  Serial.printf("\n[BOOT] ESP32 SelfConfig %s | %s | %s\n", APP_VERSION, cfg.deviceName.c_str(), chipId().c_str());
  if (!connectWiFi()) startPortal();
  else { if (cfg.autoUpdate) checkForUpdate(); applicationSetup(); }
}

void loop() {
  buttonTask();
  if (setupMode) { dns.processNextRequest(); server.handleClient(); }
  else applicationLoop();
  delay(2);
}
