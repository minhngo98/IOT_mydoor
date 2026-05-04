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
extern bool wifiLowLevelInit(bool persistent);
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

unsigned long jitteredDelay(unsigned long baseMs, unsigned long jitterMs) {
  if (jitterMs == 0) return baseMs;
  return baseMs + (esp_random() % (jitterMs + 1));
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
  resetPressActive(false), resetFactoryTriggered(false), resetPressStart(0), lastResetDebounce(0), claimRequired(false), webServerInitialized(false), isFirstBoot(false),
  otaInitialized(false), wifiReconnectBackoffMs(WIFI_TIMEOUT_MS), nextWiFiRetryAt(0),
  lastBlynkConnectAttempt(0), blynkReconnectBackoffMs(BLYNK_RECONNECT_BASE_MS), blynkRemoteGuardUntil(0),
  blynkWasConnected(false), blynkInvalidToken(false), cloudStateInitialized(false),
  lastPushedPowerState(false), lastPushedLightState(false), lastPushedBlue(false), lastPushedGreen(false),
  lastPushedRed(false), lastPushedYellow(false), lastBlynkStatePushMs(0),
  wifiReconnectAttempts(0), blynkReconnectAttempts(0), rainmakerReprovisionAttempts(0),
  bootResetReason(ESP_RST_UNKNOWN), lastInternetDisconnectEpoch(0), lastInternetReconnectEpoch(0), lastInternetOutageSec(0), logIndex(0), lastBlynkSyncLogIndex(0),
  persistentLogs(""), pendingPersistentLogCount(0), lastPersistentFlushMs(0), isOtaRunning(false), pendingReboot(false), rebootTime(0), lastRestartAt(0),
  resetFactoryPending(false), faultLedBlinkState(false), faultLedLastToggle(0), faultLedFlashRemainingToggles(0), faultLedFlashDeadline(0),
  ledWifiState(false), ledReadyState(false), ledFaultState(false), apManualMode(false), pendingApAction(0),
  powerOverrideActive(false), lightOverrideActive(false), scheduleStateInitialized(false), lastPowerScheduleActive(false), lastLightScheduleActive(false) {
    stringMutex = NULL;
    stateMutex = NULL;
#ifdef USE_RAINMAKER
    rainmakerNode = NULL;
    doorDevice = NULL;
    powerBoxDevice = NULL;
    lightDevice = NULL;
    rainmakerDoorState = "STOPPED";
    rainmakerInitialized = false;
    rainmakerProvisioningActive = false;
    rainmakerProvManagerInitialized = false;
    rainmakerReprovisionBackoffMs = RAINMAKER_REPROVISION_MS;
    nextRainmakerReprovisionAt = 0;
    memset(rainmakerProvServiceName, 0, sizeof(rainmakerProvServiceName));
    memset(rainmakerProvPop, 0, sizeof(rainmakerProvPop));
    rainmakerForceResetProvisioning = false;
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

    String output;
    output.reserve(tag.length() + timeStr.length() + message.length() + 6);
    output += "[";
    output += tag;
    output += "] ";
    output += timeStr;
    output += " ";
    output += message;
    return output;
}

bool NetworkManager::shouldPersistLog(const String& message) const {
    if (message.startsWith("[HEALTH]")) return false;
    if (message.startsWith("[AUTO]")) return false;

    if (message.startsWith("[NET]")) return true;
    if (message.startsWith("[BOOT]")) return true;
    if (message.startsWith("[SYSTEM] Controlled reboot requested")) return true;
    if (message.indexOf("Rescue AP:") >= 0) return true;

    if (message.indexOf("Cua:") >= 0) return true;
    if (message.indexOf("Nguon Box:") >= 0) return true;
    if (message.indexOf("Den:") >= 0) return true;

    return false;
}

void NetworkManager::markInternetDisconnected(unsigned long nowMs) {
    if (wifiLostFlag) return;

    wifiLostFlag = true;
    wifiLostTime = nowMs;

    time_t epoch = time(nullptr);
    lastInternetDisconnectEpoch = (epoch >= 100000) ? epoch : 0;

    String msg = "[NET] Internet disconnected";
    if (ssid.length() > 0) {
        msg += " (";
        msg += ssid;
        msg += ")";
    }
    logEvent(msg);
}

void NetworkManager::markInternetConnected(unsigned long nowMs) {
    if (!wifiLostFlag) return;

    uint32_t outageSec = static_cast<uint32_t>((nowMs - wifiLostTime) / 1000UL);
    wifiLostFlag = false;
    wifiLostTime = 0;
    lastInternetOutageSec = outageSec;

    time_t epoch = time(nullptr);
    lastInternetReconnectEpoch = (epoch >= 100000) ? epoch : 0;

    String msg;
    msg.reserve(96);
    msg = "[NET] Internet connected after ";
    msg += String(outageSec);
    msg += "s";
    if (WiFi.localIP()) {
        msg += ", IP=";
        msg += WiFi.localIP().toString();
    }
    logEvent(msg);
}

void NetworkManager::loadPersistentLogs() {
    String loaded;
    Preferences logPrefs;
    if (logPrefs.begin("mydoor_logs", false)) {
        if (logPrefs.isKey("blob")) {
            loaded = logPrefs.getString("blob", "");
        }
        logPrefs.end();
    }

    pruneLogsOlderThan3Days(loaded);

    while (loaded.length() > LOG_PERSISTENT_MAX_BYTES) {
        int firstNewline = loaded.indexOf('\n');
        if (firstNewline < 0) {
            loaded = "";
            break;
        }
        loaded.remove(0, firstNewline + 1);
    }

    if (xSemaphoreTake(stringMutex, pdMS_TO_TICKS(150))) {
        persistentLogs = loaded;
        pendingPersistentLogCount = 0;
        xSemaphoreGive(stringMutex);
    } else {
        persistentLogs = loaded;
        pendingPersistentLogCount = 0;
    }

    rebuildRuntimeLogsFromPersistent();
}

void NetworkManager::appendPersistentLogLine(time_t epoch, const String& tag, const String& message) {
    if (epoch <= 0) {
        return;
    }

    String safeTag = normalizeLogField(tag);
    if (safeTag.length() == 0) {
        safeTag = "INFO";
    }

    String safeMessage = normalizeLogField(message);

    String record;
    record.reserve(safeTag.length() + safeMessage.length() + 24);
    record += String(static_cast<unsigned long>(epoch));
    record += "|0|";
    record += safeTag;
    record += "|";
    record += safeMessage;
    record += "\n";

    if (xSemaphoreTake(stringMutex, pdMS_TO_TICKS(150))) {
        persistentLogs += record;
        pendingPersistentLogCount++;
        xSemaphoreGive(stringMutex);
    } else {
        persistentLogs += record;
        pendingPersistentLogCount++;
    }
}

void NetworkManager::flushLogsToNvsIfNeeded(bool force) {
    unsigned long now = millis();

    uint16_t pendingCount = pendingPersistentLogCount;
    if (!force) {
        if (pendingCount == 0) return;
        if (pendingCount < LOG_FLUSH_BATCH_COUNT && (now - lastPersistentFlushMs) < LOG_FLUSH_INTERVAL_MS) {
            return;
        }
    }

    String snapshot;
    if (!xSemaphoreTake(stringMutex, pdMS_TO_TICKS(200))) {
        return;
    }

    snapshot = persistentLogs;
    pruneLogsOlderThan3Days(snapshot);

    while (snapshot.length() > LOG_PERSISTENT_MAX_BYTES) {
        int firstNewline = snapshot.indexOf('\n');
        if (firstNewline < 0) {
            snapshot = "";
            break;
        }
        snapshot.remove(0, firstNewline + 1);
    }

    persistentLogs = snapshot;
    pendingPersistentLogCount = 0;
    xSemaphoreGive(stringMutex);

    Preferences logPrefs;
    if (logPrefs.begin("mydoor_logs", false)) {
        logPrefs.putString("blob", snapshot);
        logPrefs.end();
    }

    lastPersistentFlushMs = now;
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

        xSemaphoreGive(stringMutex);
    } else {
        Serial.println("[MUTEX] Timeout logging event");
    }

    if (shouldPersistLog(message)) {
        appendPersistentLogLine(epoch, tag, message);
    }
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

String NetworkManager::getHealthSnapshot() const {
    String json;
    json.reserve(420);
    json = "{\"heap_now\":";
    json += String(controlLogic.getCurrentFreeHeap());
    json += ",\"heap_min\":";
    json += String(controlLogic.getMinObservedHeap());
    json += ",\"queue_drop\":";
    json += String(controlLogic.getQueueDropCount());
    json += ",\"queue_peak\":";
    json += String(controlLogic.getMaxObservedQueueDepth());
    json += ",\"wifi_reconnect\":";
    json += String(wifiReconnectAttempts);
    json += ",\"blynk_reconnect\":";
    json += String(blynkReconnectAttempts);
    json += ",\"rm_reprovision\":";
    json += String(rainmakerReprovisionAttempts);
    json += ",\"is_connected\":";
    json += (isConnected ? "true" : "false");
    json += ",\"is_ap_mode\":";
    json += (isApMode ? "true" : "false");
    json += ",\"reset_reason_code\":";
    json += String(static_cast<int>(bootResetReason));
    json += ",\"last_disconnect_epoch\":";
    json += String(static_cast<unsigned long>(lastInternetDisconnectEpoch > 0 ? lastInternetDisconnectEpoch : 0));
    json += ",\"last_reconnect_epoch\":";
    json += String(static_cast<unsigned long>(lastInternetReconnectEpoch > 0 ? lastInternetReconnectEpoch : 0));
    json += ",\"last_outage_sec\":";
    json += String(lastInternetOutageSec);
    json += "}";
    return json;
}

void NetworkManager::syncLogsToCloud() {
    if (lastBlynkSyncLogIndex == logIndex) return;

    if (xSemaphoreTake(stringMutex, pdMS_TO_TICKS(100))) {
        int sent = 0;
        while (lastBlynkSyncLogIndex != logIndex && sent < LOG_SYNC_BATCH_PER_LOOP) {
            String logLine = eventLogs[lastBlynkSyncLogIndex];
#ifdef USE_BLYNK
            if (Blynk.connected()) {
                Blynk.virtualWrite(VPIN_TERMINAL, logLine + "\n");
            }
#endif
            lastBlynkSyncLogIndex = (lastBlynkSyncLogIndex + 1) % 15;
            sent++;
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
    String logs = "";
    if (xSemaphoreTake(stringMutex, pdMS_TO_TICKS(100))) {
        for (int i = 0; i < 15; ++i) {
            int idx = (logIndex + i) % 15;
            if (eventLogs[idx].length() > 0) {
                logs += eventLogs[idx] + "\n";
            }
        }
        xSemaphoreGive(stringMutex);
    }
    return logs;
}

String NetworkManager::getPublicLogs() const {
    return getRecentLogs();
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
  if (stateMutex == NULL) {
      stateMutex = xSemaphoreCreateMutex();
  }

#ifdef USE_RAINMAKER
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
#endif

  bootResetReason = esp_reset_reason();
  logEvent(String("[BOOT] Reset reason code: ") + String(static_cast<int>(bootResetReason)));

  lastPersistentFlushMs = millis();
  loadPersistentLogs();

  pinMode(PIN_BTN_CONFIG, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_CONFIG), isr_config_button, FALLING);

  pinMode(PIN_BTN_RESET, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_RESET), isr_reset_button, FALLING);
  loadConfig();

#ifdef USE_RAINMAKER
  WiFi.mode(WIFI_STA);
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);
  esp_event_handler_register(RMAKER_EVENT, ESP_EVENT_ANY_ID, &rainmaker_event_handler, NULL);
  esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &provisioning_event_handler, NULL);

  setupRainMaker();
  if (rainmakerInitialized) {
      startRainMakerProvisioning();
  } else {
      logEvent("[RM] Init failed, provisioning skipped.");
  }
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
  deviceId = buildDeviceId();

  if (!preferences.begin("mydoor", false)) {
    ssid = "";
    password = "";
    ssid2 = "";
    pass2 = "";
    adminUser = "";
    adminPass = "";
    blynkTemplate = "";
    blynkName = "";
    blynkAuth = "";
    rescueApSsid = DEFAULT_RESCUE_AP_SSID;
    rescueApPass = DEFAULT_RESCUE_AP_PASS;
    timezone = 7;
    onHour = 6;
    onMin = 0;
    offHour = 23;
    offMin = 0;
    scheduleDays = 127;
    lightOnHour = 18;
    lightOnMin = 0;
    lightOffHour = 5;
    lightOffMin = 0;
    lightScheduleDays = 127;
    powerOverrideActive = false;
    lightOverrideActive = false;
    claimRequired = true;
    isFirstBoot = true;

    Serial.println("[NVS] Khong mo duoc namespace 'mydoor'. Dung cau hinh mac dinh va cho claim lai.");
    return;
  }

  auto getOptString = [this](const char* key) -> String {
    return preferences.isKey(key) ? preferences.getString(key, "") : "";
  };

  if (!preferences.isKey("device_id")) {
    preferences.putString("device_id", deviceId);
  }

  ssid = getOptString("ssid");
  password = getOptString("pass");
  ssid2 = getOptString("ssid2");
  pass2 = getOptString("pass2");

  adminUser = getOptString("admin_user");
  adminPass = getOptString("admin_pass");

  blynkTemplate = getOptString("blynkTemplate");
  blynkName = getOptString("blynkName");
  blynkAuth = getOptString("blynkAuth");

  rescueApSsid = getOptString("rescue_ssid");
  rescueApPass = getOptString("rescue_pass");
  bool rescueCustomized = preferences.getBool("rescue_custom", false);

  timezone = preferences.getChar("timezone", 7);
  onHour = preferences.getUChar("onHour", 6);
  onMin = preferences.getUChar("onMin", 0);
  offHour = preferences.getUChar("offHour", 23);
  offMin = preferences.getUChar("offMin", 0);
  scheduleDays = preferences.getUChar("days", 127);

  lightOnHour = preferences.getUChar("l_onHour", 18);
  lightOnMin = preferences.getUChar("l_onMin", 0);
  lightOffHour = preferences.getUChar("l_offHour", 5);
  lightOffMin = preferences.getUChar("l_offMin", 0);
  lightScheduleDays = preferences.getUChar("lightScheduleDays", 127);

  powerOverrideActive = preferences.getBool("power_override", false);
  lightOverrideActive = preferences.getBool("light_override", false);

  if (ssid == "") {
      if (adminUser == "" || adminPass == "") {
          isFirstBoot = true;
          Serial.println("[SECURITY] Thiet bi moi: Yeu cau tao tai khoan Admin!");
      } else {
          isFirstBoot = false;
      }
  } else {
      if (adminUser == "" || adminPass == "") {
          Serial.println("[SECURITY] Phat hien Firmware cu nang cap chua co Admin. Se yeu cau claim lai.");
      }
      isFirstBoot = false;
  }

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
    preferences.putBool("rescue_custom", false);
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
    preferences.putBool("rescue_custom", false);
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
  Serial.printf("[AP] Rescue AP dang hoat dong. SSID: %s, PASS: [HIDDEN], IP: 10.10.10.1\n", rescueApSsid.c_str());

  if (ssid != "") {
      WiFi.begin(ssid.c_str(), password.c_str());
  }

  apStartTime = millis();
  setupWebServer();
}

