#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <ElegantOTA.h>
#include "Config.h"

class NetworkManager {
public:
    NetworkManager();
    void begin();
    void loop();

    // NVS Configuration
    String ssid;
    String password;
    String ssid2;      // Wi-Fi phụ
    String password4;  // Password Wi-Fi phụ (đặt là password4 tạm hoặc pass2)
    String pass2;
    String adminUser;
    String adminPass;

    // Cloud Configuration (Blynk)
    String blynk_tmpl;
    String blynk_name;
    String blynk_auth;

    // Lịch trình Relay 4
    int8_t timezone;
    uint8_t on_hour, on_min;
    uint8_t off_hour, off_min;
    uint8_t schedule_days; // Bitmask: bit 0=CN, bit 1=T2... bit 6=T7

    bool isApMode;
    bool isConnected;

    // Quản lý Interrupt BOOT Button
    void handleInterruptBoot();

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

    // Cờ báo ngắt
    volatile bool interruptTriggered;

    void loadConfig();
    void setupAP();
    void setupSTA();
    void setupWebServer();
    void checkAPCycle();
};

extern NetworkManager netManager;

#endif