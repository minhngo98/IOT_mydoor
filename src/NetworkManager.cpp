#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <cstring>
#include <time.h>
#include "Config.h"
#include "NetworkManager.h"
#ifdef USE_LOCAL_WEB_STACK
#include "WebUI.h"
#endif

#ifdef USE_BLYNK
#include "BlynkSimpleEsp32_SSL_Bounded.h"
#endif

#ifdef USE_RAINMAKER
static const char *TAG = "RainMakerManager";
#endif

// Do lỗi macro trùng lặp HTTP_GET/POST giữa AsyncWebServer và thư viện WebServer (của ElegantOTA),
// ta dùng trực tiếp webrequestmethod
#define ASYNC_GET HTTP_GET
#define ASYNC_POST HTTP_POST

NetworkManager netManager;

namespace {
String buildDeviceId() {
  uint64_t chipId = ESP.getEfuseMac();
  char buffer[13];
  snprintf(buffer, sizeof(buffer), "%04X%08X", static_cast<uint16_t>(chipId >> 32), static_cast<uint32_t>(chipId));
  return String(buffer);
}

String buildRescueSsid(const String& deviceId) {
  String suffix = deviceId;
  if (suffix.length() > 6) {
    suffix = suffix.substring(suffix.length() - 6);
  }
  return "MyDoor-" + suffix;
}

String generateSecret(size_t length) {
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  String secret;
  secret.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    secret += alphabet[esp_random() % (sizeof(alphabet) - 1)];
  }
  return secret;
}

bool isStrongAdminInput(const String& user, const String& pass) {
  return user.length() >= 4 && user.length() <= 32 && pass.length() >= 8 && pass.length() <= 64;
}

String credentialMask(size_t length) {
  String mask;
  mask.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    mask += '*';
  }
  return mask;
}

String maskBlynk(const String& input) {
  if (input.length() <= 6) return input;
  String masked = input.substring(0, 3);
  for (size_t i = 3; i < input.length() - 3; ++i) {
    masked += '*';
  }
  masked += input.substring(input.length() - 3);
  return masked;
}

bool hasSpecialChar(const String& s) {
    for (size_t i = 0; i < s.length(); ++i) {
        if (!isalnum(s.charAt(i))) {
            return true;
        }
    }
    return false;
}

#ifdef USE_LOCAL_WEB_STACK
void sendHtml(AsyncWebServerRequest* request, const char* html) {
  request->send(200, "text/html", reinterpret_cast<const uint8_t*>(html), strlen(html));
}
#endif

String normalizeLogField(const String& input) {
  String out = input;
  out.replace("\n", " ");
  out.replace("\r", " ");
  out.replace("|", "/");
  return out;
}

bool parsePersistentRecord(const String& line, time_t& epochOut, String& tagOut, String& messageOut) {
  int p1 = line.indexOf('|');
  if (p1 < 0) return false;
  int p2 = line.indexOf('|', p1 + 1);
  if (p2 < 0) return false;
  int p3 = line.indexOf('|', p2 + 1);
  if (p3 < 0) return false;

  epochOut = static_cast<time_t>(line.substring(0, p1).toInt());
  tagOut = line.substring(p2 + 1, p3);
  messageOut = line.substring(p3 + 1);
  return true;
}
}

// ISR Handler cho nút BOOT (Phải đặt ở ngoài class)
void IRAM_ATTR isr_config_button() {
  netManager.handleInterruptConfig();
}

void IRAM_ATTR isr_reset_button() {
  netManager.handleInterruptReset();
}

NetworkManager::NetworkManager()
#ifdef USE_LOCAL_WEB_STACK
  : server(80), isApMode(false), isConnected(false),
#else
  : isApMode(false), isConnected(false),
#endif
  lastWiFiCheck(0), apStartTime(0), apOfflineTime(0), wifiLostTime(0), wifiLostFlag(false),
  failedAuthCount(0), lockoutStartTime(0), isLockedOut(false), interruptConfigTriggered(false),
  interruptResetTriggered(false), configPressActive(false), configPressStart(0), lastConfigDebounce(0),
  resetPressActive(false), resetPressStart(0), lastResetDebounce(0), claimRequired(false), webServerInitialized(false), isFirstBoot(false),
  otaInitialized(false), lastBlynkConnectAttempt(0), blynkReconnectBackoffMs(BLYNK_RECONNECT_BASE_MS),
  blynkRemoteGuardUntil(0), blynkWasConnected(false), blynkInvalidToken(false), logIndex(0), lastBlynkSyncLogIndex(0),
  persistentLogs(""), pendingPersistentLogCount(0), lastPersistentFlushMs(0), isOtaRunning(false), pendingReboot(false), rebootTime(0), lastRestartAt(0),
  resetFactoryPending(false), faultLedBlinkState(false), faultLedLastToggle(0), faultLedFlashRemainingToggles(0), faultLedFlashDeadline(0),
  ledWifiState(false), ledReadyState(false), ledFaultState(false), apManualMode(false), pendingApAction(0),
  powerOverrideActive(false), lightOverrideActive(false), scheduleStateInitialized(false), lastPowerScheduleActive(false), lastLightScheduleActive(false) {
    stringMutex = NULL;
#ifdef USE_RAINMAKER
    rainmakerNode = NULL;
    doorDevice = NULL;
    powerBoxDevice = NULL;
    lightDevice = NULL;
    rainmakerDoorState = "STOPPED";
    rainmakerInitialized = false;
    wifiEventGroup = xEventGroupCreate();
#endif
}

String NetworkManager::safeGetString(const String& str) {
    String copy;
    if (xSemaphoreTake(stringMutex, pdMS_TO_TICKS(100))) {
        copy = str;
        xSemaphoreGive(stringMutex);
    } else {
        Serial.println("[MUTEX] Timeout getting safe string");
    }
    return copy;
}

void NetworkManager::safeSetString(String& target, const String& value) {
    if (xSemaphoreTake(stringMutex, pdMS_TO_TICKS(100))) {
        target = value;
        xSemaphoreGive(stringMutex);
    } else {
        Serial.println("[MUTEX] Timeout setting safe string");
    }
}

String NetworkManager::detectLogTag(const String& message) const {
    String upper = message;
    upper.toUpperCase();

    if (upper.indexOf("[AUTO]") >= 0 || upper.indexOf("AUTO") >= 0 || upper.indexOf("DEN GIO") >= 0) {
        return "AUTO";
    }
    if (upper.indexOf(" BAT") >= 0 || upper.startsWith("BAT") || upper.indexOf(": ON") >= 0 || upper.indexOf(" ON ") >= 0 || upper.endsWith(" ON")) {
        return "ON";
    }
    if (upper.indexOf(" TAT") >= 0 || upper.startsWith("TAT") || upper.indexOf(": OFF") >= 0 || upper.indexOf(" OFF ") >= 0 || upper.endsWith(" OFF")) {
        return "OFF";
    }
    return "INFO";
}

String NetworkManager::formatLogWithTag(const String& message, const String& tag, time_t epoch) const {
    String timeStr = "[--:--:--]";
    if (epoch > 0) {
        struct tm tmInfo;
        if (localtime_r(&epoch, &tmInfo) != nullptr) {
            char buf[16];
            snprintf(buf, sizeof(buf), "[%02d:%02d:%02d]", tmInfo.tm_hour, tmInfo.tm_min, tmInfo.tm_sec);
            timeStr = String(buf);
        }
    }
    return "[" + tag + "] " + timeStr + " " + message;
}

void NetworkManager::pruneLogsOlderThan3Days(String& blob) const {
    if (blob.length() == 0) return;

    time_t nowEpoch = time(nullptr);
    if (nowEpoch < 100000) {
        return;
    }

    const time_t cutoff = nowEpoch - static_cast<time_t>(LOG_RETENTION_SEC);
    String kept;
    kept.reserve(blob.length());

    int start = 0;
    while (start < static_cast<int>(blob.length())) {
        int end = blob.indexOf('\n', start);
        if (end < 0) end = blob.length();

        String line = blob.substring(start, end);
        time_t epoch;
        String tag;
        String msg;
        bool parsed = parsePersistentRecord(line, epoch, tag, msg);
        if (!parsed || epoch <= 0 || epoch >= cutoff) {
            kept += line;
            kept += "\n";
        }

        start = end + 1;
    }

    blob = kept;
}

void NetworkManager::rebuildRuntimeLogsFromPersistent() {
    for (int i = 0; i < 15; ++i) {
        eventLogs[i] = "";
    }
    logIndex = 0;
    lastBlynkSyncLogIndex = 0;

    int start = 0;
    while (start < static_cast<int>(persistentLogs.length())) {
        int end = persistentLogs.indexOf('\n', start);
        if (end < 0) end = persistentLogs.length();
        String line = persistentLogs.substring(start, end);

        time_t epoch;
        String tag;
        String msg;
        if (parsePersistentRecord(line, epoch, tag, msg)) {
            eventLogs[logIndex] = formatLogWithTag(msg, tag, epoch);
            logIndex = (logIndex + 1) % 15;
            if (logIndex == lastBlynkSyncLogIndex) {
                lastBlynkSyncLogIndex = (lastBlynkSyncLogIndex + 1) % 15;
            }
        }

        start = end + 1;
    }
    lastBlynkSyncLogIndex = logIndex;
}