void NetworkManager::enableRescueAp(const char* reason) {
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
  if (stateMutex != NULL && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50))) {
      apManualMode = manualMode;
      pendingApAction = 1;
      xSemaphoreGive(stateMutex);
  } else {
      apManualMode = manualMode;
      pendingApAction = 1;
  }

  if (reason != nullptr && reason[0] != '\0') {
      Serial.printf("[AP] Queue bat AP: %s\n", reason);
  }
}

void NetworkManager::requestApDisable(const char* reason) {
  if (stateMutex != NULL && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50))) {
      pendingApAction = 2;
      xSemaphoreGive(stateMutex);
  } else {
      pendingApAction = 2;
  }

  if (reason != nullptr && reason[0] != '\0') {
      Serial.printf("[AP] Queue tat AP: %s\n", reason);
  }
}

void NetworkManager::processPendingApAction() {
  uint8_t action = 0;
  if (stateMutex != NULL && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50))) {
      action = pendingApAction;
      pendingApAction = 0;
      xSemaphoreGive(stateMutex);
  } else {
      action = pendingApAction;
      pendingApAction = 0;
  }

  if (action == 1) {
      enableRescueAp("Queued AP ON");
      return;
  }

  if (action == 2) {
      disableRescueAp("Queued AP OFF");
      if (stateMutex != NULL && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50))) {
          apManualMode = false;
          xSemaphoreGive(stateMutex);
      } else {
          apManualMode = false;
      }
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
      if (!resetPressActive && now - lastResetDebounce >= DEBOUNCE_MS) {
          lastResetDebounce = now;
          resetPressActive = true;
          resetFactoryTriggered = false;
          resetPressStart = now;
      }
  }

  if (!resetPressActive) {
      return;
  }

  bool pressed = (digitalRead(PIN_BTN_RESET) == LOW);

  if (pressed) {
      unsigned long holdMs = now - resetPressStart;
      if (!resetFactoryTriggered && holdMs >= RESET_FACTORY_MS) {
          resetFactoryTriggered = true;
          resetPressActive = false;
          Serial.println("\n[FACTORY RESET] Dang xoa toan bo cau hinh...");
          flushLogsToNvsIfNeeded(true);
          Preferences p;
          p.begin("mydoor", false); p.clear(); p.end();
          p.begin("mydoor_state", false); p.clear(); p.end();
          Serial.println("[FACTORY RESET] Hoan tat. Dang khoi dong lai he thong...");
          resetFactoryPending = true;
          startFaultLedFlash(3);
          requestControlledReboot("Factory reset hold >=10s");
      }
      return;
  }

  unsigned long holdMs = now - resetPressStart;
  resetPressActive = false;

  if (resetFactoryTriggered) {
      return;
  }

  if (holdMs >= RESET_SHORT_PRESS_MS) {
      Serial.printf("[RESET BTN] Hold %lums ignored (short < %dms, factory >= %dms).\n", holdMs, RESET_SHORT_PRESS_MS, RESET_FACTORY_MS);
      return;
  }

  startFaultLedFlash(1);
  if (isApMode) {
      requestApDisable("GPIO21 short press");
      apManualMode = false;
  } else {
      requestApEnable(true, "GPIO21 short press");
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
    if (!rainmakerInitialized) {
        ESP_LOGW(TAG, "RainMaker not initialized. Skip provisioning start.");
        return;
    }

    if (rainmakerProvisioningActive) {
        ESP_LOGW(TAG, "Provisioning already active, skip restart.");
        return;
    }

    if (rainmakerProvServiceName[0] == '\0') {
        const char* id = deviceId.length() > 6 ? deviceId.c_str() + (deviceId.length() - 6) : deviceId.c_str();
        snprintf(rainmakerProvServiceName, sizeof(rainmakerProvServiceName), "MYDOOR_%s", id);
    }

    if (rainmakerProvPop[0] == '\0') {
        snprintf(rainmakerProvPop, sizeof(rainmakerProvPop), "%s", deviceId.c_str());
    }

    if (!wifiLowLevelInit(true)) {
        ESP_LOGE(TAG, "wifiLowLevelInit failed before provisioning start.");
        return;
    }

    if (!rainmakerProvManagerInitialized) {
        wifi_prov_mgr_config_t config = {};
        config.scheme = wifi_prov_scheme_softap;
        config.scheme_event_handler = WIFI_PROV_EVENT_HANDLER_NONE;
        config.app_event_handler.event_cb = nullptr;
        config.app_event_handler.user_data = nullptr;

        esp_err_t initErr = wifi_prov_mgr_init(config);
        if (initErr != ESP_OK) {
            ESP_LOGE(TAG, "wifi_prov_mgr_init failed: %s (%d)", esp_err_to_name(initErr), static_cast<int>(initErr));
            return;
        }
        rainmakerProvManagerInitialized = true;
        ESP_LOGI(TAG, "Provisioning manager initialized (BLE).");
    }

    if (rainmakerForceResetProvisioning) {
        esp_err_t resetErr = wifi_prov_mgr_reset_provisioning();
        if (resetErr != ESP_OK) {
            ESP_LOGW(TAG, "wifi_prov_mgr_reset_provisioning failed: %s (%d)", esp_err_to_name(resetErr), static_cast<int>(resetErr));
        }
    }

    bool provisioned = false;
    esp_err_t provCheckErr = wifi_prov_mgr_is_provisioned(&provisioned);
    if (provCheckErr != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_mgr_is_provisioned failed: %s (%d)", esp_err_to_name(provCheckErr), static_cast<int>(provCheckErr));
        return;
    }

    if (provisioned && !rainmakerForceResetProvisioning) {
        rainmakerForceResetProvisioning = false;
        ESP_LOGI(TAG, "Already provisioned. Skip provisioning start.");
        return;
    }

    esp_err_t startErr = wifi_prov_mgr_start_provisioning(
        WIFI_PROV_SECURITY_1,
        rainmakerProvPop,
        rainmakerProvServiceName,
        "12345678"
    );

    if (startErr != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_mgr_start_provisioning failed: %s (%d)", esp_err_to_name(startErr), static_cast<int>(startErr));
        return;
    }

    rainmakerProvisioningActive = true;
    rainmakerForceResetProvisioning = false;
    wifiLostFlag = false;
    wifiLostTime = 0;
    ESP_LOGI(TAG, "RainMaker SoftAP provisioning start requested. service=%s pop=%s", rainmakerProvServiceName, rainmakerProvPop);
}

