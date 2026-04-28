#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <cstring>
#include "Config.h"
#include "NetworkManager.h"
#include "WebUI.h"

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

void sendHtml(AsyncWebServerRequest* request, const char* html) {
  request->send(200, "text/html", reinterpret_cast<const uint8_t*>(html), strlen(html));
}
}

// ISR Handler cho nút BOOT (Phải đặt ở ngoài class)
void IRAM_ATTR isr_config_button() {
  netManager.handleInterruptConfig();
}

void IRAM_ATTR isr_reset_button() {
  netManager.handleInterruptReset();
}

NetworkManager::NetworkManager() : server(80), isApMode(false), isConnected(false),
  lastWiFiCheck(0), apStartTime(0), apOfflineTime(0), wifiLostTime(0), wifiLostFlag(false),
  failedAuthCount(0), lockoutStartTime(0), isLockedOut(false), interruptConfigTriggered(false),
  interruptResetTriggered(false), claimRequired(false), webServerInitialized(false), isFirstBoot(false),
  otaInitialized(false), lastBlynkConnectAttempt(0), blynkReconnectBackoffMs(BLYNK_RECONNECT_BASE_MS),
  blynkRemoteGuardUntil(0), blynkWasConnected(false), blynkInvalidToken(false), logIndex(0), lastBlynkSyncLogIndex(0), isOtaRunning(false), pendingReboot(false), rebootTime(0) {
    stringMutex = NULL;
#ifdef USE_RAINMAKER
    rainmakerNode = NULL;
    doorDevice = NULL;
    powerBoxDevice = NULL;
    lightDevice = NULL;
    rainmakerInitialized = false;
    wifiEventGroup = xEventGroupCreate();
#endif
}

String NetworkManager::safeGetString(const String& str) {
    String copy;
    if (xSemaphoreTake(stringMutex, portMAX_DELAY)) {
        copy = str;
        xSemaphoreGive(stringMutex);
    }
    return copy;
}

void NetworkManager::safeSetString(String& target, const String& value) {
    if (xSemaphoreTake(stringMutex, portMAX_DELAY)) {
        target = value;
        xSemaphoreGive(stringMutex);
    }
}

