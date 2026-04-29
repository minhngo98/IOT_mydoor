#include <Arduino.h>
#include <esp_task_wdt.h>

#include "Config.h"
#include "NetworkManager.h"
#include "ControlLogic.h"

TaskHandle_t TaskNetworkHandle = NULL;
TaskHandle_t TaskControlHandle = NULL;

void Task_Network(void *pvParameters) {
  Serial.println("[TASK] Network Task khoi chay tren Core 0.");
  esp_task_wdt_add(NULL);

  netManager.begin();

  for (;;) {
    esp_task_wdt_reset();
    netManager.loop();
    vTaskDelay(pdMS_TO_TICKS(YIELD_WIFI_MS)); // Nhường CPU cho WiFi Stack
  }
}

void Task_Control(void *pvParameters) {
  Serial.println("[TASK] Control Task khoi chay tren Core 1 (Uu tien cao).");
  esp_task_wdt_add(NULL);

  for (;;) {
    esp_task_wdt_reset();
    controlLogic.loop();
    vTaskDelay(pdMS_TO_TICKS(YIELD_CONTROL_MS)); // Nhịp điều khiển Core 1 tách biệt với luồng mạng
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== MYDOOR ESP32 (DUAL-CORE & 10Y STABILITY) ===");

  // 0. Zero-Glitch Boot: Giao phó toàn quyền cấu hình Relay trước khi cấp quyền cho bất kỳ Task nào.
  // Điều này đảm bảo khi vừa có điện, GPIO không bị trôi và Relay không bị nhảy bậy.
  controlLogic.begin();

  // 1. Task Watchdog (TWDT)
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true); // Khởi tạo TWDT, true = trigger panic nếu timeout

  // 2. Khởi tạo Dual-Core FreeRTOS
  xTaskCreatePinnedToCore(
    Task_Network,
    "TaskNetwork",
    8192,
    NULL,
    TASK_NETWORK_PRIORITY,
    &TaskNetworkHandle,
    TASK_NETWORK_CORE
  );

  xTaskCreatePinnedToCore(
    Task_Control,
    "TaskControl",
    4096,
    NULL,
    TASK_CONTROL_PRIORITY,
    &TaskControlHandle,
    TASK_CONTROL_CORE
  );
}

void loop() {
  vTaskDelete(NULL); // Xóa loop mặc định để giải phóng tài nguyên
}