void NetworkManager::stopRainMakerProvisioning() {
    if (!rainmakerInitialized) {
        return;
    }

    if (rainmakerProvManagerInitialized && rainmakerProvisioningActive) {
        wifi_prov_mgr_stop_provisioning();
        ESP_LOGI(TAG, "RainMaker provisioning stop requested.");
    }

    wifiLostFlag = false;
    wifiLostTime = 0;
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
            netManager.handleRemoteDoorCommand(CMD_UP, "RainMaker");
        } else if (strcmp(param_name, "down") == 0 && val.val.b) {
            netManager.handleRemoteDoorCommand(CMD_DOWN, "RainMaker");
        } else if (strcmp(param_name, "stop") == 0 && val.val.b) {
            netManager.handleRemoteDoorCommand(CMD_STOP, "RainMaker");
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

void NetworkManager::provisioning_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    (void)arg;
    (void)event_base;

    switch (event_id) {
        case WIFI_PROV_START:
            netManager.rainmakerProvisioningActive = true;
            netManager.logEvent("[RM] SoftAP provisioning started.");
            WiFiProv.printQR(netManager.rainmakerProvServiceName, netManager.rainmakerProvPop, "softap");
            netManager.logEvent("[RM] Scan QR in ESP RainMaker app (Self/Manual claiming).");
            break;
        case WIFI_PROV_CRED_RECV:
            netManager.logEvent("[RM] WiFi credentials received from provisioning app.");
            break;
        case WIFI_PROV_CRED_SUCCESS:
            netManager.logEvent("[RM] Provisioning credentials accepted.");
            break;
        case WIFI_PROV_CRED_FAIL: {
            wifi_prov_sta_fail_reason_t* reason = static_cast<wifi_prov_sta_fail_reason_t*>(event_data);
            if (reason && *reason == WIFI_PROV_STA_AUTH_ERROR) {
                netManager.logEvent("[RM] Provisioning failed: WiFi auth error.");
            } else if (reason && *reason == WIFI_PROV_STA_AP_NOT_FOUND) {
                netManager.logEvent("[RM] Provisioning failed: WiFi AP not found.");
            } else {
                netManager.logEvent("[RM] Provisioning failed: unknown station reason.");
            }
            break;
        }
        case WIFI_PROV_END:
            netManager.rainmakerProvisioningActive = false;
            if (netManager.rainmakerProvManagerInitialized) {
                wifi_prov_mgr_deinit();
                netManager.rainmakerProvManagerInitialized = false;
                ESP_LOGI(TAG, "Provisioning manager deinitialized.");
            }
            netManager.logEvent("[RM] Provisioning service stopped.");
            break;
        default:
            break;
    }
}