void NetworkManager::loadPersistentLogs() {
    Preferences p;
    p.begin("mydoor_logs", true);
    persistentLogs = p.getString("lines", "");
    p.end();

    pruneLogsOlderThan3Days(persistentLogs);

    while (persistentLogs.length() > static_cast<int>(LOG_PERSISTENT_MAX_BYTES)) {
        int firstNewLine = persistentLogs.indexOf('\n');
        if (firstNewLine < 0) {
            persistentLogs = "";
            break;
        }
        persistentLogs = persistentLogs.substring(firstNewLine + 1);
    }

    rebuildRuntimeLogsFromPersistent();
}

void NetworkManager::appendPersistentLogLine(time_t epoch, const String& tag, const String& message) {
    String normalizedTag = normalizeLogField(tag);
    String normalizedMsg = normalizeLogField(message);
    String line = String(static_cast<unsigned long>(epoch > 0 ? epoch : 0)) + "|INFO|" + normalizedTag + "|" + normalizedMsg;

    persistentLogs += line;
    persistentLogs += "\n";
    pendingPersistentLogCount++;

    pruneLogsOlderThan3Days(persistentLogs);

    while (persistentLogs.length() > static_cast<int>(LOG_PERSISTENT_MAX_BYTES)) {
        int firstNewLine = persistentLogs.indexOf('\n');
        if (firstNewLine < 0) {
            persistentLogs = "";
            break;
        }
        persistentLogs = persistentLogs.substring(firstNewLine + 1);
    }
}

void NetworkManager::flushLogsToNvsIfNeeded(bool force) {
    unsigned long now = millis();
    bool shouldFlush = force || pendingPersistentLogCount >= LOG_FLUSH_BATCH_COUNT || (pendingPersistentLogCount > 0 && (now - lastPersistentFlushMs >= LOG_FLUSH_INTERVAL_MS));
    if (!shouldFlush) return;

    String snapshot;
    if (!xSemaphoreTake(stringMutex, pdMS_TO_TICKS(150))) {
        return;
    }
    snapshot = persistentLogs;
    pendingPersistentLogCount = 0;
    lastPersistentFlushMs = now;
    xSemaphoreGive(stringMutex);

    Preferences p;
    p.begin("mydoor_logs", false);
    p.putString("lines", snapshot);
    p.end();
}

void NetworkManager::logEvent(const String& message) {
    Serial.println(message);

    time_t epoch = time(nullptr);
    if (epoch < 100000) {
        epoch = 0;
    }

    String tag = detectLogTag(message);
    String display = formatLogWithTag(message, tag, epoch);

    if (xSemaphoreTake(stringMutex, pdMS_TO_TICKS(150))) {
        eventLogs[logIndex] = display;
        logIndex = (logIndex + 1) % 15;
        if (logIndex == lastBlynkSyncLogIndex) {
            lastBlynkSyncLogIndex = (lastBlynkSyncLogIndex + 1) % 15;
        }

        appendPersistentLogLine(epoch, tag, message);
        xSemaphoreGive(stringMutex);
    } else {
        Serial.println("[MUTEX] Timeout logging event");
    }
}

void NetworkManager::syncLogsToCloud() {
    if (lastBlynkSyncLogIndex == logIndex) return;

    if (xSemaphoreTake(stringMutex, pdMS_TO_TICKS(100))) {
        while (lastBlynkSyncLogIndex != logIndex) {
            String logLine = eventLogs[lastBlynkSyncLogIndex];
#ifdef USE_BLYNK
            if (Blynk.connected()) {
                Blynk.virtualWrite(VPIN_TERMINAL, logLine + "\n");
            }
#endif
            lastBlynkSyncLogIndex = (lastBlynkSyncLogIndex + 1) % 15;
        }
        xSemaphoreGive(stringMutex);
    }
}

String NetworkManager::renderPersistentLogsForClient() const {
    String snapshot;
    if (!xSemaphoreTake(stringMutex, pdMS_TO_TICKS(100))) {
        return "[System] Dang dong bo log, vui long thu lai...\n";
    }
    snapshot = persistentLogs;
    xSemaphoreGive(stringMutex);

    if (snapshot.length() == 0) return "";

    String output;
    output.reserve(snapshot.length() + 128);

    int start = 0;
    while (start < static_cast<int>(snapshot.length())) {
        int end = snapshot.indexOf('\n', start);
        if (end < 0) end = snapshot.length();

        String line = snapshot.substring(start, end);
        time_t epoch;
        String tag;
        String msg;
        if (parsePersistentRecord(line, epoch, tag, msg)) {
            output += formatLogWithTag(msg, tag, epoch);
            output += "\n";
        }
        start = end + 1;
    }

    return output;
}

String NetworkManager::getRecentLogs() const {
    return renderPersistentLogsForClient();
}

String NetworkManager::getPublicLogs() const {
    return renderPersistentLogsForClient();
}

void NetworkManager::replayLogsToBlynk() {
#ifdef USE_BLYNK
    if (!Blynk.connected()) return;

    String snapshot;
    if (!xSemaphoreTake(stringMutex, pdMS_TO_TICKS(150))) {
        return;
    }
    snapshot = persistentLogs;
    lastBlynkSyncLogIndex = logIndex;
    xSemaphoreGive(stringMutex);

    int start = 0;
    while (start < static_cast<int>(snapshot.length())) {
        int end = snapshot.indexOf('\n', start);
        if (end < 0) end = snapshot.length();
        String line = snapshot.substring(start, end);

        time_t epoch;
        String tag;
        String msg;
        if (parsePersistentRecord(line, epoch, tag, msg)) {
            Blynk.virtualWrite(VPIN_TERMINAL, formatLogWithTag(msg, tag, epoch) + "\n");
        }

        start = end + 1;
    }
#endif
}

void NetworkManager::handleInterruptConfig() {
  interruptConfigTriggered = true;
}

void NetworkManager::handleInterruptReset() {
  interruptResetTriggered = true;
}

void NetworkManager::begin() {
  if (stringMutex == NULL) {
      stringMutex = xSemaphoreCreateMutex();
  }

  lastPersistentFlushMs = millis();
  loadPersistentLogs();

  pinMode(PIN_BTN_CONFIG, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_CONFIG), isr_config_button, FALLING);

  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_RESET), isr_reset_button, FALLING);
  loadConfig();

#ifdef USE_RAINMAKER
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  ESP_ERROR_CHECK(esp_netif_init());
  wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(RMAKER_EVENT, ESP_EVENT_ANY_ID, &rainmaker_event_handler, NULL));

  setupRainMaker();
  startRainMakerProvisioning();
#endif

#ifndef USE_RAINMAKER
  // Mạng WiFi chưa được thiết lập, chạy chế độ Access Point
  if (ssid == "" || isFirstBoot) {
    Serial.println("[WIFI] Thiet bi chua san sang van hanh day du. Vao Rescue AP (10.10.10.1)...");
    setupAP();
  } else {
    setupSTA();
  }
#endif

#ifdef USE_LOCAL_WEB_STACK
  setupWebServer();
#endif
}