void NetworkManager::logEvent(const String& message) {
    // 1. In ra Serial Console
    Serial.println(message);

    // 2. Thêm thời gian nếu có thể (lấy từ timeinfo)
    struct tm timeinfo;
    String timeStr = "";
    if (getLocalTime(&timeinfo, 10)) {
        char buf[20];
        snprintf(buf, sizeof(buf), "[%02d:%02d:%02d] ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        timeStr = String(buf);
    } else {
        timeStr = "[--:--:--] ";
    }

    String logLine = timeStr + message;

    // 3. Đưa vào mảng log (Ring Buffer đơn giản 15 dòng cho WebUI)
    if (xSemaphoreTake(stringMutex, portMAX_DELAY)) {
        eventLogs[logIndex] = logLine;
        logIndex = (logIndex + 1) % 15;
        // Nếu vòng ring buffer đè lên dữ liệu chưa sync, phải tăng lastSync để đuổi theo
        if (logIndex == lastBlynkSyncLogIndex) {
            lastBlynkSyncLogIndex = (lastBlynkSyncLogIndex + 1) % 15;
        }
        xSemaphoreGive(stringMutex);
    }
}

void NetworkManager::syncLogsToCloud() {
    if (lastBlynkSyncLogIndex == logIndex) return; // Không có log mới

    if (xSemaphoreTake(stringMutex, portMAX_DELAY)) {
        while (lastBlynkSyncLogIndex != logIndex) {
            String logLine = eventLogs[lastBlynkSyncLogIndex];

            // Gửi lên Cloud
#ifdef USE_BLYNK
            if (Blynk.connected()) {
                Blynk.virtualWrite(VPIN_TERMINAL, logLine + "\n");
            }
#endif
#ifdef USE_RAINMAKER
            // Future: push logs to RainMaker
#endif

            lastBlynkSyncLogIndex = (lastBlynkSyncLogIndex + 1) % 15;
        }
        xSemaphoreGive(stringMutex);
    }
}

String NetworkManager::getRecentLogs() const {
    String output = "";
    if (xSemaphoreTake(stringMutex, portMAX_DELAY)) {
        int count = 0;
        int idx = logIndex;
        // Đi lùi từ log mới nhất về log cũ nhất
        while (count < 15) {
            idx--;
            if (idx < 0) idx = 14;
            if (eventLogs[idx].length() > 0) {
                output += eventLogs[idx] + "\n";
            }
            count++;
        }
        xSemaphoreGive(stringMutex);
    }
    return output;
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
  ESP_ERROR_CHECK(esp_wifi_init());

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

  setupWebServer();
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
  if (rescueApSsid == "") {
    rescueApSsid = "esp32";
    preferences.putString("rescue_ssid", rescueApSsid);
  }

  rescueApPass = preferences.getString("rescue_pass", "");
  bool generatedRescuePass = false;
  if (rescueApPass.length() < 8) {
    rescueApPass = "12345678";
    preferences.putString("rescue_pass", rescueApPass);
    generatedRescuePass = true;
  }

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

  preferences.end();

  if (claimRequired) {
    if (ssid == "") {
      Serial.println("[SECURITY] Thiet bi moi: bat buoc tao Admin truoc khi vao che do van hanh.");
    } else {
      Serial.println("[SECURITY] Firmware nang cap chua co Admin. Bat buoc claim lai qua Rescue AP.");
    }
  }

  Serial.println("[NVS] Da tai cau hinh: WiFi=" + ssid + ", DeviceID=" + deviceId + ", RescueAP=" + rescueApSsid);
  Serial.println("[SECURITY] WARNING: Mật khẩu Rescue AP hiện tại là: " + rescueApPass);
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

  // Ghi đè pass tạm thời và hiện SSID để dễ kết nối
  rescueApPass = "12345678";
  WiFi.softAP(rescueApSsid.c_str(), rescueApPass.c_str(), 1, 0);
  Serial.println("[AP] Rescue AP dang hoat dong. Hidden=0, Pass=12345678, IP: 10.10.10.1");

  if (ssid != "") {
      WiFi.begin(ssid.c_str(), password.c_str());
  }

  apStartTime = millis();
  setupWebServer();
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

    doorDevice = esp_rmaker_device_create("door-control", "Door Control", NULL);
    if(doorDevice) {
        esp_rmaker_device_add_cb(doorDevice, write_cb_wrapper, (void*)0x00);
        esp_rmaker_node_add_device(rainmakerNode, doorDevice);

        esp_rmaker_param_t *door_up_param = esp_rmaker_param_create("up", RMakerParamType_Bool, rm_false());
        esp_rmaker_param_add_ui_type(door_up_param, RMAKER_UI_BUTTON);
        esp_rmaker_device_add_param(doorDevice, door_up_param);

        esp_rmaker_param_t *door_down_param = esp_rmaker_param_create("down", RMakerParamType_Bool, rm_false());
        esp_rmaker_param_add_ui_type(door_down_param, RMAKER_UI_BUTTON);
        esp_rmaker_device_add_param(doorDevice, door_down_param);

        esp_rmaker_param_t *door_stop_param = esp_rmaker_param_create("stop", RMakerParamType_Bool, rm_false());
        esp_rmaker_param_add_ui_type(door_stop_param, RMAKER_UI_BUTTON);
        esp_rmaker_device_add_param(doorDevice, door_stop_param);

        esp_rmaker_param_t *door_state_param = esp_rmaker_param_create("state", RMakerParamType_String, rm_str("STOPPED"));
        esp_rmaker_param_add_ui_type(door_state_param, RMAKER_UI_TEXT);
        esp_rmaker_param_set_flags(door_state_param, READ_ACCESS);
        esp_rmaker_device_add_param(doorDevice, door_state_param);
    }

    powerBoxDevice = esp_rmaker_switch_device_create("power-box", "Power Box", controlLogic.isPowerBoxOn());
    if(powerBoxDevice) {
        esp_rmaker_device_add_cb(powerBoxDevice, write_cb_wrapper, (void*)0x01);
        esp_rmaker_node_add_device(rainmakerNode, powerBoxDevice);
    }

    lightDevice = esp_rmaker_switch_device_create("light", "Light Control", controlLogic.isLightOn());
    if(lightDevice) {
        esp_rmaker_device_add_cb(lightDevice, write_cb_wrapper, (void*)0x02);
        esp_rmaker_node_add_device(rainmakerNode, lightDevice);
    }

    esp_rmaker_start();
    rainmakerInitialized = true;
    ESP_LOGI(TAG, "RainMaker initialized and started.");
}

void NetworkManager::startRainMakerProvisioning() {
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_rmaker_start_provisioning(RMAKER_PROV_BLE, NULL, NULL);
    ESP_LOGI(TAG, "RainMaker BLE provisioning started.");
}

void NetworkManager::stopRainMakerProvisioning() {
    esp_rmaker_stop_provisioning();
    ESP_LOGI(TAG, "RainMaker provisioning stopped.");
}

void NetworkManager::pushRainMakerState() {
    if (!rainmakerInitialized || !esp_rmaker_is_connected()) return;

    esp_rmaker_param_update_and_report(
        esp_rmaker_device_get_param_by_name(powerBoxDevice, "power"),
        controlLogic.isPowerBoxOn() ? rm_true() : rm_false()
    );

    esp_rmaker_param_update_and_report(
        esp_rmaker_device_get_param_by_name(lightDevice, "power"),
        controlLogic.isLightOn() ? rm_true() : rm_false()
    );

    // TODO: Connect this to actual door state feedback when implemented in ControlLogic
    esp_rmaker_param_update_and_report(
        esp_rmaker_device_get_param_by_name(doorDevice, "state"),
        rm_str("STOPPED")
    );
}

esp_err_t NetworkManager::write_cb_wrapper(const rm_param_val_t val, void *priv_data) {
    uint32_t device_id = (uint32_t)priv_data;
    esp_rmaker_param_t *param = esp_rmaker_param_get_parent(esp_rmaker_param_get_parent(esp_rmaker_param_get_handle(val)));

    if (!param) return ESP_FAIL;

    if (device_id == 0x01) {
        bool turnOn = rm_param_val_to_bool(val);
        controlLogic.executeRemoteCommand(turnOn ? CMD_LIGHT_ON : CMD_LIGHT_OFF); // Reusing CMD structure, might need new specific ones or toggle
        netManager.logEvent("Power Box: " + String(turnOn ? "ON" : "OFF") + " (RainMaker)");
        controlLogic.togglePowerBox(turnOn);
    } else if (device_id == 0x02) {
        bool turnOn = rm_param_val_to_bool(val);
        controlLogic.executeRemoteCommand(turnOn ? CMD_LIGHT_ON : CMD_LIGHT_OFF);
        netManager.logEvent("Light: " + String(turnOn ? "ON" : "OFF") + " (RainMaker)");
        controlLogic.toggleLight(turnOn);
    } else if (device_id == 0x00) {
        const char* param_name = esp_rmaker_param_get_name(param);
        if (strcmp(param_name, "up") == 0 && rm_param_val_to_bool(val)) {
            controlLogic.executeRemoteCommand(CMD_UP);
            netManager.logEvent("Door: UP (RainMaker)");
        } else if (strcmp(param_name, "down") == 0 && rm_param_val_to_bool(val)) {
            controlLogic.executeRemoteCommand(CMD_DOWN);
            netManager.logEvent("Door: DOWN (RainMaker)");
        } else if (strcmp(param_name, "stop") == 0 && rm_param_val_to_bool(val)) {
            controlLogic.executeRemoteCommand(CMD_STOP);
            netManager.logEvent("Door: STOP (RainMaker)");
        }
        esp_rmaker_param_update_and_report(param, rm_false());
    }
    netManager.pushCloudState();
    return ESP_OK;
}

void NetworkManager::rainmaker_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if (RMAKER_EVENT == event_base) {
        switch (event_id) {
            case RMAKER_EVENT_MQTT_CONNECTED:
                ESP_LOGI(TAG, "RainMaker: MQTT Connected.");
                xEventGroupSetBits(netManager.wifiEventGroup, RM_MQTT_CONNECTED_BIT);
                netManager.pushRainMakerState();
                netManager.stopRainMakerProvisioning();
                break;
            case RMAKER_EVENT_MQTT_DISCONNECTED:
                ESP_LOGI(TAG, "RainMaker: MQTT Disconnected.");
                xEventGroupClearBits(netManager.wifiEventGroup, RM_MQTT_CONNECTED_BIT);
                break;
            case RMAKER_EVENT_FACTORY_RESET:
                ESP_LOGW(TAG, "RainMaker: Factory Reset Initiated. Erasing NVS and restarting...");
                xEventGroupSetBits(netManager.wifiEventGroup, RM_FACTORY_RESET_BIT);
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
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
            ESP_LOGI(TAG, "Wi-Fi Connected. IP: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(netManager.wifiEventGroup, WIFI_CONNECTED_BIT);
            netManager.isConnected = true;
            netManager.stopRainMakerProvisioning();
        }
    }
}
#endif

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
        } else {
            // Chậm phản hồi 1 giây để chống Bruteforce bằng cách lưu thời gian sai trước (Tùy chọn)
            // Hiện tại chúng ta sẽ trả về ngay hoặc nếu muốn delay thì delay non-blocking
            // Nhưng đối với checkAuth thì tốt nhất ta chỉ ghi nhận đếm số lần sai và cho qua, vì đây là luồng async_tcp
        }
        request->requestAuthentication("MyDoor Config Admin");
        return false;
    }

    // Auth thành công
    failedAuthCount = 0;
    apStartTime = millis(); // Reset chu kỳ AP vì có tương tác
    return true;
}

