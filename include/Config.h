#ifndef CONFIG_H
#define CONFIG_H

// BLYNK TEMPLATE SETTINGS MUST BE BEFORE BLYNK INCLUDE
#define BLYNK_TEMPLATE_ID "TMPLxxxxxx"
#define BLYNK_TEMPLATE_NAME "MyDoor"

// ==========================================
// 1. BLYNK IOT CONFIGURATION (LẤY TỪ WEB BLYNK CONSOLE)
// ==========================================
#ifdef USE_BLYNK
  #define VPIN_DOOR_UP    V0  // Nút MỞ (Button)
  #define VPIN_DOOR_DOWN  V1  // Nút ĐÓNG (Button)
  #define VPIN_DOOR_STOP  V2  // Nút DỪNG (Button)
  #define VPIN_POWER_BOX  V3  // Công tắc Nguồn Tổng (Switch)
  #define VPIN_TERMINAL   V4  // Terminal hiển thị Log sự kiện
  #define VPIN_LIGHT      V5  // Nút Bật/Tắt Đèn (Switch)
#endif

// ==========================================
// 2. PIN DEFINITIONS (DỰA THEO SƠ ĐỒ ĐÃ CHỐT)
// ==========================================
// Relay Điều khiển Cửa (Opto-isolated, Active LOW)
#define PIN_RELAY_UP    25  // IN1: Kéo LÊN
#define PIN_RELAY_DOWN  26  // IN2: Kéo XUỐNG
#define PIN_RELAY_STOP  27  // IN3: DỪNG

// Relay Cấp/Ngắt Nguồn Tổng Box Cửa Cuốn
#define PIN_RELAY_POWER 32  // IN4: Điều khiển nguồn box điều khiển (Bật ban ngày, Tắt ban đêm)

// Relay Điều khiển Đèn Chiếu Sáng
#define PIN_RELAY_LIGHT 33  // IN5: Điều khiển Đèn (Lighting)

// Đèn LED Trạng thái (Active LOW: Sáng khi kéo xuống GND)
#define PIN_LED_WIFI    13  // BLUE: Trạng thái WiFi/Cloud
#define PIN_LED_READY   14  // GREEN: Hệ thống sẵn sàng
#define PIN_LED_FAULT   4   // RED: Lỗi hệ thống
#define PIN_LED_WARN    16  // YELLOW: Cảnh báo

// Nút nhấn vật lý
#define PIN_BTN_CONFIG  0   // Nút BOOT: Nhấn giữ 3s để Wake-up AP
#define PIN_BTN_RESET   2   // Nút cứng Reset (Chung): Nhấn giữ 3s -> Reboot, Nhấn giữ 10s -> Factory Reset
#define PIN_BTN_LIGHT   15  // Nút cứng Điều khiển Đèn tại chỗ

// ==========================================
// 3. LOGIC STATES (CỰC KỲ QUAN TRỌNG ĐỂ KHÔNG CHẠP CHÁY)
// ==========================================
// Mạch Relay Opto kích mức thấp
#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// Mạch Relay 4 cắt nguồn (kích mức thấp)
#define POWER_ON  LOW
#define POWER_OFF HIGH

// LED Active LOW
#define LED_ON    LOW
#define LED_OFF   HIGH

// ==========================================
// 4. THÔNG SỐ VẬN HÀNH & KIẾN TRÚC DUAL-CORE
// ==========================================
#define DOOR_PULSE_MS      500    // Thời gian "nhấn giữ" nút âm tường ảo (ms)
#define WIFI_TIMEOUT_MS    10000  // 10s: Thời gian chờ kết nối WiFi

// Watchdog Timer (WDT)
#define WDT_TIMEOUT_SEC    8      // Hardware WDT Timeout (8 giây)
#define BLYNK_CONNECT_TIMEOUT_MS         1500
#define BLYNK_RECONNECT_BASE_MS          5000
#define BLYNK_RECONNECT_MAX_MS           60000
#define BLYNK_POST_CONNECT_GUARD_MS      2500
#define BLYNK_SSL_HANDSHAKE_TIMEOUT_SEC  3

// FreeRTOS Yield Constants
#define YIELD_WIFI_MS      20     // Thời gian nhường CPU cho stack Wi-Fi
#define YIELD_CONTROL_MS   10     // Nhịp điều khiển cho Core 1
#define YIELD_BUTTON_MS    100    // Thời gian chờ poll nút nhấn
#define CONFIG_HOLD_MS     5000   // Nhấn giữ nút CONFIG để bật Rescue AP
#define DEBOUNCE_MS        200    // Chống dội nút cứng
#define RESET_REBOOT_MS    3000   // Nhấn giữ nút RESET để reboot
#define RESET_FACTORY_MS   10000  // Nhấn giữ nút RESET để factory reset
#define RESTART_GUARD_MS   30000  // Chặn reboot lặp liên tục trong thời gian ngắn

// Cấu hình Task FreeRTOS (Priority: Control > Network)
#define TASK_NETWORK_PRIORITY 1   // Ưu tiên thấp hơn cho Core 0 (Network)
#define TASK_CONTROL_PRIORITY 2   // Ưu tiên cao hơn cho Core 1 (Control)
#define TASK_NETWORK_CORE     0   // PRO_CPU
#define TASK_CONTROL_CORE     1   // APP_CPU

// Chế độ Rescue AP (Hidden SSID)
#define AP_CYCLE_ON_MS     600000 // 10 phút Bật AP (ms)
#define AP_CYCLE_OFF_MS    300000 // 5 phút Tắt AP (ms)
#define AP_LOCKOUT_MS      1800000// 30 phút khóa AP nếu nhập sai 5 lần (ms)
#define RAINMAKER_REPROVISION_MS 300000 // 5 phút mất mạng thì bật lại provisioning

// Mật khẩu mặc định xuất xưởng (First Boot)
#define DEFAULT_RESCUE_AP_SSID "SmartHomebyMinh"
#define DEFAULT_RESCUE_AP_PASS "04011998"

// Ngưỡng thời gian Reset cứng (ms)
#define RESET_REBOOT_HOLD_MS    3000  // 3s: Reboot
#define RESET_FACTORY_HOLD_MS   10000 // 10s: Factory Reset

// Cảnh báo & Tự phục hồi
#define MIN_FREE_HEAP      20000  // Ngưỡng RAM nguy hiểm (20KB), dưới mức này tự Reboot
#define DAILY_REBOOT_HOUR  3      // Tự động Reboot lúc 03:00 AM (nếu Idle)

#endif // CONFIG_H