void NetworkManager::loadConfig() {
  preferences.begin("mydoor", false); // Két sắt NVS tên "mydoor"

  deviceId = buildDeviceId();
  if (preferences.getString("device_id", "") == "") {
    preferences.putString("device_id", deviceId);
  }

  ssid = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");
  ssid2 = preferences.getString("ssid2", "");
  pass2 = preferences.getString("pass2", "");

  adminUser = preferences.getString("admin_user", "");
  adminPass = preferences.getString("admin_pass", "");

  blynkTemplate = preferences.getString("blynkTemplate", "");
  blynkName = preferences.getString("blynkName", "");
  blynkAuth = preferences.getString("blynkAuth", "");

  rescueApSsid = preferences.getString("rescue_ssid", "");
  rescueApPass = preferences.getString("rescue_pass", "");
  bool rescueCustomized = preferences.getBool("rescue_customized", false);

  timezone = preferences.getChar("timezone", 7); // Mặc định UTC+7
  onHour = preferences.getUChar("onHour", 6); // Mặc định bật 6h sáng
  onMin = preferences.getUChar("onMin", 0);
  offHour = preferences.getUChar("offHour", 23); // Tắt 23h đêm
  offMin = preferences.getUChar("offMin", 0);
  scheduleDays = preferences.getUChar("days", 127); // Mặc định cả tuần (1111111 = 127)

  lightOnHour = preferences.getUChar("l_onHour", 18); // Đèn bật 18h tối
  lightOnMin = preferences.getUChar("l_onMin", 0);
  lightOffHour = preferences.getUChar("l_offHour", 5); // Đèn tắt 5h sáng
  lightOffMin = preferences.getUChar("l_offMin", 0);
  lightScheduleDays = preferences.getUChar("lightScheduleDays", 127);

  // Xử lý cờ First Boot và tương thích ngược
  if (ssid == "") {
      // Máy mới tinh hoặc đã factory reset (chưa có Wi-Fi)
      if (adminUser == "" || adminPass == "") {
          isFirstBoot = true;
          Serial.println("[SECURITY] Thiet bi moi: Yeu cau tao tai khoan Admin!");
      } else {
          isFirstBoot = false;
      }
  } else {
      // Máy đã có Wi-Fi (nâng cấp từ bản cũ)
      if (adminUser == "" || adminPass == "") {
          Serial.println("[SECURITY] Phat hien Firmware cu nang cap chua co Admin. Se yeu cau claim lai.");
      }
      isFirstBoot = false; // Đã có Wi-Fi thì không bao giờ là First Boot
  }

  String persistedAdminUser = preferences.getString("admin_user", "");
  String persistedAdminPass = preferences.getString("admin_pass", "");
  adminUser = persistedAdminUser;
  adminPass = persistedAdminPass;
  claimRequired = (adminUser == "" || adminPass == "");
  isFirstBoot = claimRequired;

  bool rescueSsidEmpty = (rescueApSsid == "");
  bool rescuePassWeak = rescueApPass.length() < 8;
  bool rescueSsidLegacy = rescueApSsid.equalsIgnoreCase("esp32");
  bool shouldForceDefaultRescue = !rescueCustomized && (rescueSsidEmpty || rescuePassWeak || rescueSsidLegacy || claimRequired);

  if (shouldForceDefaultRescue) {
    rescueApSsid = DEFAULT_RESCUE_AP_SSID;
    rescueApPass = DEFAULT_RESCUE_AP_PASS;
    preferences.putString("rescue_ssid", rescueApSsid);
    preferences.putString("rescue_pass", rescueApPass);
    preferences.putBool("rescue_customized", false);
  } else {
    if (rescueSsidEmpty) {
      rescueApSsid = DEFAULT_RESCUE_AP_SSID;
      preferences.putString("rescue_ssid", rescueApSsid);
    }
    if (rescuePassWeak) {
      rescueApPass = DEFAULT_RESCUE_AP_PASS;
      preferences.putString("rescue_pass", rescueApPass);
    }
  }

  if (rescueCustomized && rescueSsidEmpty) {
    preferences.putBool("rescue_customized", false);
  }

  preferences.end();

  if (claimRequired) {
    if (ssid == "") {
      Serial.println("[SECURITY] Thiet bi moi: bat buoc tao Admin truoc khi vao che do van hanh.");
    } else {
      Serial.println("[SECURITY] Firmware nang cap chua co Admin. Bat buoc claim lai qua Rescue AP.");
    }
  }

  Serial.println("[NVS] Da tai cau hinh: WiFi=" + ssid + ", DeviceID=" + deviceId + ", RescueAP=" + rescueApSsid);
  Serial.println("[SECURITY] Da tai thong tin Rescue AP (mat khau duoc an).");
}

void NetworkManager::setupAP() {
  isApMode = true;
#ifdef USE_BLYNK
  Blynk.disconnect();
  resetBlynkSessionState();
#endif
  WiFi.mode(WIFI_AP_STA);

  // Set IP tĩnh cho Captive Portal: 10.10.10.1
  IPAddress local_ip(10, 10, 10, 1);
  IPAddress gateway(10, 10, 10, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_ip, gateway, subnet);

  WiFi.softAP(rescueApSsid.c_str(), rescueApPass.c_str(), 1, 0);
  Serial.printf("[AP] Rescue AP dang hoat dong. SSID: %s, PASS: %s, IP: 10.10.10.1\n", rescueApSsid.c_str(), rescueApPass.c_str());

  if (ssid != "") {
      WiFi.begin(ssid.c_str(), password.c_str());
  }

  apStartTime = millis();
  setupWebServer();
}

void NetworkManager::enableRescueAp(const char* reason) {
  isLockedOut = false;
  failedAuthCount = 0;
  if (reason != nullptr && reason[0] != '\0') {
      Serial.printf("[AP] Bat Rescue AP: %s\n", reason);
  }
  if (!isApMode) {
      setupAP();
      return;
  }
  apStartTime = millis();
}

void NetworkManager::disableRescueAp(const char* reason) {
  if (!isApMode) {
      return;
  }

  WiFi.softAPdisconnect(true);
  isApMode = false;
  apManualMode = false;
  if (reason != nullptr && reason[0] != '\0') {
      Serial.printf("[AP] Tat Rescue AP: %s\n", reason);
  }

  if (ssid != "") {
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid.c_str(), password.c_str());
  } else {
      WiFi.mode(WIFI_AP_STA);
  }

#ifdef USE_BLYNK
  resetBlynkSessionState();
#endif
}

void NetworkManager::toggleRescueAp(const char* reason) {
  if (isApMode) {
      disableRescueAp(reason);
  } else {
      enableRescueAp(reason);
  }
}

void NetworkManager::requestApEnable(bool manualMode, const char* reason) {
  apManualMode = manualMode;
  if (reason != nullptr && reason[0] != '\0') {
      Serial.printf("[AP] Queue bat AP: %s\n", reason);
  }
  pendingApAction = 1;
}

void NetworkManager::requestApDisable(const char* reason) {
  if (reason != nullptr && reason[0] != '\0') {
      Serial.printf("[AP] Queue tat AP: %s\n", reason);
  }
  pendingApAction = 2;
}

void NetworkManager::processPendingApAction() {
  if (pendingApAction == 1) {
      enableRescueAp("Queued AP ON");
      pendingApAction = 0;
      return;
  }

  if (pendingApAction == 2) {
      disableRescueAp("Queued AP OFF");
      apManualMode = false;
      pendingApAction = 0;
  }
}

void NetworkManager::startFaultLedFlash(uint8_t pulses) {
  if (pulses == 0) return;
  faultLedFlashRemainingToggles = static_cast<uint8_t>(pulses * 2);
  faultLedLastToggle = millis();
  faultLedFlashDeadline = faultLedLastToggle + 120;
  faultLedBlinkState = true;
  digitalWrite(PIN_LED_WARN, LED_ON);
}

void NetworkManager::updateFaultLed(unsigned long now) {
  if (faultLedFlashRemainingToggles > 0) {
      if (now >= faultLedFlashDeadline) {
          faultLedBlinkState = !faultLedBlinkState;
          digitalWrite(PIN_LED_WARN, faultLedBlinkState ? LED_ON : LED_OFF);
          faultLedFlashRemainingToggles--;
          faultLedFlashDeadline = now + 120;
      }
      return;
  }

  if (isApMode) {
      if (now - faultLedLastToggle >= 300) {
          faultLedLastToggle = now;
          faultLedBlinkState = !faultLedBlinkState;
          digitalWrite(PIN_LED_WARN, faultLedBlinkState ? LED_ON : LED_OFF);
      }
  } else {
      faultLedBlinkState = false;
      digitalWrite(PIN_LED_WARN, LED_OFF);
  }
}

void NetworkManager::updateStatusLeds() {
  ledWifiState = isConnected && !isApMode;
  ledReadyState = !isFirstBoot && !pendingReboot;
  ledFaultState = isLockedOut;

  digitalWrite(PIN_LED_WIFI, ledWifiState ? LED_ON : LED_OFF);
  digitalWrite(PIN_LED_READY, ledReadyState ? LED_ON : LED_OFF);
  digitalWrite(PIN_LED_FAULT, ledFaultState ? LED_ON : LED_OFF);
}

void NetworkManager::handleResetButton() {
  unsigned long now = millis();

  if (interruptResetTriggered) {
      interruptResetTriggered = false;
      if (now - lastResetDebounce >= DEBOUNCE_MS) {
          lastResetDebounce = now;
          resetPressActive = true;
          resetPressStart = now;
      }
  }

  if (!resetPressActive) {
      return;
  }

  if (digitalRead(PIN_BTN_RESET) == LOW) {
      return;
  }

  unsigned long holdMs = now - resetPressStart;
  resetPressActive = false;

  if (holdMs >= RESET_FACTORY_MS) {
      Serial.println("\n[FACTORY RESET] Dang xoa toan bo cau hinh...");
      flushLogsToNvsIfNeeded(true);
      Preferences p;
      p.begin("mydoor", false); p.clear(); p.end();
      p.begin("mydoor_state", false); p.clear(); p.end();
      Serial.println("[FACTORY RESET] Hoan tat. Dang khoi dong lai he thong...");
      resetFactoryPending = true;
      startFaultLedFlash(3);
      pendingReboot = true;
      rebootTime = now;
      return;
  }

  if (holdMs >= RESET_REBOOT_MS) {
      Serial.println("\n[REBOOT] Lenh Reboot tu nut bam cung.");
      flushLogsToNvsIfNeeded(true);
      resetFactoryPending = false;
      startFaultLedFlash(1);
      pendingReboot = true;
      rebootTime = now;
      return;
  }

  if (isApMode) {
      requestApDisable("GPIO2 short press");
      apManualMode = false;
  } else {
      requestApEnable(true, "GPIO2 short press");
  }
}

