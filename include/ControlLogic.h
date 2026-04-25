#ifndef CONTROL_LOGIC_H
#define CONTROL_LOGIC_H

#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Config.h"

// Trạng thái các nút bấm từ xa (Blynk/App)
enum RemoteCommand {
    CMD_NONE,
    CMD_UP,
    CMD_DOWN,
    CMD_STOP
};

class ControlLogic {
public:
    ControlLogic();
    void begin();
    void loop(); // Chạy trong Task_Control trên Core 1

    // API nhận lệnh từ Core 0 (Network/Blynk)
    void executeRemoteCommand(RemoteCommand cmd);
    void togglePowerBox(bool turnOn);
    bool isPowerBoxOn();

    // Đồng bộ thời gian (NTP từ Core 0 báo về)
    void setLocalTime(int hour, int min);

private:
    Preferences preferences;
    bool currentPowerBoxState;

    // Quản lý Command Queue
    QueueHandle_t commandQueue;

    // Zero-Glitch Boot & NVS
    void initGPIO();
    void loadPersistedState();
    void savePowerBoxState(bool state);
    void latchPowerRelay(bool state);

    // Relay Control (Non-blocking)
    void processPendingCommand();
    void triggerRelay(uint8_t pin);

    // Auto Shutdown Relay
    uint8_t activeRelayPin;
    unsigned long relayTriggerTime;
    bool isRelayActive;

    // Daily Reboot Logic
    int currentHour;
    int currentMin;
    void checkDailyReboot();

    // Cảnh báo RAM
    void monitorHeap();
};

extern ControlLogic controlLogic;

#endif // CONTROL_LOGIC_H