void NetworkManager::wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            if (!netManager.rainmakerInitialized) {
                return;
            }
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI(TAG, "Wi-Fi Disconnected. Retrying...");
            xEventGroupClearBits(netManager.wifiEventGroup, WIFI_CONNECTED_BIT);
            netManager.isConnected = false;
            if (!netManager.wifiLostFlag) {
                netManager.markInternetDisconnected(millis());
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
            netManager.markInternetConnected(millis());
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

bool NetworkManager::requestControlledReboot(const char* reason) {
  if (isOtaRunning) {
      logEvent("[SYSTEM] Reboot blocked while OTA is running.");
      return false;
  }

  bool scheduled = false;
  if (stateMutex != NULL && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100))) {
      if (!pendingReboot) {
          pendingReboot = true;
          rebootTime = millis();
          scheduled = true;
      }
      xSemaphoreGive(stateMutex);
  } else if (!pendingReboot) {
      pendingReboot = true;
      rebootTime = millis();
      scheduled = true;
  }

  if (!scheduled) {
      return false;
  }

  if (reason != nullptr && reason[0] != '\0') {
      logEvent(String("[SYSTEM] Controlled reboot requested: ") + reason);
  } else {
      logEvent("[SYSTEM] Controlled reboot requested.");
  }

  return true;
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
    String json;
    json.reserve(32 + (n > 0 ? n : 1) * 48);
    json = "[";
    for (int i = 0; i < n; ++i) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"";
      json += WiFi.SSID(i);
      json += "\",\"rssi\":";
      json += String(WiFi.RSSI(i));
      json += "}";
    }
    json += "]";
    request->send(200, "application/json", json);
  });

  // API Lấy Cấu Hình Cũ để hiện lên Form
  server.on("/get_config", ASYNC_GET, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    String json;
    json.reserve(768);
    json = "{\"timezone\":";
    json += String(netManager.timezone);
    json += ",\"onHour\":";
    json += String(netManager.onHour);
    json += ",\"onMin\":";
    json += String(netManager.onMin);
    json += ",\"offHour\":";
    json += String(netManager.offHour);
    json += ",\"offMin\":";
    json += String(netManager.offMin);
    json += ",\"days\":";
    json += String(netManager.scheduleDays);
    json += ",\"l_onHour\":";
    json += String(netManager.lightOnHour);
    json += ",\"l_onMin\":";
    json += String(netManager.lightOnMin);
    json += ",\"l_offHour\":";
    json += String(netManager.lightOffHour);
    json += ",\"l_offMin\":";
    json += String(netManager.lightOffMin);
    json += ",\"lightScheduleDays\":";
    json += String(netManager.lightScheduleDays);
    json += ",\"ssid\":\"";
    json += netManager.safeGetString(netManager.ssid);
    json += "\"";
    json += ",\"password\":\"";
    json += netManager.safeGetString(netManager.password);
    json += "\"";
    json += ",\"ssid2\":\"";
    json += netManager.safeGetString(netManager.ssid2);
    json += "\"";
    json += ",\"pass2\":\"";
    json += netManager.safeGetString(netManager.pass2);
    json += "\"";
    json += ",\"device_id\":\"";
    json += netManager.deviceId;
    json += "\"";
    json += ",\"rescue_ssid\":\"";
    json += netManager.safeGetString(netManager.rescueApSsid);
    json += "\"";
    json += ",\"admin_user\":\"";
    json += netManager.safeGetString(netManager.adminUser);
    json += "\"";

#ifndef USE_RAINMAKER
    json += ",\"blynkTemplate\":\"";
    json += maskBlynk(netManager.safeGetString(netManager.blynkTemplate));
    json += "\"";
    json += ",\"blynkName\":\"";
    json += maskBlynk(netManager.safeGetString(netManager.blynkName));
    json += "\"";
    json += ",\"blynkAuth\":\"";
    json += maskBlynk(netManager.safeGetString(netManager.blynkAuth));
    json += "\"";
#endif

    json += ",\"power_box_on\":";
    json += (controlLogic.isPowerBoxOn() ? "true" : "false");
    json += ",\"light_on\":";
    json += (controlLogic.isLightOn() ? "true" : "false");
    json += "}";
    request->send(200, "application/json", json);
  });

  // API Lưu WiFi / Cloud theo từng nhóm field gửi lên
  server.on("/save_wifi", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    bool hasWifiFields = request->hasParam("ssid", true) || request->hasParam("password", true)
      || request->hasParam("ssid2", true) || request->hasParam("pass2", true);

#ifndef USE_RAINMAKER
    bool hasCloudFields = request->hasParam("blynkTemplate", true)
      || request->hasParam("blynkName", true) || request->hasParam("blynkAuth", true);
#else
    bool hasCloudFields = false;
#endif

    if (!hasWifiFields && !hasCloudFields) {
      return request->send(400, "text/plain", "Missing args");
    }

    Preferences p;
    p.begin("mydoor", false);

    auto saveStringIfChanged = [&p](const char* key, const String& newValue) {
      if (p.getString(key, "") != newValue) {
        p.putString(key, newValue);
        return true;
      }
      return false;
    };

    bool wifiChanged = false;
    bool cloudChanged = false;

    if (hasWifiFields) {
      String currentSSID = p.getString("ssid", "");
      String currentPass = p.getString("pass", "");
      String currentSSID2 = p.getString("ssid2", "");
      String currentPass2 = p.getString("pass2", "");

      String newSSID = request->hasParam("ssid", true) ? request->getParam("ssid", true)->value() : currentSSID;
      String newPass = request->hasParam("password", true) ? request->getParam("password", true)->value() : currentPass;
      String newSSID2 = request->hasParam("ssid2", true) ? request->getParam("ssid2", true)->value() : currentSSID2;
      String newPass2 = request->hasParam("pass2", true) ? request->getParam("pass2", true)->value() : currentPass2;

      newSSID.trim();
      newPass.trim();
      newSSID2.trim();
      newPass2.trim();

      if (newSSID.length() == 0) {
        p.end();
        return request->send(400, "text/plain", "WiFi SSID chinh khong duoc de trong.");
      }

      if (newPass.length() == 0 && newSSID == currentSSID) {
        newPass = currentPass;
      }
      if (newSSID != currentSSID && newPass.length() == 0) {
        p.end();
        return request->send(400, "text/plain", "Doi SSID chinh thi phai nhap mat khau moi.");
      }

      if (newSSID2.length() == 0) {
        newPass2 = "";
      } else if (newPass2.length() == 0 && newSSID2 == currentSSID2) {
        newPass2 = currentPass2;
      } else if (newPass2.length() == 0) {
        p.end();
        return request->send(400, "text/plain", "Doi SSID phu thi phai nhap mat khau WiFi phu.");
      }

      if (newSSID2 == newSSID) {
        p.end();
        return request->send(400, "text/plain", "WiFi phu khong duoc trung WiFi chinh.");
      }

      if ((newPass.length() > 0 && newPass.length() < 8) || (newPass2.length() > 0 && newPass2.length() < 8)) {
        p.end();
        return request->send(400, "text/plain", "Mat khau WiFi phai tu 8 ky tu tro len.");
      }

      wifiChanged |= saveStringIfChanged("ssid", newSSID);
      wifiChanged |= saveStringIfChanged("pass", newPass);
      wifiChanged |= saveStringIfChanged("ssid2", newSSID2);
      wifiChanged |= saveStringIfChanged("pass2", newPass2);

      netManager.ssid = newSSID;
      netManager.password = newPass;
      netManager.ssid2 = newSSID2;
      netManager.pass2 = newPass2;
    }

#ifndef USE_RAINMAKER
    if (hasCloudFields) {
      if (request->hasParam("blynkTemplate", true)) {
        String newVal = request->getParam("blynkTemplate", true)->value();
        if (newVal.length() > 0 && newVal.indexOf('*') == -1) {
          cloudChanged |= saveStringIfChanged("blynkTemplate", newVal);
          netManager.blynkTemplate = newVal;
        }
      }
      if (request->hasParam("blynkName", true)) {
        String newVal = request->getParam("blynkName", true)->value();
        if (newVal.length() > 0 && newVal.indexOf('*') == -1) {
          cloudChanged |= saveStringIfChanged("blynkName", newVal);
          netManager.blynkName = newVal;
        }
      }
      if (request->hasParam("blynkAuth", true)) {
        String newVal = request->getParam("blynkAuth", true)->value();
        if (newVal.length() > 0 && newVal.indexOf('*') == -1) {
          cloudChanged |= saveStringIfChanged("blynkAuth", newVal);
          netManager.blynkAuth = newVal;
          Blynk.disconnect();
          Blynk.config(newVal.c_str(), "blynk.cloud", 443);
        }
      }
    }
#endif

    p.end();

    if (wifiChanged) {
      bool confirmedReboot = request->hasParam("reboot_confirm", true) && request->getParam("reboot_confirm", true)->value() == "1";
      if (!confirmedReboot) {
        return request->send(409, "text/plain", "Thay doi WiFi can khoi dong lai thiet bi de ap dung. Ban co dong y reboot ngay khong?");
      }
      request->send(200, "text/plain", "OK_REBOOT");
      netManager.requestControlledReboot("WiFi config updated by user confirm");
      return;
    }

    if (cloudChanged) {
      request->send(200, "text/plain", "OK_CLOUD");
      return;
    }

    request->send(200, "text/plain", "NO_CHANGES");
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
    p.putBool("rescue_custom", true);
    p.end();

    netManager.safeSetString(netManager.rescueApSsid, newSsid);
    if (newPass.length() > 0) {
      netManager.safeSetString(netManager.rescueApPass, newPass);
    }

    request->send(200, "text/plain", "OK");

#ifndef USE_RAINMAKER
    if (netManager.isApMode) {
#endif
      netManager.requestControlledReboot("Rescue AP config updated");
#ifndef USE_RAINMAKER
    }
#endif
  });

  // API Khởi động lại
  server.on("/reboot", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    if (netManager.isOtaRunning) {
      return request->send(409, "text/plain", "OTA đang chạy, không thể reboot lúc này.");
    }

    request->send(200, "text/plain", "Rebooting");
    netManager.flushLogsToNvsIfNeeded(true);
    netManager.requestControlledReboot("Web reboot endpoint");
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
            netManager.handleRemoteDoorCommand(CMD_UP, "WebUI");
        }
        else if (cmd == "stop") {
            netManager.handleRemoteDoorCommand(CMD_STOP, "WebUI");
        }
        else if (cmd == "down") {
            netManager.handleRemoteDoorCommand(CMD_DOWN, "WebUI");
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

  // API Health Snapshot cho soak test (auth bắt buộc)
  server.on("/health", ASYNC_GET, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;
    request->send(200, "application/json", netManager.getHealthSnapshot());
  });

  // API Public read-only logs (không yêu cầu auth)
  server.on("/public_logs", ASYNC_GET, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;
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

bool NetworkManager::requestControlledReboot(const char* reason) {
  if (isOtaRunning) {
      logEvent("[SYSTEM] Reboot blocked while OTA is running.");
      return false;
  }

  bool scheduled = false;
  if (stateMutex != NULL && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(100))) {
      if (!pendingReboot) {
          pendingReboot = true;
          rebootTime = millis();
          scheduled = true;
      }
      xSemaphoreGive(stateMutex);
  } else if (!pendingReboot) {
      pendingReboot = true;
      rebootTime = millis();
      scheduled = true;
  }

  if (!scheduled) {
      return false;
  }

  if (reason != nullptr && reason[0] != '\0') {
      logEvent(String("[SYSTEM] Controlled reboot requested: ") + reason);
  } else {
      logEvent("[SYSTEM] Controlled reboot requested.");
  }

  return true;
}

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

