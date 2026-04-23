#include <Arduino.h>
#include <HardwareSerial.h> // Ensure Serial is defined for RainMaker core
#include <Preferences.h>
#include <time.h>
#include <esp_task_wdt.h> // Task Watchdog
#include <soc/rtc_wdt.h>  // Hardware WDT

// Khai báo Blynk template trước khi include các file khác
#define BLYNK_TEMPLATE_ID "TMPLxxxxxx"
#define BLYNK_TEMPLATE_NAME "MyDoor"

#define BLYNK_PRINT Serial

#ifdef USE_BLYNK
  #include <BlynkSimpleEsp32.h>
#endif

// Bao gồm file cấu hình và thư viện
#include "Config.h"
#include "NetworkManager.h"
// #include "ControlLogic.h" // Sẽ được tách ra file riêng ở bước sau

// ==========================================
// BIẾN TRẠNG THÁI (Lưu trữ và phục hồi)
// ==========================================
Preferences preferences;
bool isBoxPowered = true;     // Trạng thái của Relay 4 (Nguồn tổng)
bool savedBoxPowered = true;  // Trạng thái đã lưu trong NVS (Dùng cho Dirty Flag)
bool wasWifiConnected = false;

// Handle cho các Task FreeRTOS
TaskHandle_t TaskNetworkHandle = NULL;
TaskHandle_t TaskControlHandle = NULL;

// Khai báo hàm cục bộ (Tạm thời để trong main trước khi tách file)
void setupPinsZeroGlitch();
void Task_Network(void *pvParameters);
void Task_Control(void *pvParameters);
void checkMemoryAndReboot();
void checkDailyReboot();
void logEvent(String eventName);

// Các hàm điều khiển cơ bản
void pulseRelay(int pin);
void openDoor();
void closeDoor();
void stopDoor();
void setBoxPower(bool state);

// ==========================================
// SETUP & KHỞI ĐỘNG (BOOT STRATEGY)
// ==========================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== MYDOOR ESP32 (DUAL-CORE & 10Y STABILITY) ===");

  // 1. READ NVS: Khởi tạo Preferences ngay đầu tiên để đọc trạng thái cũ
  preferences.begin("mydoor", false);
  savedBoxPowered = preferences.getBool("box_power", true); // Đọc trạng thái cuối (mặc định Bật)
  isBoxPowered = savedBoxPowered;
  Serial.printf("[NVS] Khoi phuc trang thai Nguon Tong: %s\n", isBoxPowered ? "BAT" : "TAT");
  preferences.end(); // Đóng ngay lập tức để bảo vệ NVS

  // 2 & 3. ZERO-GLITCH GPIO: Đảm bảo GPIO được cấu hình đúng trạng thái trước khi pinMode
  setupPinsZeroGlitch();

  // 4. TASK WATCHDOG (TWDT): Khởi tạo Watchdog cho hệ thống FreeRTOS (Thời gian chờ 8 giây)
  esp_task_wdt_init(WDT_TIMEOUT_SEC, true); // true = Panic/Reboot khi TWDT bị trigger

  // 5. INIT TASKS (DUAL-CORE ARCHITECTURE)
  // Tạo Task Network (Core 0, Priority thấp)
  xTaskCreatePinnedToCore(
    Task_Network,            /* Hàm thực thi của Task */
    "TaskNetwork",           /* Tên Task */
    8192,                    /* Kích thước Stack (8KB cho WiFi/Web/SSL) */
    NULL,                    /* Tham số truyền vào */
    TASK_NETWORK_PRIORITY,   /* Mức độ ưu tiên (Priority: 1) */
    &TaskNetworkHandle,      /* Trỏ tới Task Handle */
    TASK_NETWORK_CORE        /* Core chạy (Core 0: PRO_CPU) */
  );

  // Tạo Task Control (Core 1, Priority cao)
  xTaskCreatePinnedToCore(
    Task_Control,            /* Hàm thực thi của Task */
    "TaskControl",           /* Tên Task */
    4096,                    /* Kích thước Stack (4KB cho Logic điều khiển) */
    NULL,                    /* Tham số truyền vào */
    TASK_CONTROL_PRIORITY,   /* Mức độ ưu tiên (Priority: 2) */
    &TaskControlHandle,      /* Trỏ tới Task Handle */
    TASK_CONTROL_CORE        /* Core chạy (Core 1: APP_CPU) */
  );

  // Note: Việc khởi tạo Network và Logic sẽ được thực thi bên trong 2 Task riêng biệt.
  // Hàm setup() của Arduino (chạy trên Core 1) sẽ nhường quyền ngay lập tức.
}

