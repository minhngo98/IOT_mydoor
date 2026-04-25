#include "ControlLogic.h"

namespace {
RTC_DATA_ATTR uint32_t rtcPowerStateMagic = 0;
RTC_DATA_ATTR bool rtcPowerState = true;
constexpr uint32_t RTC_POWER_STATE_MAGIC = 0x4D445057;
}

ControlLogic controlLogic;

ControlLogic::ControlLogic() :
    currentPowerBoxState((rtcPowerStateMagic == RTC_POWER_STATE_MAGIC) ? rtcPowerState : true),
    activeRelayPin(0), relayTriggerTime(0), isRelayActive(false),
    currentHour(-1), currentMin(-1) {
}

void ControlLogic::begin() {
    commandQueue = xQueueCreate(5, sizeof(RemoteCommand));
    initGPIO();
    loadPersistedState();
}

void ControlLogic::initGPIO() {
    // 1. Tắt hết LED trước
    pinMode(PIN_LED_WIFI, OUTPUT); digitalWrite(PIN_LED_WIFI, LED_OFF);
    pinMode(PIN_LED_READY, OUTPUT); digitalWrite(PIN_LED_READY, LED_OFF);
    pinMode(PIN_LED_FAULT, OUTPUT); digitalWrite(PIN_LED_FAULT, LED_OFF);
    pinMode(PIN_LED_WARN, OUTPUT); digitalWrite(PIN_LED_WARN, LED_OFF);

    // 2. Chốt logic RELAY an toàn (Zero-Glitch Boot)
    digitalWrite(PIN_RELAY_UP, RELAY_OFF);
    digitalWrite(PIN_RELAY_DOWN, RELAY_OFF);
    digitalWrite(PIN_RELAY_STOP, RELAY_OFF);

    pinMode(PIN_RELAY_UP, OUTPUT);
    pinMode(PIN_RELAY_DOWN, OUTPUT);
    pinMode(PIN_RELAY_STOP, OUTPUT);
    latchPowerRelay(currentPowerBoxState);

    // Pin Nguồn Tổng sẽ được setup ở loadPersistedState()
}

void ControlLogic::loadPersistedState() {
    preferences.begin("mydoor", false);

    // Sử dụng lại namespace và key cũ ("mydoor" / "box_power") để tương thích
    currentPowerBoxState = preferences.getBool("box_power", currentPowerBoxState);
    preferences.end();

    // Gán trạng thái vào Relay ngay lập tức
    latchPowerRelay(currentPowerBoxState);

    Serial.printf("[CONTROL] Da khoi tao Zero-Glitch. Nguon Box Cua: %s\n", currentPowerBoxState ? "ON" : "OFF");
}

void ControlLogic::savePowerBoxState(bool state) {
    if (currentPowerBoxState != state) { // Dirty Flag check
        currentPowerBoxState = state;
        latchPowerRelay(state);

        preferences.begin("mydoor", false);
        preferences.putBool("box_power", state);
        preferences.end();

        Serial.printf("[CONTROL] (Flash Saved) Nguon Box Cua thay doi: %s\n", state ? "ON" : "OFF");
    }
}

void ControlLogic::latchPowerRelay(bool state) {
    rtcPowerStateMagic = RTC_POWER_STATE_MAGIC;
    rtcPowerState = state;
    digitalWrite(PIN_RELAY_POWER, state ? POWER_ON : POWER_OFF);
    pinMode(PIN_RELAY_POWER, OUTPUT);
}

void ControlLogic::togglePowerBox(bool turnOn) {
    savePowerBoxState(turnOn);
}

bool ControlLogic::isPowerBoxOn() {
    return currentPowerBoxState;
}

void ControlLogic::executeRemoteCommand(RemoteCommand cmd) {
    // Nhận lệnh từ Task Network (Core 0), đưa vào Queue để Core 1 xử lý
    if (commandQueue != NULL) {
        xQueueSend(commandQueue, &cmd, 0);
    }
}

void ControlLogic::processPendingCommand() {
    if (isRelayActive) return;

    RemoteCommand pendingCmd = CMD_NONE;
    if (commandQueue != NULL && xQueueReceive(commandQueue, &pendingCmd, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Chặn lệnh cửa nếu Nguồn Box (Relay 4) đang TẮT
        if (!currentPowerBoxState) {
            if (pendingCmd == CMD_UP || pendingCmd == CMD_DOWN || pendingCmd == CMD_STOP) {
                Serial.println("[LOI] Box Nguon dang TAT. Khong the dieu khien cua!");
                return;
            }
        }

        switch (pendingCmd) {
            case CMD_UP:
                triggerRelay(PIN_RELAY_UP);
                break;
            case CMD_DOWN:
                triggerRelay(PIN_RELAY_DOWN);
                break;
            case CMD_STOP:
                triggerRelay(PIN_RELAY_STOP);
                break;
            default:
                break;
        }
    }
}

void ControlLogic::triggerRelay(uint8_t pin) {
    Serial.printf("[CONTROL] Kich hoat Relay pin %d\n", pin);
    digitalWrite(pin, RELAY_ON);
    activeRelayPin = pin;
    relayTriggerTime = millis();
    isRelayActive = true;
}

void ControlLogic::setLocalTime(int hour, int min) {
    currentHour = hour;
    currentMin = min;
}

void ControlLogic::checkDailyReboot() {
    // Khởi động lại hệ thống vào lúc 03:00 sáng
    if (currentHour == DAILY_REBOOT_HOUR && currentMin == 0 && !isRelayActive) {
        Serial.println("\n[MAINTENANCE] Dang thuc hien Daily Reboot luc 3:00 AM...");
        delay(1000);
        ESP.restart();
    }
}

void ControlLogic::monitorHeap() {
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < MIN_FREE_HEAP) {
        Serial.printf("\n[CRITICAL] Free Heap qua thap (%d bytes)! Dang Reboot bao ve RAM...\n", freeHeap);
        delay(500);
        ESP.restart();
    }
}

void ControlLogic::loop() {
    // 1. Tự động nhả Relay sau DOOR_PULSE_MS
    if (isRelayActive && (millis() - relayTriggerTime >= DOOR_PULSE_MS)) {
        digitalWrite(activeRelayPin, RELAY_OFF);
        isRelayActive = false;
        activeRelayPin = 0;
        Serial.println("[CONTROL] Da ngat Pulse Relay (nha nut)");
    }

    // 2. Xử lý hàng đợi lệnh từ Core 0
    processPendingCommand();

    // 3. Các tác vụ định kỳ (mỗi giây)
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck >= 1000) {
        lastCheck = millis();
        monitorHeap();
        checkDailyReboot();
    }

    // Thêm delay ngắn để nhường CPU cho Watchdog
    vTaskDelay(pdMS_TO_TICKS(10));
}