void NetworkManager::saveManualOverrideState() {
  Preferences p;
  p.begin("mydoor", false);
  p.putBool("power_override", powerOverrideActive);
  p.putBool("light_override", lightOverrideActive);
  p.end();
}

void NetworkManager::updateManualOverridesAtScheduleEdge(bool powerScheduleActiveNow, bool lightScheduleActiveNow) {
  bool overrideChanged = false;

  if (!scheduleStateInitialized) {
      lastPowerScheduleActive = powerScheduleActiveNow;
      lastLightScheduleActive = lightScheduleActiveNow;
      scheduleStateInitialized = true;
      return;
  }

  if (powerScheduleActiveNow != lastPowerScheduleActive) {
      if (powerOverrideActive) {
          powerOverrideActive = false;
          overrideChanged = true;
          logEvent("[AUTO] Power override cleared at schedule edge");
      }
      lastPowerScheduleActive = powerScheduleActiveNow;
  }

  if (lightScheduleActiveNow != lastLightScheduleActive) {
      if (lightOverrideActive) {
          lightOverrideActive = false;
          overrideChanged = true;
          logEvent("[AUTO] Light override cleared at schedule edge");
      }
      lastLightScheduleActive = lightScheduleActiveNow;
  }

  if (overrideChanged) {
      saveManualOverrideState();
  }
}