void NetworkManager::setupSTA() {
  isApMode = false;
#ifdef USE_BLYNK
  resetBlynkSessionState();
#endif
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true); // Tiết kiệm điện, chống nóng

  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("[STA] Dang ket noi toi WiFi Chinh: ");
  Serial.println(ssid);

  // Không chặn (Non-blocking) vòng lặp ở đây. Sẽ kiểm tra trạng thái trong loop()
  setupWebServer();

#ifdef USE_BLYNK
  if (blynkAuth.length() > 5) {
    _blynkTransport.setHandshakeTimeoutSeconds(BLYNK_SSL_HANDSHAKE_TIMEOUT_SEC);
    Blynk.config(blynkAuth.c_str(), "blynk.cloud", 443);
    Serial.println("[BLYNK] Da khoi tao cau hinh Blynk SSL.");
  }
#endif

  // Khởi tạo NTP để lấy thời gian
  configTime(timezone * 3600, 0, "pool.ntp.org", "time.nist.gov");
}

#ifdef USE_RAINMAKER
void NetworkManager::setupRainMaker() {
    if (rainmakerInitialized) return;

    esp_rmaker_config_t rmaker_config = {
        .enable_time_sync = true,
    };
    rainmakerNode = esp_rmaker_node_init(&rmaker_config, "MyDoor", "ESP32 Door Control");
    if (!rainmakerNode) {
        ESP_LOGE(TAG, "Could not initialise RainMaker node.");
        return;
    }

    doorDevice = esp_rmaker_device_create("door-control", "Door Control", (void*)0x00);
    if(doorDevice) {
        esp_rmaker_device_add_cb(doorDevice, write_cb_wrapper, NULL);
        esp_rmaker_node_add_device(rainmakerNode, doorDevice);

        esp_rmaker_param_t *door_up_param = esp_rmaker_param_create("up", ESP_RMAKER_PARAM_POWER, esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
        esp_rmaker_param_add_ui_type(door_up_param, ESP_RMAKER_UI_PUSHBUTTON);
        esp_rmaker_device_add_param(doorDevice, door_up_param);

        esp_rmaker_param_t *door_down_param = esp_rmaker_param_create("down", ESP_RMAKER_PARAM_POWER, esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
        esp_rmaker_param_add_ui_type(door_down_param, ESP_RMAKER_UI_PUSHBUTTON);
        esp_rmaker_device_add_param(doorDevice, door_down_param);

        esp_rmaker_param_t *door_stop_param = esp_rmaker_param_create("stop", ESP_RMAKER_PARAM_POWER, esp_rmaker_bool(false), PROP_FLAG_READ | PROP_FLAG_WRITE);
        esp_rmaker_param_add_ui_type(door_stop_param, ESP_RMAKER_UI_PUSHBUTTON);
        esp_rmaker_device_add_param(doorDevice, door_stop_param);

        esp_rmaker_param_t *door_state_param = esp_rmaker_param_create("state", ESP_RMAKER_PARAM_NAME, esp_rmaker_str("STOPPED"), PROP_FLAG_READ);
        esp_rmaker_param_add_ui_type(door_state_param, ESP_RMAKER_UI_TEXT);
        esp_rmaker_device_add_param(doorDevice, door_state_param);
    }

    powerBoxDevice = esp_rmaker_switch_device_create("power-box", (void*)0x01, controlLogic.isPowerBoxOn());
    if(powerBoxDevice) {
        esp_rmaker_device_add_cb(powerBoxDevice, write_cb_wrapper, NULL);
        esp_rmaker_node_add_device(rainmakerNode, powerBoxDevice);
    }

    lightDevice = esp_rmaker_switch_device_create("light", (void*)0x02, controlLogic.isLightOn());
    if(lightDevice) {
        esp_rmaker_device_add_cb(lightDevice, write_cb_wrapper, NULL);
        esp_rmaker_node_add_device(rainmakerNode, lightDevice);
    }

    esp_rmaker_start();
    rainmakerInitialized = true;
    ESP_LOGI(TAG, "RainMaker initialized and started.");
}

void NetworkManager::startRainMakerProvisioning() {
    ESP_ERROR_CHECK(esp_wifi_start());
    wifiLostFlag = false;
    wifiLostTime = 0;
    ESP_LOGI(TAG, "RainMaker provisioning flow started.");
}

void NetworkManager::stopRainMakerProvisioning() {
    wifiLostFlag = false;
    wifiLostTime = 0;
    ESP_LOGI(TAG, "RainMaker provisioning stop requested.");
}

void NetworkManager::pushRainMakerState() {
    if (!rainmakerInitialized || !isConnected) return;

    esp_rmaker_param_update_and_report(
        esp_rmaker_device_get_param_by_name(powerBoxDevice, "power"),
        controlLogic.isPowerBoxOn() ? esp_rmaker_bool(true) : esp_rmaker_bool(false)
    );

    esp_rmaker_param_update_and_report(
        esp_rmaker_device_get_param_by_name(lightDevice, "power"),
        controlLogic.isLightOn() ? esp_rmaker_bool(true) : esp_rmaker_bool(false)
    );

    esp_rmaker_param_update_and_report(
        esp_rmaker_device_get_param_by_name(doorDevice, "state"),
        esp_rmaker_str(rainmakerDoorState.c_str())
    );
}

esp_err_t NetworkManager::write_cb_wrapper(const esp_rmaker_device_t *device, const esp_rmaker_param_t *param, const esp_rmaker_param_val_t val, void *priv_data, esp_rmaker_write_ctx_t *ctx) {
    (void)device;
    (void)ctx;
    uint32_t device_id = (uint32_t) priv_data;

    if (device_id == 0x01) {
        bool turnOn = val.val.b;
        controlLogic.executeRemoteCommand(turnOn ? CMD_POWER_ON : CMD_POWER_OFF);
        netManager.logEvent("Power Box: " + String(turnOn ? "ON" : "OFF") + " (RainMaker)");
        netManager.applyManualOverrideForPower(turnOn, "RainMaker");
    } else if (device_id == 0x02) {
        bool turnOn = val.val.b;
        controlLogic.executeRemoteCommand(turnOn ? CMD_LIGHT_ON : CMD_LIGHT_OFF);
        netManager.logEvent("Light: " + String(turnOn ? "ON" : "OFF") + " (RainMaker)");
        netManager.applyManualOverrideForLight(turnOn, "RainMaker");
    } else if (device_id == 0x00) {
        const char* param_name = esp_rmaker_param_get_name(param);
        if (strcmp(param_name, "up") == 0 && val.val.b) {
            netManager.rainmakerDoorState = "UP";
            controlLogic.executeRemoteCommand(CMD_UP);
            netManager.logEvent("Door: UP (RainMaker)");
        } else if (strcmp(param_name, "down") == 0 && val.val.b) {
            netManager.rainmakerDoorState = "DOWN";
            controlLogic.executeRemoteCommand(CMD_DOWN);
            netManager.logEvent("Door: DOWN (RainMaker)");
        } else if (strcmp(param_name, "stop") == 0 && val.val.b) {
            netManager.rainmakerDoorState = "STOPPED";
            controlLogic.executeRemoteCommand(CMD_STOP);
            netManager.logEvent("Door: STOP (RainMaker)");
        }
        esp_rmaker_param_update_and_report((esp_rmaker_param_t *)param, esp_rmaker_bool(false));
    }
    netManager.pushCloudState();
    return ESP_OK;
}

void NetworkManager::rainmaker_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (RMAKER_EVENT == event_base) {
        switch (event_id) {
            case RMAKER_EVENT_INIT_DONE:
                ESP_LOGI(TAG, "RainMaker: Init done.");
                break;
            case RMAKER_EVENT_CLAIM_STARTED:
                ESP_LOGI(TAG, "RainMaker: Claim started.");
                break;
            case RMAKER_EVENT_CLAIM_SUCCESSFUL:
                ESP_LOGI(TAG, "RainMaker: Claim successful.");
                netManager.pushRainMakerState();
                break;
            case RMAKER_EVENT_CLAIM_FAILED:
                ESP_LOGW(TAG, "RainMaker: Claim failed.");
                break;
            default:
                break;
        }
    }
}

void NetworkManager::wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI(TAG, "Wi-Fi Disconnected. Retrying...");
            xEventGroupClearBits(netManager.wifiEventGroup, WIFI_CONNECTED_BIT);
            netManager.isConnected = false;
            if (!netManager.wifiLostFlag) {
                netManager.wifiLostFlag = true;
                netManager.wifiLostTime = millis();
                netManager.logEvent("[RM] WiFi lost, start long-loss timer.");
            }
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "Wi-Fi Connected. IP: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(netManager.wifiEventGroup, WIFI_CONNECTED_BIT);
            netManager.isConnected = true;
            netManager.stopRainMakerProvisioning();
            netManager.logEvent("[RM] WiFi recovered, provisioning stopped.");
        }
    }
}
#endif