void NetworkManager::syncOtaAuth() {
    // OTA uses the Admin credential pair and is enabled only after the device is claimed.
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
                  ",\"rescue_ssid\":\"" + netManager.safeGetString(netManager.rescueApSsid) + "\"";

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

    if(request->hasParam("admin_user", true) && request->hasParam("admin_pass", true)) {
      String newUser = request->getParam("admin_user", true)->value();
      String newPass = request->getParam("admin_pass", true)->value();
      if (!isStrongAdminInput(newUser, newPass)) {
        return request->send(400, "text/plain", "Admin credentials are too weak.");
      }

      Preferences p; p.begin("mydoor", false);
      p.putString("admin_user", newUser);
      p.putString("admin_pass", newPass);
      p.end();
      netManager.loadConfig();
      netManager.syncOtaAuth(); // Đồng bộ ngay lập tức sang OTA
      request->send(200, "text/plain", "OK");
    } else request->send(400, "text/plain", "Bad Request");
  });

  server.on("/save_rescue_ap", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    if(request->hasParam("rescue_ap_ssid", true) && request->hasParam("rescue_ap_pass", true)) {
      String newSsid = request->getParam("rescue_ap_ssid", true)->value();
      String newPass = request->getParam("rescue_ap_pass", true)->value();

      if (newSsid.length() < 4 || newPass.length() < 8 || !hasSpecialChar(newPass)) {
        return request->send(400, "text/plain", "SSID hoặc Mật khẩu quá ngắn, hoặc Mật khẩu thiếu ký tự đặc biệt (VD: @, #, $, ...).");
      }

      Preferences p; p.begin("mydoor", false);
      p.putString("rescue_ssid", newSsid);
      p.putString("rescue_pass", newPass);
      p.end();

      netManager.safeSetString(netManager.rescueApSsid, newSsid);
      netManager.safeSetString(netManager.rescueApPass, newPass);

      request->send(200, "text/plain", "OK");

      if (netManager.isApMode) {
        netManager.pendingReboot = true;
        netManager.rebootTime = millis();
      }
    } else request->send(400, "text/plain", "Bad Request");
  });

  // API Khởi động lại
  server.on("/reboot", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (!netManager.checkAuth(request)) return;

    request->send(200, "text/plain", "Rebooting");
    netManager.pendingReboot = true;
    netManager.rebootTime = millis();
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
        controlLogic.togglePowerBox(turnOn);
        netManager.logEvent("Nguon Box: " + String(turnOn ? "BAT" : "TAT") + " (WebUI)");
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
        controlLogic.toggleLight(turnOn);
        netManager.logEvent("Den: " + String(turnOn ? "BAT" : "TAT") + " (WebUI)");
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

void NetworkManager::checkAPCycle() {
  bool shouldCycleAp = wifiLostFlag || ssid == "";

  if (isApMode) {
      if (millis() - apStartTime >= AP_CYCLE_ON_MS) {
          // Hết 10 phút, Tắt AP, nghỉ 5 phút
          Serial.println("[AP CYCLE] AP da bat 10 phut, tat AP trong 5 phut...");
          isApMode = false;
          WiFi.softAPdisconnect(true);
          WiFi.mode(WIFI_STA); // Trả về STA để dò mạng
          apOfflineTime = millis();
      }
  } else if (shouldCycleAp) {
      if (millis() - apOfflineTime >= AP_CYCLE_OFF_MS) {
          // Hết 5 phút nghỉ, bật lại AP 10 phút
          Serial.println("[AP CYCLE] AP da nghi 5 phut, bat lai AP 10 phut...");
          setupAP();
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

  if (scheduleActiveNow && !controlLogic.isPowerBoxOn()) {
      Serial.printf("[AUTO] %02d:%02d - Den gio mo Box Cua\n", timeinfo.tm_hour, timeinfo.tm_min);
      controlLogic.togglePowerBox(true);
      pushCloudState();
  }
  else if (!scheduleActiveNow && controlLogic.isPowerBoxOn()) {
      Serial.printf("[AUTO] %02d:%02d - Den gio dong Box Cua\n", timeinfo.tm_hour, timeinfo.tm_min);
      controlLogic.togglePowerBox(false);
      pushCloudState();
  }

  // Logic tự động bật/tắt Đèn (Relay 5)
  bool lightScheduleActiveNow = isLightScheduleActiveNow(currentMins);

  if (lightScheduleActiveNow && !controlLogic.isLightOn()) {
      Serial.printf("[AUTO] %02d:%02d - Den gio bat Den\n", timeinfo.tm_hour, timeinfo.tm_min);
      controlLogic.toggleLight(true);
      pushCloudState();
  }
  else if (!lightScheduleActiveNow && controlLogic.isLightOn()) {
      Serial.printf("[AUTO] %02d:%02d - Den gio tat Den\n", timeinfo.tm_hour, timeinfo.tm_min);
      controlLogic.toggleLight(false);
      pushCloudState();
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
  controlLogic.executeRemoteCommand(cmd);
}

void NetworkManager::handleRemotePowerCommand(bool turnOn) {
#ifdef USE_BLYNK
  if (!canAcceptRemoteCommands()) {
      Serial.println("[BLYNK] Bo qua lenh nguon do server dang replay trang thai cu.");
      pushCloudState();
      return;
  }
  logEvent("Nguon Box: " + String(turnOn ? "BAT" : "TAT") + " (Blynk)");
#endif
  controlLogic.togglePowerBox(turnOn);
  pushCloudState();
}

void NetworkManager::handleRemoteLightCommand(bool turnOn) {
#ifdef USE_BLYNK
  if (!canAcceptRemoteCommands()) {
      Serial.println("[BLYNK] Bo qua lenh den do server dang replay trang thai cu.");
      pushCloudState();
      return;
  }
  logEvent("Den: " + String(turnOn ? "BAT" : "TAT") + " (Blynk)");
#endif
  controlLogic.toggleLight(turnOn);
  pushCloudState();
}

void NetworkManager::onBlynkConnected() {
#ifdef USE_BLYNK
  blynkWasConnected = true;
  blynkInvalidToken = false;
  blynkReconnectBackoffMs = BLYNK_RECONNECT_BASE_MS;
  blynkRemoteGuardUntil = millis() + BLYNK_POST_CONNECT_GUARD_MS;
  Serial.println("[BLYNK] Cloud da ket noi. Dang dong bo trang thai local va tam khoa lenh replay.");
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
  if (isApMode) {
    isConnected = false;
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    isConnected = true;
    wifiLostFlag = false;
    return;
  }

  isConnected = false;

  if (!wifiLostFlag) {
      wifiLostFlag = true;
      wifiLostTime = millis();
  }

  if (ssid != "" && millis() - lastWiFiCheck > WIFI_TIMEOUT_MS) {
    lastWiFiCheck = millis();

    static bool trySecondary = false;

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

  if (wifiLostFlag && millis() - wifiLostTime >= 300000 && !isApMode) {
      Serial.println("[AP] Mat ket noi 5 phut, tu dong bat Rescue AP!");
      setupAP();
  }

  // Nếu đang ở AP mode nhưng lại có connection STA được khôi phục ngầm (WIFI_AP_STA), tắt cờ lost
  if (isApMode && WiFi.status() == WL_CONNECTED) {
      isConnected = true;
      wifiLostFlag = false;
  }
}

void NetworkManager::loop() {
  // Xử lý Ngắt Nút BOOT (Wake-up AP)
  if (interruptConfigTriggered) {
    interruptConfigTriggered = false;
    static unsigned long lastPress = 0;
    if (millis() - lastPress > 200) { // Chống dội phím cơ bản (200ms)
        lastPress = millis();
        // Kiểm tra xem nút có được giữ 5 giây không (Non-blocking delay qua FreeRTOS)
        int holdTime = 0;
        while(digitalRead(PIN_BTN_CONFIG) == LOW && holdTime < 50) {
            esp_task_wdt_reset(); // Feed WDT to prevent panic
            vTaskDelay(pdMS_TO_TICKS(YIELD_BUTTON_MS)); // Nhường CPU cho Core 0 Network
            holdTime++;
        }
        if (holdTime >= 50) {
            Serial.println("\n[SYSTEM] BAT CHE DO CAU HINH WIFI (AP) DO NGUOI DUNG BAM NUT!");
            isLockedOut = false; // Mở khóa AP luôn
            failedAuthCount = 0;
            WiFi.disconnect(true);
            setupAP();
        }
    }
  }

  // Xử lý Nút Reset Cứng (Reboot / Factory Reset)
  if (interruptResetTriggered) {
    interruptResetTriggered = false;
    static unsigned long lastResetPress = 0;
    if (millis() - lastResetPress > 200) { // Chống dội phím cơ bản (200ms)
        lastResetPress = millis();
        int holdTime = 0;
        // Kiểm tra tối đa 120 chu kỳ (~12s)
        while(digitalRead(PIN_BTN_RESET) == LOW && holdTime < 120) {
            esp_task_wdt_reset(); // Feed WDT to prevent panic
            vTaskDelay(pdMS_TO_TICKS(YIELD_BUTTON_MS));
            holdTime++;
        }

        if (holdTime >= 100) { // Giữ ~10s -> FACTORY RESET (Xóa toàn bộ Cấu hình)
            Serial.println("\n[FACTORY RESET] Đang xóa toàn bộ cấu hình...");
            for (int i=0; i<5; i++) { // Nháy đèn Vàng 5 lần
                digitalWrite(PIN_LED_WARN, LED_ON);
                vTaskDelay(pdMS_TO_TICKS(200));
                digitalWrite(PIN_LED_WARN, LED_OFF);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            // Xóa dữ liệu
            Preferences p;
            p.begin("mydoor", false); p.clear(); p.end();
            p.begin("mydoor_state", false); p.clear(); p.end();

            Serial.println("[FACTORY RESET] Hoàn tất. Đang khởi động lại hệ thống...");
            ESP.restart();
        }
        else if (holdTime >= 30) { // Giữ ~3s -> REBOOT Hệ thống
            Serial.println("\n[REBOOT] Lệnh Reboot từ nút bấm cứng.");
            for (int i=0; i<3; i++) { // Nháy đèn Vàng 3 lần
                digitalWrite(PIN_LED_WARN, LED_ON);
                vTaskDelay(pdMS_TO_TICKS(200));
                digitalWrite(PIN_LED_WARN, LED_OFF);
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            ESP.restart();
        }
    }
  }

#ifndef USE_RAINMAKER
  handleWiFi();
#endif
  handleBlynk();

  static unsigned long lastNTPCheck = 0;
  if (millis() - lastNTPCheck >= 60000) { // Kiểm tra NTP và Auto Schedule mỗi 1 phút
      lastNTPCheck = millis();
      checkNTP();
  }

  ElegantOTA.loop();

  // Đồng bộ Logs lên Cloud (chỉ chạy ở Core 0)
  syncLogsToCloud();

  // Quản lý Khóa AP 30 Phút
  if (isLockedOut && millis() - lockoutStartTime >= AP_LOCKOUT_MS) {
      isLockedOut = false;
      failedAuthCount = 0;
      Serial.println("[SECURITY] Hết 30 phút khóa AP. Mở khóa.");
  }

  // Quản lý Reboot Non-blocking
  if (pendingReboot && millis() - rebootTime >= 2000) {
      ESP.restart();
  }

  // Gọi checkAPCycle mỗi vòng lặp bất kể chế độ
#ifndef USE_RAINMAKER
  checkAPCycle();
#endif
}
