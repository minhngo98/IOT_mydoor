#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ElegantOTA.h>
#include "Config.h"
#include "ControlLogic.h"

#ifdef USE_BLYNK
// Avoid re-defining Blynk instance in multiple files
#endif

#ifdef USE_RAINMAKER
#include <esp_rmaker_core.h>
#include <esp_rmaker_standard_params.h>
#include <esp_rmaker_standard_devices.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#endif

class NetworkManager {
public:
    NetworkManager();
    void begin();
    void loop();

    // Cấu hình NVS
    String ssid;
    String password;
    String ssid2;
    String pass2;
    String adminUser;
    String adminPass;
    bool isFirstBoot; // Biến cờ xác định thiết bị có cần tạo Admin lần đầu không
    bool isOtaRunning; // Cờ báo hiệu đang chạy OTA

    // Cloud Configuration (Blynk)
    String blynkTemplate;
    String blynkName;
    String blynkAuth;
    String deviceId;
    String rescueApSsid;

    // Lịch trình Relay 4 (Nguồn Box)
    int8_t timezone;
    uint8_t onHour, onMin;
    uint8_t offHour, offMin;
    uint8_t scheduleDays; // Bitmask: bit 0=CN, bit 1=T2... bit 6=T7

    // Lịch trình Relay 5 (Đèn)
    uint8_t lightOnHour, lightOnMin;
    uint8_t lightOffHour, lightOffMin;
    uint8_t lightScheduleDays;

    bool isApMode;
    bool isConnected;

    // Quản lý Interrupt BOOT Button (Config) và Reset
    void handleInterruptConfig();
    void handleInterruptReset();

    // Đồng bộ credential OTA riêng với ElegantOTA
    void syncOtaAuth();

    // Đồng bộ thời gian (NTP) và đẩy trạng thái Blynk
    void syncLogsToCloud();
    void checkNTP();
    void pushCloudState();
    void pushBlynkState();
#ifdef USE_RAINMAKER
    void setupRainMaker();
    void startRainMakerProvisioning();
    void stopRainMakerProvisioning();
    void pushRainMakerState();
#endif
    void handleRemoteDoorCommand(RemoteCommand cmd);
    void handleRemotePowerCommand(bool turnOn);
    void handleRemoteLightCommand(bool turnOn);
    void onBlynkConnected();
    void onBlynkDisconnected();
    bool canAcceptRemoteCommands() const;

    // Log Terminal
    void logEvent(const String& message);
    String getRecentLogs() const;

private:
    AsyncWebServer server;
    Preferences preferences;
    SemaphoreHandle_t stringMutex; // Mutex bảo vệ truy cập biến String giữa các luồng

    // Lưu trữ log tối đa (Ring buffer cơ bản)
    String eventLogs[15];
    int logIndex;
    int lastBlynkSyncLogIndex;

    unsigned long lastWiFiCheck;

    // Quản lý AP Cycle
    unsigned long apStartTime;
    unsigned long apOfflineTime;
    unsigned long wifiLostTime;
    bool wifiLostFlag;

    // Cờ nút nhấn Reset
    unsigned long resetBtnPressTime;
    bool resetBtnPressed;

    // Bảo mật & Rate Limit
    uint8_t failedAuthCount;
    unsigned long lockoutStartTime;
    bool isLockedOut;
    String rescueApPass;
    bool claimRequired;

    // Cờ báo ngắt
    volatile bool interruptConfigTriggered;
    volatile bool interruptResetTriggered;

    // Cờ reboot non-blocking
    bool pendingReboot;
    unsigned long rebootTime;

    // Helper functions cho AP cycle và Schedule
    bool isScheduleActiveNow(int currentMins);
    bool isLightScheduleActiveNow(int currentMins);

    void loadConfig();
    void setupAP();
    void setupSTA();
    void setupWebServer();
    void checkAPCycle();
    void handleWiFi();
    void handleResetButton();
    bool checkAuth(AsyncWebServerRequest *request);
    void handleBlynk();
    void resetBlynkSessionState();
    String safeGetString(const String& str);
    void safeSetString(String& target, const String& value);

    bool webServerInitialized;
    bool otaInitialized;
    unsigned long lastBlynkConnectAttempt;
    unsigned long blynkReconnectBackoffMs;
    unsigned long blynkRemoteGuardUntil;
    bool blynkWasConnected;
    bool blynkInvalidToken;

#ifdef USE_RAINMAKER
    esp_rmaker_node_t *rainmakerNode;
    esp_rmaker_device_t *doorDevice;
    esp_rmaker_device_t *powerBoxDevice;
    esp_rmaker_device_t *lightDevice;

    bool rainmakerInitialized;
    EventGroupHandle_t wifiEventGroup;
    #define WIFI_CONNECTED_BIT BIT0
    #define RM_MQTT_CONNECTED_BIT BIT1
    #define RM_FACTORY_RESET_BIT BIT2

    static esp_err_t write_cb_wrapper(const rm_param_val_t val, void *priv_data);
    static void rainmaker_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
#endif
};

extern NetworkManager netManager;

#endif