#ifdef USE_LOCAL_WEB_STACK
bool NetworkManager::checkAuth(AsyncWebServerRequest *request) {
    if (adminUser == "" || adminPass == "") {
        request->send(503, "text/plain", "Device is not claimed yet.");
        return false;
    }
    if (isLockedOut) {
        request->send(429, "text/plain", "Too Many Requests. Locked for 30 minutes.");
        return false;
    }
    if (!request->authenticate(adminUser.c_str(), adminPass.c_str())) {
        failedAuthCount++;
        if (failedAuthCount >= 5) {
            isLockedOut = true;
            lockoutStartTime = millis();
            Serial.println("[SECURITY] Đã khóa truy cập AP 30 phút do sai Pass 5 lần!");
        }
        request->requestAuthentication("MyDoor Config Admin");
        return false;
    }

    failedAuthCount = 0;
    apStartTime = millis();
    return true;
}

void NetworkManager::syncOtaAuth() {
    if (isFirstBoot || adminUser == "" || adminPass == "") {
        return;
    }

    if (!otaInitialized) {
        ElegantOTA.begin(&server, adminUser.c_str(), adminPass.c_str());
        otaInitialized = true;
        Serial.println("[SECURITY] ElegantOTA da bat voi Admin credential.");
    } else {
        ElegantOTA.setAuth(adminUser.c_str(), adminPass.c_str());
        Serial.println("[SECURITY] ElegantOTA da dong bo Admin credential moi.");
    }
}