void NetworkManager::applyManualOverrideForPower(bool turnOn, const char* source) {
  bool overrideChanged = false;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
      if (!powerOverrideActive) {
          powerOverrideActive = true;
          overrideChanged = true;
          logEvent(String("[AUTO] Power override active (NTP pending) (") + source + ")");
      }

      if (overrideChanged) {
          saveManualOverrideState();
      }
      return;
  }

  int currentMins = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  bool scheduleActiveNow = isScheduleActiveNow(currentMins);

  if (turnOn != scheduleActiveNow) {
      if (!powerOverrideActive) {
          powerOverrideActive = true;
          overrideChanged = true;
          logEvent(String("[AUTO] Power override active until next schedule edge (") + source + ")");
      }
  } else if (powerOverrideActive) {
      powerOverrideActive = false;
      overrideChanged = true;
      logEvent("[AUTO] Power override cleared (manual aligned with schedule)");
  }

  if (overrideChanged) {
      saveManualOverrideState();
  }
}

void NetworkManager::applyManualOverrideForLight(bool turnOn, const char* source) {
  bool overrideChanged = false;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) {
      if (!lightOverrideActive) {
          lightOverrideActive = true;
          overrideChanged = true;
          logEvent(String("[AUTO] Light override active (NTP pending) (") + source + ")");
      }

      if (overrideChanged) {
          saveManualOverrideState();
      }
      return;
  }

  int currentMins = timeinfo.tm_hour * 60 + timeinfo.tm_min;
  bool scheduleActiveNow = isLightScheduleActiveNow(currentMins);

  if (turnOn != scheduleActiveNow) {
      if (!lightOverrideActive) {
          lightOverrideActive = true;
          overrideChanged = true;
          logEvent(String("[AUTO] Light override active until next schedule edge (") + source + ")");
      }
  } else if (lightOverrideActive) {
      lightOverrideActive = false;
      overrideChanged = true;
      logEvent("[AUTO] Light override cleared (manual aligned with schedule)");
  }

  if (overrideChanged) {
      saveManualOverrideState();
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
    pushBlynkState(true);
#endif
#ifdef USE_RAINMAKER
    pushRainMakerState();
#endif
}

