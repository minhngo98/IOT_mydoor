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
    String blynk_tmpl;
    String blynk_name;
    String blynk_auth;
    String deviceId;
    String rescueApSsid;

    // Lịch trình Relay 4
    int8_t timezone;
    uint8_t on_hour, on_min;
    uint8_t off_hour, off_min;
    uint8_t schedule_days; // Bitmask: bit 0=CN, bit 1=T2... bit 6=T7

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
    void onBlynkConnected();
    void onBlynkDisconnected();
    bool canAcceptRemoteCommands() const;
    void rotateRescueApCredential();
    void rotateOtaCredential();

private:
    AsyncWebServer server;
    Preferences preferences;

    unsigned long lastWiFiCheck;

    // Quản lý AP Cycle
    unsigned long apStartTime;
    unsigned long apOfflineTime;
    unsigned long wifiLostTime;
    bool wifiLostFlag;

    // Bảo mật & Rate Limit
    uint8_t failedAuthCount;
    unsigned long lockoutStartTime;
    bool isLockedOut;
    String rescueApPass;
    String otaUser;
    String otaPass;
    bool claimRequired;

    // Cờ báo ngắt
    volatile bool interruptConfigTriggered;
    volatile bool interruptResetTriggered;

    // Helper functions cho AP cycle và Schedule
    bool isScheduleActiveNow(int currentMins);

    void loadConfig();
    void setupAP();
    void setupSTA();
    void setupWebServer();
    void checkAPCycle();
    void handleWiFi();
    bool checkAuth(AsyncWebServerRequest *request);
    void handleBlynk();
    void resetBlynkSessionState();

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