void NetworkManager::setupWebServer() {
  if (webServerInitialized) return; // Tránh khởi tạo lặp lại nhiều lần

  // API Đăng ký First Boot Admin
  server.on("/setup_first_boot", ASYNC_POST, [](AsyncWebServerRequest *request){
      if (!netManager.isFirstBoot) {
          return request->send(403, "text/plain", "Forbidden. Device already initialized.");
      }

      if (request->hasParam("admin_user", true) && request->hasParam("admin_pass", true)) {
          String newUser = request->getParam("admin_user", true)->value();
          String newPass = request->getParam("admin_pass", true)->value();

          if (isStrongAdminInput(newUser, newPass)) {
              Preferences p; p.begin("mydoor", false);
              p.putString("admin_user", newUser);
              p.putString("admin_pass", newPass);
              p.end();

              netManager.adminUser = newUser;
              netManager.adminPass = newPass;
              netManager.claimRequired = false;
              netManager.isFirstBoot = false; // Tắt cờ First Boot

              // Bây giờ đã có Admin, cấp phép chạy OTA Server
              netManager.syncOtaAuth();

              netManager.logEvent("Admin setup completed.");
              return request->send(200, "text/plain", "OK");
          }
      }
      request->send(400, "text/plain", "Invalid input");
  });

  // Giao diện chính (Bắt buộc Đăng Nhập HTTP Basic Auth, trừ khi First Boot)
  server.on("/", ASYNC_GET, [](AsyncWebServerRequest *request){
    // Nếu thiết bị chưa từng được setup, chuyển qua trang tạo tài khoản luôn, không hỏi Auth
    if (netManager.isFirstBoot) {
        sendHtml(request, setup_html);
        return;
    }

    if (!netManager.checkAuth(request)) return;
    sendHtml(request, index_html);
  });

  // API Quét WiFi (JSON)
  server.on("/scan", ASYNC_GET, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    request->send(200, "application/json", json);
  });

  // API Lấy Cấu Hình Cũ để hiện lên Form
  server.on("/get_config", ASYNC_GET, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    String json = "{\"timezone\":" + String(netManager.timezone) +
                  ",\"onHour\":" + String(netManager.onHour) +
                  ",\"onMin\":" + String(netManager.onMin) +
                  ",\"offHour\":" + String(netManager.offHour) +
                  ",\"offMin\":" + String(netManager.offMin) +
                  ",\"days\":" + String(netManager.scheduleDays) +
                  ",\"l_onHour\":" + String(netManager.lightOnHour) +
                  ",\"l_onMin\":" + String(netManager.lightOnMin) +
                  ",\"l_offHour\":" + String(netManager.lightOffHour) +
                  ",\"l_offMin\":" + String(netManager.lightOffMin) +
                  ",\"lightScheduleDays\":" + String(netManager.lightScheduleDays) +
                  ",\"device_id\":\"" + netManager.deviceId + "\"" +
                  ",\"rescue_ssid\":\"" + netManager.safeGetString(netManager.rescueApSsid) + "\"" +
                  ",\"admin_user\":\"" + netManager.safeGetString(netManager.adminUser) + "\"";

#ifndef USE_RAINMAKER
    json +=       ",\"blynkTemplate\":\"" + maskBlynk(netManager.safeGetString(netManager.blynkTemplate)) + "\"" +
                  ",\"blynkName\":\"" + maskBlynk(netManager.safeGetString(netManager.blynkName)) + "\"" +
                  ",\"blynkAuth\":\"" + maskBlynk(netManager.safeGetString(netManager.blynkAuth)) + "\"";
#endif

    json +=       ",\"power_box_on\":" + String(controlLogic.isPowerBoxOn() ? "true" : "false") +
                  ",\"light_on\":" + String(controlLogic.isLightOn() ? "true" : "false") + "}";
    request->send(200, "application/json", json);
  });

  // API Lưu WiFi & Cloud (Sẽ reboot sau 3 giây)
  server.on("/save_wifi", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    if(request->hasParam("ssid", true) && request->hasParam("password", true)) {
      String newSSID = request->getParam("ssid", true)->value();
      String newPass = request->getParam("password", true)->value();

      Preferences p; p.begin("mydoor", false);

      auto saveStringIfChanged = [&p](const char* key, String newValue) {
          if (p.getString(key, "") != newValue) {
              p.putString(key, newValue);
          }
      };

      saveStringIfChanged("ssid", newSSID);
      saveStringIfChanged("pass", newPass);
      if(request->hasParam("ssid2", true)) saveStringIfChanged("ssid2", request->getParam("ssid2", true)->value());
      if(request->hasParam("pass2", true)) saveStringIfChanged("pass2", request->getParam("pass2", true)->value());

#ifndef USE_RAINMAKER
      if(request->hasParam("blynkTemplate", true)) {
        String newVal = request->getParam("blynkTemplate", true)->value();
        if (newVal.length() > 0 && newVal.indexOf('*') == -1) saveStringIfChanged("blynkTemplate", newVal);
      }
      if(request->hasParam("blynkName", true)) {
        String newVal = request->getParam("blynkName", true)->value();
        if (newVal.length() > 0 && newVal.indexOf('*') == -1) saveStringIfChanged("blynkName", newVal);
      }
      if(request->hasParam("blynkAuth", true)) {
        String newVal = request->getParam("blynkAuth", true)->value();
        if (newVal.length() > 0 && newVal.indexOf('*') == -1) saveStringIfChanged("blynkAuth", newVal);
      }
#endif

      p.end();

      request->send(200, "text/plain", "OK");
      netManager.pendingReboot = true;
      netManager.rebootTime = millis();
    } else request->send(400, "text/plain", "Missing args");
  });

  // API Lưu Lịch Trình Relay 4
  server.on("/save_schedule", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    Preferences p; p.begin("mydoor", false);

    auto saveCharIfChanged = [&p](const char* key, int8_t newValue) {
        if (p.getChar(key, 0) != newValue) p.putChar(key, newValue);
    };
    auto saveUCharIfChanged = [&p](const char* key, uint8_t newValue) {
        if (p.getUChar(key, 0) != newValue) p.putUChar(key, newValue);
    };

    if(request->hasParam("timezone", true)) saveCharIfChanged("timezone", request->getParam("timezone", true)->value().toInt());
    if(request->hasParam("onHour", true)) saveUCharIfChanged("onHour", request->getParam("onHour", true)->value().toInt());
    if(request->hasParam("onMin", true))  saveUCharIfChanged("onMin", request->getParam("onMin", true)->value().toInt());
    if(request->hasParam("offHour", true)) saveUCharIfChanged("offHour", request->getParam("offHour", true)->value().toInt());
    if(request->hasParam("offMin", true))  saveUCharIfChanged("offMin", request->getParam("offMin", true)->value().toInt());

    uint8_t days = 0;
    for(int i=0; i<7; i++) {
      if(request->hasParam("day_" + String(i), true)) days |= (1 << i);
    }
    saveUCharIfChanged("days", days);

    if(request->hasParam("l_onHour", true)) saveUCharIfChanged("l_onHour", request->getParam("l_onHour", true)->value().toInt());
    if(request->hasParam("l_onMin", true))  saveUCharIfChanged("l_onMin", request->getParam("l_onMin", true)->value().toInt());
    if(request->hasParam("l_offHour", true)) saveUCharIfChanged("l_offHour", request->getParam("l_offHour", true)->value().toInt());
    if(request->hasParam("l_offMin", true))  saveUCharIfChanged("l_offMin", request->getParam("l_offMin", true)->value().toInt());

    uint8_t lightScheduleDays = 0;
    for(int i=0; i<7; i++) {
      if(request->hasParam("l_day_" + String(i), true)) lightScheduleDays |= (1 << i);
    }
    saveUCharIfChanged("lightScheduleDays", lightScheduleDays);

    p.end();

    netManager.loadConfig(); // Load lại ngay vào RAM

    // Cập nhật lại NTP
    configTime(netManager.timezone * 3600, 0, "pool.ntp.org", "time.nist.gov");

    request->send(200, "text/plain", "OK");
  });

  // API Lưu Tài Khoản Admin
  server.on("/save_admin", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    if (!request->hasParam("admin_user", true)) {
      return request->send(400, "text/plain", "Bad Request");
    }

    String newUser = request->getParam("admin_user", true)->value();
    String newPass = request->hasParam("admin_pass", true)
      ? request->getParam("admin_pass", true)->value()
      : "";

    if (newUser.length() < 4) {
      return request->send(400, "text/plain", "Admin username is too short.");
    }

    Preferences p; p.begin("mydoor", false);
    String currentPass = p.getString("admin_pass", "");
    String finalPass = newPass.length() > 0 ? newPass : currentPass;

    if (!isStrongAdminInput(newUser, finalPass)) {
      p.end();
      return request->send(400, "text/plain", "Admin credentials are too weak.");
    }

    p.putString("admin_user", newUser);
    if (newPass.length() > 0) {
      p.putString("admin_pass", newPass);
    }
    p.end();

    netManager.loadConfig();
    netManager.syncOtaAuth(); // Đồng bộ ngay lập tức sang OTA
    request->send(200, "text/plain", "OK");
  });

  server.on("/save_rescue_ap", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    if (!request->hasParam("rescue_ap_ssid", true)) {
      return request->send(400, "text/plain", "Bad Request");
    }

    String newSsid = request->getParam("rescue_ap_ssid", true)->value();
    String newPass = request->hasParam("rescue_ap_pass", true)
      ? request->getParam("rescue_ap_pass", true)->value()
      : "";

    if (newSsid.length() < 4) {
      return request->send(400, "text/plain", "SSID quá ngắn.");
    }

    Preferences p; p.begin("mydoor", false);
    String currentPass = p.getString("rescue_pass", "");
    String finalPass = newPass.length() > 0 ? newPass : currentPass;

    if (finalPass.length() < 8 || !hasSpecialChar(finalPass)) {
      p.end();
      return request->send(400, "text/plain", "Mật khẩu Rescue AP quá ngắn hoặc thiếu ký tự đặc biệt (VD: @, #, $, ...).");
    }

    p.putString("rescue_ssid", newSsid);
    if (newPass.length() > 0) {
      p.putString("rescue_pass", newPass);
    }
    p.putBool("rescue_customized", true);
    p.end();

    netManager.safeSetString(netManager.rescueApSsid, newSsid);
    if (newPass.length() > 0) {
      netManager.safeSetString(netManager.rescueApPass, newPass);
    }

    request->send(200, "text/plain", "OK");

#ifndef USE_RAINMAKER
    if (netManager.isApMode) {
#endif
      netManager.pendingReboot = true;
      netManager.rebootTime = millis();
#ifndef USE_RAINMAKER
    }
#endif
  });

  // API Khởi động lại
  server.on("/reboot", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    request->send(200, "text/plain", "Rebooting");
    netManager.flushLogsToNvsIfNeeded(true);
    netManager.pendingReboot = true;
    netManager.rebootTime = millis();
  });

  server.on("/ap_mode", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    if (!request->hasParam("state", true)) {
      return request->send(400, "text/plain", "Missing state");
    }

    String state = request->getParam("state", true)->value();
    bool turnOn = (state == "1" || state == "on" || state == "true");
    if (turnOn) {
      netManager.requestApEnable(true, "WebUI request");
      netManager.logEvent("Rescue AP: BAT (WebUI)");
    } else {
      netManager.requestApDisable("WebUI request");
      netManager.logEvent("Rescue AP: TAT (WebUI)");
    }

    request->send(200, "text/plain", "OK");
  });

  // API API Control Cửa (Up/Stop/Down)
  server.on("/control", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    if(request->hasParam("cmd", true)) {
        String cmd = request->getParam("cmd", true)->value();
        if (cmd == "up") {
            controlLogic.executeRemoteCommand(CMD_UP);
            netManager.logEvent("Cua: LEN (WebUI)");
        }
        else if (cmd == "stop") {
            controlLogic.executeRemoteCommand(CMD_STOP);
            netManager.logEvent("Cua: DUNG (WebUI)");
        }
        else if (cmd == "down") {
            controlLogic.executeRemoteCommand(CMD_DOWN);
            netManager.logEvent("Cua: XUONG (WebUI)");
        }

        request->send(200, "text/plain", "OK");
    } else {
        request->send(400, "text/plain", "Missing cmd");
    }
  });

  // API Đóng/Cắt Nguồn Tổng Relay 4
  server.on("/power", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    if(request->hasParam("state", true)) {
        String state = request->getParam("state", true)->value();
        bool turnOn = (state == "1" || state == "true");
        controlLogic.executeRemoteCommand(turnOn ? CMD_POWER_ON : CMD_POWER_OFF);
        netManager.logEvent("Nguon Box: " + String(turnOn ? "BAT" : "TAT") + " (WebUI)");
        netManager.applyManualOverrideForPower(turnOn, "WebUI");
        netManager.pushCloudState();

        request->send(200, "text/plain", "OK");
    } else {
        request->send(400, "text/plain", "Missing state");
    }
  });

  // API Đóng/Cắt Đèn
  server.on("/light", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    if(request->hasParam("state", true)) {
        String state = request->getParam("state", true)->value();
        bool turnOn = (state == "1" || state == "true");
        controlLogic.executeRemoteCommand(turnOn ? CMD_LIGHT_ON : CMD_LIGHT_OFF);
        netManager.logEvent("Den: " + String(turnOn ? "BAT" : "TAT") + " (WebUI)");
        netManager.applyManualOverrideForLight(turnOn, "WebUI");
        netManager.pushCloudState();

        request->send(200, "text/plain", "OK");
    } else {
        request->send(400, "text/plain", "Missing state");
    }
  });

  // API Đọc Lịch Sử Logs cho WebUI
  server.on("/logs", ASYNC_GET, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;
    request->send(200, "text/plain", netManager.getRecentLogs());
  });

  // API Public read-only logs (không yêu cầu auth)
  server.on("/public_logs", ASYNC_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", netManager.getPublicLogs());
  });

  // Khởi động ElegantOTA (/update) -> Nạp file .bin từ trình duyệt
  netManager.syncOtaAuth();

  ElegantOTA.onStart([]() {
      netManager.isOtaRunning = true;
      Serial.println("[OTA] Bat dau upload, dung check RAM de tranh Brick!");
  });
  ElegantOTA.onEnd([](bool success) {
      netManager.isOtaRunning = false;
      Serial.println(success ? "[OTA] Hoan tat thanh cong." : "[OTA] Upload that bai.");
  });

  server.begin();
  webServerInitialized = true;
  Serial.println("[WEB] Web Server & OTA san sang.");
}
#else
void NetworkManager::syncOtaAuth() {}
void NetworkManager::setupWebServer() {}
#endif

void NetworkManager::checkAPCycle() {
  bool provisioningCritical = claimRequired || isFirstBoot || ssid == "";
  bool shouldCycleAp = !provisioningCritical && wifiLostFlag;

  if (isApMode) {
      if (!shouldCycleAp || apManualMode) {
          return;
      }
      if (millis() - apStartTime >= AP_CYCLE_ON_MS) {
          Serial.println("[AP CYCLE] AP da bat 10 phut, tat AP trong 5 phut...");
          requestApDisable("AP cycle OFF window");
          apOfflineTime = millis();
      }
  } else if (shouldCycleAp) {
      if (millis() - apOfflineTime >= AP_CYCLE_OFF_MS) {
          Serial.println("[AP CYCLE] AP da nghi 5 phut, bat lai AP 10 phut...");
          requestApEnable(false, "AP cycle ON window");
      }
  }
}