void NetworkManager::pushBlynkState(bool force) {
#ifdef USE_BLYNK
  if (!Blynk.connected()) {
      cloudStateInitialized = false;
      return;
  }

  bool powerNow = controlLogic.isPowerBoxOn();
  bool lightNow = controlLogic.isLightOn();
  bool blueNow = ledWifiState;
  bool greenNow = ledReadyState;
  bool redNow = ledFaultState;
  bool yellowNow = (isApMode || faultLedBlinkState);

  bool changed = !cloudStateInitialized ||
                 powerNow != lastPushedPowerState ||
                 lightNow != lastPushedLightState ||
                 blueNow != lastPushedBlue ||
                 greenNow != lastPushedGreen ||
                 redNow != lastPushedRed ||
                 yellowNow != lastPushedYellow;

  unsigned long now = millis();
  bool heartbeatDue = (now - lastBlynkStatePushMs) >= CLOUD_STATE_HEARTBEAT_MS;
  if (!force && !changed && !heartbeatDue) {
      return;
  }

  Blynk.virtualWrite(VPIN_POWER_BOX, powerNow ? 1 : 0);
  Blynk.virtualWrite(VPIN_LIGHT, lightNow ? 1 : 0);
  Blynk.virtualWrite(VPIN_LED_BLUE, blueNow ? 1 : 0);
  Blynk.virtualWrite(VPIN_LED_GREEN, greenNow ? 1 : 0);
  Blynk.virtualWrite(VPIN_LED_RED, redNow ? 1 : 0);
  Blynk.virtualWrite(VPIN_LED_YELLOW, yellowNow ? 1 : 0);

  lastPushedPowerState = powerNow;
  lastPushedLightState = lightNow;
  lastPushedBlue = blueNow;
  lastPushedGreen = greenNow;
  lastPushedRed = redNow;
  lastPushedYellow = yellowNow;
  cloudStateInitialized = true;
  lastBlynkStatePushMs = now;
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
  if (param.asInt() == 1) netManager.handleRemoteDoorCommand(CMD_UP, "Blynk");
}
BLYNK_WRITE(VPIN_DOOR_DOWN) {
  if (param.asInt() == 1) netManager.handleRemoteDoorCommand(CMD_DOWN, "Blynk");
}
BLYNK_WRITE(VPIN_DOOR_STOP) {
  if (param.asInt() == 1) netManager.handleRemoteDoorCommand(CMD_STOP, "Blynk");
}
BLYNK_WRITE(VPIN_POWER_BOX) {
  netManager.handleRemotePowerCommand(param.asInt() == 1);
}
BLYNK_WRITE(VPIN_LIGHT) {
  netManager.handleRemoteLightCommand(param.asInt() == 1);
}
#endif