// ==========================================
// LOOP (KHÔNG SỬ DỤNG - DÙNG FREERTOS TASKS)
// ==========================================
void loop() {
  // Hàm loop() rỗng. Mọi logic đã chuyển sang Task_Network và Task_Control
  vTaskDelete(NULL); // Tự xóa loop() để nhường toàn bộ Core 1 cho Task_Control
}

// ==========================================
// TASK 0: NETWORK (CORE 0 - PRO_CPU)
// WiFi, AsyncWebServer, Blynk, NTP, OTA
// ==========================================
void Task_Network(void *pvParameters) {
  Serial.println("[TASK] Network Task khoi chay tren Core 0.");
  esp_task_wdt_add(NULL); // Đăng ký Task Network vào Watchdog

  // Khởi tạo WiFi, WebServer, OTA (Từ NetworkManager)
  netManager.begin();

#ifdef USE_BLYNK
  // Cấu hình Blynk (Blynk.connect() không chạy chặn)
  if (netManager.blynk_auth.length() == 32) {
    Blynk.config(netManager.blynk_auth.c_str());
  } else {
    Serial.println("[BLYNK] Chua co Token. Vao 10.10.10.1 de cai dat!");
  }
#endif

  logEvent("He thong vua khoi dong (Power ON)");

  // Vòng lặp chính của Core 0
  for (;;) {
    esp_task_wdt_reset(); // Nuôi chó (Feed WDT) ở Core 0

    // Xử lý chu kỳ AP, Kết nối mạng WiFi, Mất kết nối
    netManager.loop();

    // Xử lý cập nhật giao diện mạng (Đèn WiFi) và đồng bộ NTP
    if (netManager.isConnected) {
      if (!wasWifiConnected) {
        wasWifiConnected = true;
        digitalWrite(PIN_LED_WIFI, LED_ON); // Sáng xanh dương
        digitalWrite(PIN_LED_WARN, LED_OFF); // Tắt vàng
        configTime(netManager.timezone * 3600, 0, "pool.ntp.org", "time.nist.gov"); // Đồng bộ giờ
        logEvent("WiFi da ket noi thanh cong. Da dong bo NTP.");
      }

      // Xử lý giao tiếp Blynk (Non-blocking)
#ifdef USE_BLYNK
      if (!Blynk.connected()) {
        Blynk.connect(50); // Thử kết nối 50ms rồi nhả Core 0 cho việc khác
      } else {
        Blynk.run();
      }
#endif

    } else {
      if (wasWifiConnected) {
        wasWifiConnected = false;
        digitalWrite(PIN_LED_WARN, LED_ON); // Sáng vàng cảnh báo
        logEvent("MAT KET NOI WIFI! Chuyen sang co che Auto-AP/Retry.");
      }
      // Đèn WiFi nháy báo mất mạng / Đang phát AP
      digitalWrite(PIN_LED_WIFI, (millis() % 1000 < 500) ? LED_ON : LED_OFF);
    }

    // Tạm nghỉ 20ms để WiFi Stack của ESP32 xử lý (Rất quan trọng cho Core 0)
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// TASK 1: CONTROL (CORE 1 - APP_CPU)
// Relay Logic, Nút bấm (Interrupt/Polling), NVS, Health Check
// ==========================================
void Task_Control(void *pvParameters) {
  Serial.println("[TASK] Control Task khoi chay tren Core 1 (Uu tien cao).");
  esp_task_wdt_add(NULL); // Đăng ký Task Control vào Watchdog

  // Vòng lặp chính của Core 1
  for (;;) {
    esp_task_wdt_reset(); // Nuôi chó (Feed WDT) ở Core 1

    // 1. Xử lý lưu NVS bằng Dirty Flag (Bảo vệ mòn Flash)
    if (isBoxPowered != savedBoxPowered) {
      preferences.begin("mydoor", false);
      preferences.putBool("box_power", isBoxPowered);
      preferences.end();
      savedBoxPowered = isBoxPowered;
      Serial.printf("[NVS-DIRTY] Da ghi trang thai Nguon Tong moi: %s\n", isBoxPowered ? "BAT" : "TAT");
    }

    // 2. Health Check (Chống treo do rò rỉ RAM)
    checkMemoryAndReboot();

    // 3. Tự động Reboot làm mới (Chỉ chạy lúc 3h sáng)
    checkDailyReboot();

    // TODO: Sẽ tích hợp thêm logic quét nút âm tường cứng (Interrupt/Polling)
    // TODO: Kiểm tra lịch trình (Schedule) bật tắt Nguồn Box

    // Chạy mượt, giải phóng Core 1, đảm bảo Priority cao hơn Network
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ==========================================
// HÀM CHỨC NĂNG CORE (ZERO-GLITCH & ĐIỀU KHIỂN)
// ==========================================

// ZERO-GLITCH BOOT STRATEGY: Ghi Mức logic trước, PinMode sau
void setupPinsZeroGlitch() {
  // 1. DigitalWrite ngay lập tức để ghim điện áp, tránh Relay nhảy loạn
  digitalWrite(PIN_RELAY_UP, RELAY_OFF);
  digitalWrite(PIN_RELAY_DOWN, RELAY_OFF);
  digitalWrite(PIN_RELAY_STOP, RELAY_OFF);

  // Nguồn Box cửa: Phục hồi trạng thái đọc được từ NVS (isBoxPowered)
  digitalWrite(PIN_RELAY_POWER, isBoxPowered ? POWER_ON : POWER_OFF);

  // LED mặc định tắt, trừ LED báo Sẵn sàng
  digitalWrite(PIN_LED_WIFI, LED_OFF);
  digitalWrite(PIN_LED_WARN, LED_OFF);
  digitalWrite(PIN_LED_FAULT, LED_OFF);
  digitalWrite(PIN_LED_READY, LED_ON); // Báo hiệu đã qua Zero-Glitch Boot

  // 2. Mới bắt đầu Set PinMode. Vì đã có digitalWrite ở trên, chân sẽ có ngay điện áp mục tiêu
  pinMode(PIN_RELAY_UP, OUTPUT);
  pinMode(PIN_RELAY_DOWN, OUTPUT);
  pinMode(PIN_RELAY_STOP, OUTPUT);
  pinMode(PIN_RELAY_POWER, OUTPUT);

  pinMode(PIN_LED_WIFI, OUTPUT);
  pinMode(PIN_LED_READY, OUTPUT);
  pinMode(PIN_LED_WARN, OUTPUT);
  pinMode(PIN_LED_FAULT, OUTPUT);

  Serial.println("[BOOT] Zero-Glitch GPIO thanh cong.");
}

void pulseRelay(int pin) {
  if (!isBoxPowered) {
    Serial.println("[LOI] Box Nguon dang TAT. Khong the dieu khien cua!");
    logEvent("LOI DIEU KHIEN: Box Nguon dang tat.");
    return;
  }
  digitalWrite(pin, RELAY_ON);
  vTaskDelay(DOOR_PULSE_MS / portTICK_PERIOD_MS); // Thay delay() bằng vTaskDelay cho FreeRTOS
  digitalWrite(pin, RELAY_OFF);
}

void stopDoor()  { pulseRelay(PIN_RELAY_STOP); }
void openDoor()  { pulseRelay(PIN_RELAY_UP); }
void closeDoor() { pulseRelay(PIN_RELAY_DOWN); }

void setBoxPower(bool state) {
  isBoxPowered = state; // Gán trạng thái mới (Dirty Flag ở Core 1 sẽ phát hiện và tự ghi NVS)
  digitalWrite(PIN_RELAY_POWER, state ? POWER_ON : POWER_OFF);
}

// Hàm Self-Healing: Kiểm tra RAM trống, tự Reboot nếu < 20KB
void checkMemoryAndReboot() {
  static unsigned long lastMemCheck = 0;
  if (millis() - lastMemCheck > 60000) { // Kiểm tra mỗi 1 phút
    lastMemCheck = millis();
    uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < MIN_FREE_HEAP) {
      Serial.printf("[FATAL] RAM CAN KIET! Con %d bytes. Kich hoat SELF-HEALING REBOOT...\n", freeHeap);
      logEvent("RAM nguy hiem (" + String(freeHeap) + "). Tu dong Reboot!");
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      ESP.restart();
    }
  }
}

// Hàm Daily Reboot 03:00 AM (Chống treo, phân mảnh mạng sau 1 năm chạy)
void checkDailyReboot() {
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 60000) { // 1 phút check 1 lần
    lastCheck = millis();
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) { // Có giờ NTP
      if (timeinfo.tm_hour == DAILY_REBOOT_HOUR && timeinfo.tm_min == 0) {
        logEvent("==== DAILY REBOOT 03:00 AM ====");
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        ESP.restart();
      }
    }
  }
}

void logEvent(String eventName) {
  struct tm timeinfo;
  String timeStr = "[Chua NTP]";
  if (getLocalTime(&timeinfo, 0)) {
    char tBuff[50];
    strftime(tBuff, sizeof(tBuff), "[%d/%m/%Y %H:%M:%S]", &timeinfo);
    timeStr = String(tBuff);
  }
  String finalLog = timeStr + " " + eventName;
  Serial.println(finalLog);

#ifdef USE_BLYNK
  if (Blynk.connected()) Blynk.virtualWrite(VPIN_TERMINAL, finalLog + "\n");
#endif
}

#ifdef USE_BLYNK
// ==========================================
// BLYNK APP - ĐIỀU KHIỂN (CORE 0 nhận lệnh)
// ==========================================
BLYNK_WRITE(VPIN_DOOR_UP)   { if(param.asInt()) { logEvent("Blynk App: MỞ"); openDoor(); } }
BLYNK_WRITE(VPIN_DOOR_DOWN) { if(param.asInt()) { logEvent("Blynk App: ĐÓNG"); closeDoor(); } }
BLYNK_WRITE(VPIN_DOOR_STOP) { if(param.asInt()) { logEvent("Blynk App: DỪNG"); stopDoor(); } }
BLYNK_WRITE(VPIN_POWER_BOX) {
  bool state = param.asInt();

  // 3. CLOUD SYNC PRIORITY: Chỉ cho phép ghi NVS qua lệnh Blynk nếu có tín hiệu thực
  logEvent(state ? "Blynk App: Bật Nguồn" : "Blynk App: Ngắt Nguồn");
  setBoxPower(state);
}

BLYNK_CONNECTED() {
  // Khi App Blynk vừa kết nối, đồng bộ ngược Trạng Thái Nguồn (isBoxPowered) lên App.
  // Tránh việc Blynk Server đè mất trạng thái đã khôi phục từ NVS cục bộ lúc boot.
  Blynk.virtualWrite(VPIN_POWER_BOX, isBoxPowered ? 1 : 0);
}
#endif