bool NetworkManager::isScheduleActiveNow(int currentMins) {
  int onMins = onHour * 60 + onMin;
  int offMins = offHour * 60 + offMin;

  if (onMins == offMins) return false;

  // Lấy thứ hiện tại và thứ hôm qua
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) return false;

  uint8_t today = timeinfo.tm_wday;
  uint8_t yesterday = (today + 6) % 7;

  bool todayActive = (scheduleDays & (1 << today)) != 0;
  bool yesterdayActive = (scheduleDays & (1 << yesterday)) != 0;

  if (onMins < offMins) {
      // Lịch cùng ngày (VD: 06:00 -> 23:00)
      return todayActive && currentMins >= onMins && currentMins < offMins;
  } else {
      // Lịch qua đêm (VD: 22:00 -> 06:00)
      bool activeToday = todayActive && currentMins >= onMins;
      bool activeYesterday = yesterdayActive && currentMins < offMins;
      return activeToday || activeYesterday;
  }
}

bool NetworkManager::isLightScheduleActiveNow(int currentMins) {
  int onMins = lightOnHour * 60 + lightOnMin;
  int offMins = lightOffHour * 60 + lightOffMin;

  if (onMins == offMins) return false;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) return false;

  uint8_t today = timeinfo.tm_wday;
  uint8_t yesterday = (today + 6) % 7;

  bool todayActive = (lightScheduleDays & (1 << today)) != 0;
  bool yesterdayActive = (lightScheduleDays & (1 << yesterday)) != 0;

  if (onMins < offMins) {
      return todayActive && currentMins >= onMins && currentMins < offMins;
  } else {
      bool activeToday = todayActive && currentMins >= onMins;
      bool activeYesterday = yesterdayActive && currentMins < offMins;
      return activeToday || activeYesterday;
  }
}

void NetworkManager::updateManualOverridesAtScheduleEdge(bool powerScheduleActiveNow, bool lightScheduleActiveNow) {
  if (!scheduleStateInitialized) {
      lastPowerScheduleActive = powerScheduleActiveNow;
      lastLightScheduleActive = lightScheduleActiveNow;
      scheduleStateInitialized = true;
      return;
  }

  if (powerScheduleActiveNow != lastPowerScheduleActive) {
      if (powerOverrideActive) {
          powerOverrideActive = false;
          logEvent("[AUTO] Power override cleared at schedule edge");
      }
      lastPowerScheduleActive = powerScheduleActiveNow;
  }

  if (lightScheduleActiveNow != lastLightScheduleActive) {
      if (lightOverrideActive) {
          lightOverrideActive = false;
          logEvent("[AUTO] Light override cleared at schedule edge");
      }
      lastLightScheduleActive = lightScheduleActiveNow;
  }
}

void NetworkManager::applyManualOverrideForPower(bool turnOn, const char* source) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) return;

  int currentMins = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  bool scheduleActiveNow = isScheduleActiveNow(currentMins);

  if (turnOn != scheduleActiveNow) {
      if (!powerOverrideActive) {
          powerOverrideActive = true;
          logEvent(String("[AUTO] Power override active until next schedule edge (") + source + ")");
      }
  } else if (powerOverrideActive) {
      powerOverrideActive = false;
      logEvent("[AUTO] Power override cleared (manual aligned with schedule)");
  }
}

void NetworkManager::applyManualOverrideForLight(bool turnOn, const char* source) {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) return;

  int currentMins = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  bool scheduleActiveNow = isLightScheduleActiveNow(currentMins);

  if (turnOn != scheduleActiveNow) {
      if (!lightOverrideActive) {
          lightOverrideActive = true;
          logEvent(String("[AUTO] Light override active until next schedule edge (") + source + ")");
      }
  } else if (lightOverrideActive) {
      lightOverrideActive = false;
      logEvent("[AUTO] Light override cleared (manual aligned with schedule)");
  }
}

void NetworkManager::checkNTP() {
  if (!isConnected || isApMode) return;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
      Serial.println("[NTP] Chua lay duoc thoi gian...");
      return;
  }

  // Đồng bộ với Control Logic Core 1
  controlLogic.setLocalTime(timeinfo.tm_hour, timeinfo.tm_min);

  // Logic tự động bật/tắt Box Cửa (Relay 4)
  int currentMins = timeinfo.tm_hour * 60 + timeinfo.tm_min;

  bool scheduleActiveNow = isScheduleActiveNow(currentMins);
  bool lightScheduleActiveNow = isLightScheduleActiveNow(currentMins);

  updateManualOverridesAtScheduleEdge(scheduleActiveNow, lightScheduleActiveNow);

  if (!powerOverrideActive) {
      if (scheduleActiveNow && !controlLogic.isPowerBoxOn()) {
          Serial.printf("[AUTO] %02d:%02d - Den gio mo Box Cua\n", timeinfo.tm_hour, timeinfo.tm_min);
          logEvent("[AUTO] Nguon Box: BAT (Schedule)");
          controlLogic.executeRemoteCommand(CMD_POWER_ON);
          pushCloudState();
      }
      else if (!scheduleActiveNow && controlLogic.isPowerBoxOn()) {
          Serial.printf("[AUTO] %02d:%02d - Den gio dong Box Cua\n", timeinfo.tm_hour, timeinfo.tm_min);
          logEvent("[AUTO] Nguon Box: TAT (Schedule)");
          controlLogic.executeRemoteCommand(CMD_POWER_OFF);
          pushCloudState();
      }
  }

  if (!lightOverrideActive) {
      if (lightScheduleActiveNow && !controlLogic.isLightOn()) {
          Serial.printf("[AUTO] %02d:%02d - Den gio bat Den\n", timeinfo.tm_hour, timeinfo.tm_min);
          logEvent("[AUTO] Den: BAT (Schedule)");
          controlLogic.executeRemoteCommand(CMD_LIGHT_ON);
          pushCloudState();
      }
      else if (!lightScheduleActiveNow && controlLogic.isLightOn()) {
          Serial.printf("[AUTO] %02d:%02d - Den gio tat Den\n", timeinfo.tm_hour, timeinfo.tm_min);
          logEvent("[AUTO] Den: TAT (Schedule)");
          controlLogic.executeRemoteCommand(CMD_LIGHT_OFF);
          pushCloudState();
      }
  }
}

void NetworkManager::pushCloudState() {
#ifdef USE_BLYNK
    pushBlynkState();
#endif
#ifdef USE_RAINMAKER
    pushRainMakerState();
#endif
}

void NetworkManager::pushBlynkState() {
#ifdef USE_BLYNK
  if (Blynk.connected()) {
      Blynk.virtualWrite(VPIN_POWER_BOX, controlLogic.isPowerBoxOn() ? 1 : 0);
      Blynk.virtualWrite(VPIN_LIGHT, controlLogic.isLightOn() ? 1 : 0);
      Blynk.virtualWrite(VPIN_LED_BLUE, ledWifiState ? 1 : 0);
      Blynk.virtualWrite(VPIN_LED_GREEN, ledReadyState ? 1 : 0);
      Blynk.virtualWrite(VPIN_LED_RED, ledFaultState ? 1 : 0);
      Blynk.virtualWrite(VPIN_LED_YELLOW, (isApMode || faultLedBlinkState) ? 1 : 0);
  }
#endif
}

// Handler Blynk nhận lệnh từ App
#ifdef USE_BLYNK
BLYNK_CONNECTED() {
  netManager.onBlynkConnected();
}

BLYNK_DISCONNECTED() {
  netManager.onBlynkDisconnected();
}

BLYNK_WRITE(VPIN_DOOR_UP) {
  if (param.asInt() == 1) netManager.handleRemoteDoorCommand(CMD_UP);
}
BLYNK_WRITE(VPIN_DOOR_DOWN) {
  if (param.asInt() == 1) netManager.handleRemoteDoorCommand(CMD_DOWN);
}
BLYNK_WRITE(VPIN_DOOR_STOP) {
  if (param.asInt() == 1) netManager.handleRemoteDoorCommand(CMD_STOP);
}
BLYNK_WRITE(VPIN_POWER_BOX) {
  netManager.handleRemotePowerCommand(param.asInt() == 1);
}
BLYNK_WRITE(VPIN_LIGHT) {
  netManager.handleRemoteLightCommand(param.asInt() == 1);
}
#endif

void NetworkManager::handleRemoteDoorCommand(RemoteCommand cmd) {
#ifdef USE_BLYNK
  if (!canAcceptRemoteCommands()) {
      Serial.println("[BLYNK] Bo qua lenh cua do session cloud vua reconnect hoac chua san sang.");
      return;
  }
  if (cmd == CMD_UP) logEvent("Cua: LEN (Blynk)");
  else if (cmd == CMD_DOWN) logEvent("Cua: XUONG (Blynk)");
  else if (cmd == CMD_STOP) logEvent("Cua: DUNG (Blynk)");
#endif
#ifdef USE_RAINMAKER
  if (cmd == CMD_UP) rainmakerDoorState = "UP";
  else if (cmd == CMD_DOWN) rainmakerDoorState = "DOWN";
  else if (cmd == CMD_STOP) rainmakerDoorState = "STOPPED";
#endif
  controlLogic.executeRemoteCommand(cmd);
}