void NetworkManager::handleRemoteDoorCommand(RemoteCommand cmd, const char* source) {
#ifdef USE_BLYNK
  if (source != nullptr && strcmp(source, "Blynk") == 0 && !canAcceptRemoteCommands()) {
      Serial.println("[BLYNK] Bo qua lenh cua do session cloud vua reconnect hoac chua san sang.");
      return;
  }
#endif

  if (!controlLogic.isPowerBoxOn()) {
      logEvent("Vui long bat nguon de dieu khien cua cuon.");
      return;
  }

#ifdef USE_RAINMAKER
  if (source != nullptr && strcmp(source, "RainMaker") == 0) {
      if (cmd == CMD_UP) rainmakerDoorState = "UP";
      else if (cmd == CMD_DOWN) rainmakerDoorState = "DOWN";
      else if (cmd == CMD_STOP) rainmakerDoorState = "STOPPED";
  }
#endif

  if (cmd == CMD_UP) logEvent(String("Cua: LEN (") + (source ? source : "Remote") + ")");
  else if (cmd == CMD_DOWN) logEvent(String("Cua: XUONG (") + (source ? source : "Remote") + ")");
  else if (cmd == CMD_STOP) logEvent(String("Cua: DUNG (") + (source ? source : "Remote") + ")");

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
  cloudStateInitialized = false;
  lastBlynkStatePushMs = 0;
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
  blynkReconnectAttempts++;
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

  blynkReconnectBackoffMs = jitteredDelay(blynkReconnectBackoffMs, BLYNK_RECONNECT_JITTER_MS);
#endif
}

void NetworkManager::handleWiFi() {
  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    bool wasLost = wifiLostFlag;

    if (stateMutex != NULL && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50))) {
        isConnected = true;
        xSemaphoreGive(stateMutex);
    } else {
        isConnected = true;
    }

    if (wasLost) {
        markInternetConnected(now);
    }

    wifiReconnectBackoffMs = WIFI_TIMEOUT_MS;
    nextWiFiRetryAt = 0;

    if (isApMode && !apManualMode) {
        Serial.println("[WIFI] Co mang tro lai. Dang tat Rescue AP...");
        requestApDisable("WiFi recovered");
    }
    return;
  }

  bool firstLoss = false;
  if (stateMutex != NULL && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50))) {
      isConnected = false;
      firstLoss = !wifiLostFlag;
      xSemaphoreGive(stateMutex);
  } else {
      isConnected = false;
      firstLoss = !wifiLostFlag;
  }

  if (firstLoss) {
      markInternetDisconnected(now);
      wifiReconnectBackoffMs = WIFI_TIMEOUT_MS;
      nextWiFiRetryAt = now;
  }

  if (ssid != "" && now >= nextWiFiRetryAt) {
    static bool trySecondary = false;

    wifiReconnectAttempts++;
    Serial.println("[WIFI] Mat ket noi, dang thu lai...");
    WiFi.disconnect();

    if (trySecondary && ssid2.length() > 0) {
        Serial.println("Thu ket noi Wi-Fi phu: " + ssid2);
        WiFi.begin(ssid2.c_str(), pass2.c_str());
    } else {
        WiFi.begin(ssid.c_str(), password.c_str());
    }

    trySecondary = !trySecondary;
    nextWiFiRetryAt = now + jitteredDelay(wifiReconnectBackoffMs, WIFI_RECONNECT_JITTER_MS);
    if (wifiReconnectBackoffMs < WIFI_RECONNECT_MAX_MS) {
        unsigned long nextBackoff = wifiReconnectBackoffMs * 2;
        wifiReconnectBackoffMs = (nextBackoff > WIFI_RECONNECT_MAX_MS) ? WIFI_RECONNECT_MAX_MS : nextBackoff;
    }
  }

  if (wifiLostFlag && !isApMode && (now - wifiLostTime >= 300000)) {
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
          rainmakerReprovisionBackoffMs = RAINMAKER_REPROVISION_MS;
          nextRainmakerReprovisionAt = now + jitteredDelay(rainmakerReprovisionBackoffMs, RAINMAKER_REPROVISION_JITTER_MS);
      } else if (now >= nextRainmakerReprovisionAt) {
          if (!rainmakerInitialized) {
              logEvent("[RM] Reprovision skipped: init not ready.");
              nextRainmakerReprovisionAt = now + jitteredDelay(RAINMAKER_REPROVISION_MS, RAINMAKER_REPROVISION_JITTER_MS);
              return;
          }

          if (rainmakerProvisioningActive) {
              nextRainmakerReprovisionAt = now + jitteredDelay(RAINMAKER_REPROVISION_MS, RAINMAKER_REPROVISION_JITTER_MS);
              return;
          }

          rainmakerReprovisionAttempts++;
          rainmakerForceResetProvisioning = true;
          startRainMakerProvisioning();
          logEvent("[RM] Long WiFi outage, restart provisioning.");
          if (rainmakerReprovisionBackoffMs < RAINMAKER_REPROVISION_MAX_MS) {
              unsigned long nextBackoff = rainmakerReprovisionBackoffMs * 2;
              rainmakerReprovisionBackoffMs = (nextBackoff > RAINMAKER_REPROVISION_MAX_MS) ? RAINMAKER_REPROVISION_MAX_MS : nextBackoff;
          }
          nextRainmakerReprovisionAt = now + jitteredDelay(rainmakerReprovisionBackoffMs, RAINMAKER_REPROVISION_JITTER_MS);
      }
  } else {
      wifiLostFlag = false;
      wifiLostTime = 0;
      rainmakerReprovisionBackoffMs = RAINMAKER_REPROVISION_MS;
      nextRainmakerReprovisionAt = 0;
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

  bool rebootDue = false;
  if (stateMutex != NULL && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50))) {
      rebootDue = pendingReboot && (millis() - rebootTime >= 2000);
      xSemaphoreGive(stateMutex);
  } else {
      rebootDue = pendingReboot && (millis() - rebootTime >= 2000);
  }

  if (rebootDue) {
      if (now - lastRestartAt >= RESTART_GUARD_MS) {
          lastRestartAt = now;
          flushLogsToNvsIfNeeded(true);
          ESP.restart();
      } else {
          Serial.println("[GUARD] Bo qua reboot de tranh reboot-loop lien tuc.");
          if (stateMutex != NULL && xSemaphoreTake(stateMutex, pdMS_TO_TICKS(50))) {
              pendingReboot = false;
              xSemaphoreGive(stateMutex);
          } else {
              pendingReboot = false;
          }
      }
  }

#ifndef USE_RAINMAKER
  checkAPCycle();
#endif
}
