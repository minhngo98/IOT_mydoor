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
    void checkNTP();
    void pushBlynkState();
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
};

extern NetworkManager netManager;

#endif