void NetworkManager::handleRemotePowerCommand(bool turnOn) {
#ifdef USE_BLYNK
  if (!canAcceptRemoteCommands()) {
      Serial.println("[BLYNK] Bo qua lenh nguon do server dang replay trang thai cu.");
      return;
  }
  logEvent("Nguon Box: " + String(turnOn ? "BAT" : "TAT") + " (Blynk)");
#endif
  controlLogic.executeRemoteCommand(turnOn ? CMD_POWER_ON : CMD_POWER_OFF);
  applyManualOverrideForPower(turnOn, "Blynk");
}

void NetworkManager::handleRemoteLightCommand(bool turnOn) {
#ifdef USE_BLYNK
  if (!canAcceptRemoteCommands()) {
      Serial.println("[BLYNK] Bo qua lenh den do server dang replay trang thai cu.");
      return;
  }
  logEvent("Den: " + String(turnOn ? "BAT" : "TAT") + " (Blynk)");
#endif
  controlLogic.executeRemoteCommand(turnOn ? CMD_LIGHT_ON : CMD_LIGHT_OFF);
  applyManualOverrideForLight(turnOn, "Blynk");
}

void NetworkManager::onBlynkConnected() {
#ifdef USE_BLYNK
  blynkWasConnected = true;
  blynkInvalidToken = false;
  blynkReconnectBackoffMs = BLYNK_RECONNECT_BASE_MS;
  blynkRemoteGuardUntil = millis() + BLYNK_POST_CONNECT_GUARD_MS;
  Serial.println("[BLYNK] Cloud da ket noi. Dang replay log lich su va dong bo trang thai local.");
  replayLogsToBlynk();
  pushCloudState();
#endif
}

void NetworkManager::onBlynkDisconnected() {
#ifdef USE_BLYNK
  if (blynkWasConnected) {
      Serial.println("[BLYNK] Mat ket noi cloud. He thong tiep tuc o che do local fail-safe.");
  }
  blynkWasConnected = false;
  blynkRemoteGuardUntil = 0;
#endif
}

bool NetworkManager::canAcceptRemoteCommands() const {
#ifdef USE_BLYNK
  return !isApMode && isConnected && Blynk.connected() && millis() >= blynkRemoteGuardUntil;
#else
  return true;
#endif
}

void NetworkManager::resetBlynkSessionState() {
#ifdef USE_BLYNK
  lastBlynkConnectAttempt = 0;
  blynkReconnectBackoffMs = BLYNK_RECONNECT_BASE_MS;
  blynkRemoteGuardUntil = 0;
  blynkWasConnected = false;
  blynkInvalidToken = false;
#endif
}

void NetworkManager::handleBlynk() {
#ifdef USE_BLYNK
  if (isApMode || !isConnected || isFirstBoot || blynkAuth.length() <= 5) {
      if (Blynk.connected()) {
          Blynk.disconnect();
      }
      resetBlynkSessionState();
      return;
  }

  if (Blynk.connected()) {
      Blynk.run();
      return;
  }

  if (Blynk.isTokenInvalid()) {
      if (!blynkInvalidToken) {
          Serial.println("[BLYNK] Auth token khong hop le. Dung reconnect cho den khi duoc cap nhat.");
      }
      blynkInvalidToken = true;
      return;
  }

  unsigned long now = millis();
  if (now - lastBlynkConnectAttempt < blynkReconnectBackoffMs) {
      return;
  }

  lastBlynkConnectAttempt = now;
  esp_task_wdt_reset();
  Serial.printf("[BLYNK] Thu reconnect: timeout=%dms, handshake=%ds, backoff=%lums\n",
                BLYNK_CONNECT_TIMEOUT_MS,
                BLYNK_SSL_HANDSHAKE_TIMEOUT_SEC,
                blynkReconnectBackoffMs);
  bool connected = Blynk.connect(BLYNK_CONNECT_TIMEOUT_MS);
  esp_task_wdt_reset();

  if (connected) {
      blynkReconnectBackoffMs = BLYNK_RECONNECT_BASE_MS;
      return;
  }

  if (Blynk.isTokenInvalid()) {
      blynkInvalidToken = true;
      Serial.println("[BLYNK] Auth token khong hop le. Da dung reconnect tu dong.");
      return;
  }

  if (blynkReconnectBackoffMs < BLYNK_RECONNECT_MAX_MS) {
      unsigned long nextBackoff = blynkReconnectBackoffMs * 2;
      blynkReconnectBackoffMs = (nextBackoff > BLYNK_RECONNECT_MAX_MS) ? BLYNK_RECONNECT_MAX_MS : nextBackoff;
  }
#endif
}

void NetworkManager::handleWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    isConnected = true;
    wifiLostFlag = false;

    if (isApMode && !apManualMode) {
        Serial.println("[WIFI] Co mang tro lai. Dang tat Rescue AP...");
        requestApDisable("WiFi recovered");
    }
    return;
  }

  isConnected = false;

  // LƯU Ý QUAN TRỌNG: Không return ngay khi ở isApMode.
  // Nếu return ở đây, thiết bị sẽ không bao giờ chạy xuống logic kết nối lại STA ở dưới
  // và bị kẹt vĩnh viễn ở AP mode nếu mất mạng trên 5 phút.

  if (!wifiLostFlag) {
      wifiLostFlag = true;
      wifiLostTime = millis();
  }

  if (ssid != "" && millis() - lastWiFiCheck > WIFI_TIMEOUT_MS) {
    lastWiFiCheck = millis();

    static bool trySecondary = false;

    // Khi vừa bắt đầu giai đoạn mất mạng, luôn thử lại Wi-Fi chính trước
    if (wifiLostFlag && (millis() - wifiLostTime < 1000)) {
        trySecondary = false;
    }

    Serial.println("[WIFI] Mat ket noi, dang thu lai...");
    WiFi.disconnect();

    if (trySecondary && ssid2.length() > 0) {
        Serial.println("Thu ket noi Wi-Fi phu: " + ssid2);
        WiFi.begin(ssid2.c_str(), pass2.c_str());
    } else {
        WiFi.begin(ssid.c_str(), password.c_str());
    }
    trySecondary = !trySecondary;
  }

  if (wifiLostFlag && !isApMode && (millis() - wifiLostTime >= 300000)) {
      Serial.println("[AP] Mat ket noi 5 phut, tu dong bat Rescue AP!");
      requestApEnable(false, "Long WiFi outage");
  }
}

void NetworkManager::loop() {
  unsigned long now = millis();

  if (interruptConfigTriggered) {
    interruptConfigTriggered = false;
    if (now - lastConfigDebounce >= DEBOUNCE_MS) {
      lastConfigDebounce = now;
      configPressActive = true;
      configPressStart = now;
    }
  }

  if (configPressActive) {
    if (digitalRead(PIN_BTN_CONFIG) == LOW) {
      if (now - configPressStart >= CONFIG_HOLD_MS) {
        Serial.println("\n[SYSTEM] BAT CHE DO CAU HINH WIFI (AP) DO NGUOI DUNG BAM NUT!");
        WiFi.disconnect(true);
        requestApEnable(true, "GPIO0 hold");
        configPressActive = false;
      }
    } else {
      configPressActive = false;
    }
  }

  handleResetButton();
  processPendingApAction();

#ifndef USE_RAINMAKER
  handleWiFi();
#else
  if (!isConnected) {
      if (!wifiLostFlag) {
          wifiLostFlag = true;
          wifiLostTime = now;
      } else if (now - wifiLostTime >= RAINMAKER_REPROVISION_MS) {
          startRainMakerProvisioning();
          logEvent("[RM] Long WiFi outage, restart provisioning.");
      }
  } else {
      wifiLostFlag = false;
      wifiLostTime = 0;
  }
#endif
  handleBlynk();

  static unsigned long lastNTPCheck = 0;
  if (millis() - lastNTPCheck >= 60000) {
      lastNTPCheck = millis();
      checkNTP();
  }

#ifdef USE_LOCAL_WEB_STACK
  ElegantOTA.loop();
#endif

  syncLogsToCloud();
  flushLogsToNvsIfNeeded(false);

  if (isLockedOut && millis() - lockoutStartTime >= AP_LOCKOUT_MS) {
      isLockedOut = false;
      failedAuthCount = 0;
      Serial.println("[SECURITY] Hết 30 phút khóa AP. Mở khóa.");
  }

  updateFaultLed(now);
  updateStatusLeds();

#ifdef USE_BLYNK
  static unsigned long lastLedCloudSync = 0;
  if (now - lastLedCloudSync >= 1000) {
      lastLedCloudSync = now;
      pushBlynkState();
  }
#endif

  if (pendingReboot && millis() - rebootTime >= 2000) {
      if (now - lastRestartAt >= RESTART_GUARD_MS) {
          lastRestartAt = now;
          flushLogsToNvsIfNeeded(true);
          ESP.restart();
      } else {
          Serial.println("[GUARD] Bo qua reboot de tranh reboot-loop lien tuc.");
          pendingReboot = false;
      }
  }

#ifndef USE_RAINMAKER
  checkAPCycle();
#endif
}
