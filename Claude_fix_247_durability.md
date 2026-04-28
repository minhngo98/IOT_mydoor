# Kế Hoạch Sửa Lỗi Tăng Cường Độ Bền 24/7 (10 Năm)

Dựa trên kết quả Review tổng thể dự án, dưới đây là kế hoạch chi tiết để khắc phục 4 nhóm lỗi nghiêm trọng ảnh hưởng đến độ ổn định của hệ thống:

## 1. Loại bỏ các hàm `delay()` blocking trong `ControlLogic.cpp`
**Vấn đề:** Các lệnh `delay()` (1000ms, 500ms, 50ms) làm đóng băng task của FreeRTOS, khiến CPU không thể xử lý các task khác và có nguy cơ trigger Watchdog Timer (WDT) reset.
**Giải pháp:**
- Thay thế `delay(1000)` và `delay(500)` trước khi reboot bằng `vTaskDelay(pdMS_TO_TICKS(1000))`. `vTaskDelay` báo cho FreeRTOS biết task này đang nghỉ để nhường CPU cho task khác chạy.
- Sửa lại logic chống dội phím (debounce) ở dòng 233 (`delay(50)`) thành non-blocking. Sử dụng `millis()` để kiểm tra thời gian trôi qua, không dùng `delay()`.

## 2. Xử lý Race Condition (Xung đột đa luồng)
**Vấn đề:** `NetworkManager` (Core 0) đọc trực tiếp các biến `currentPowerBoxState` và `currentLightState` thông qua các hàm `isPowerBoxOn()` và `isLightOn()` của `ControlLogic` (Core 1). Khi có nhiều tác vụ cùng đọc/ghi tại một thời điểm, dữ liệu có thể bị sai lệch.
**Giải pháp:**
- Thêm `SemaphoreHandle_t stateMutex` vào `ControlLogic.h`.
- Khởi tạo mutex này trong constructor `ControlLogic::ControlLogic()`.
- Trong các hàm `isPowerBoxOn()`, `isLightOn()`, `savePowerBoxState()`, `saveLightState()`, phải wrap logic bên trong khối lệnh `xSemaphoreTake(stateMutex, portMAX_DELAY)` và `xSemaphoreGive(stateMutex)`.

## 3. Khắc phục phân mảnh bộ nhớ RAM (Memory Fragmentation) do dùng `String`
**Vấn đề:** Hàm `NetworkManager::logEvent` hiện đang dùng biến cục bộ `String` cộng chuỗi và mảng `String eventLogs[15]` để lưu log. Sau thời gian dài, việc cấp phát/giải phóng bộ nhớ động liên tục (Heap fragmentation) sẽ gây Crash/WDT Panic.
**Giải pháp:**
- Trong `NetworkManager.h`: Đổi `String eventLogs[15];` thành mảng char 2 chiều `char eventLogs[15][80];` (giới hạn mỗi log 80 ký tự).
- Trong `NetworkManager.cpp`: Viết lại hàm `logEvent(const String& message)` hoặc `logEvent(const char* message)` sử dụng `snprintf` và `strncpy` để xử lý chuỗi trực tiếp trên bộ nhớ đã cấp phát tĩnh. Loại bỏ hoàn toàn dấu `+` cộng chuỗi trong hàm này.

## 4. Chống mòn bộ nhớ Flash (NVS Wear Leveling)
**Vấn đề:** Các hàm `savePowerBoxState` và `saveLightState` gọi lệnh `preferences.putBool()` lập tức mỗi khi state thay đổi. Nếu thiết bị được bật tắt liên tục (do người dùng hoặc do nhiễu), Flash memory có thể bị hỏng sector sau vài năm.
**Giải pháp:**
- Áp dụng kỹ thuật Lazy Write (Ghi trễ).
- Thêm các biến vào `ControlLogic.h`: `bool pendingNVSWrite = false;` và `uint32_t lastStateChangeTime = 0;`.
- Trong `savePowerBoxState`/`saveLightState`: Thay vì gọi `preferences.putBool()`, ta chỉ set `pendingNVSWrite = true` và `lastStateChangeTime = millis()`.
- Trong `ControlLogic::loop()`: Thêm logic kiểm tra: `if (pendingNVSWrite && (millis() - lastStateChangeTime > 60000))` (đợi 1 phút sau lần đổi trạng thái cuối cùng) -> Thực hiện `preferences.putBool()` và reset cờ `pendingNVSWrite = false`.

---
**Các file cần sửa:**
- `include/ControlLogic.h`
- `src/ControlLogic.cpp`
- `include/NetworkManager.h`
- `src/NetworkManager.cpp`

Sau khi hoàn thiện kế hoạch này, tiến hành gọi các lệnh `Edit` hoặc Python Regex để thay thế code an toàn. Cuối cùng chạy `pio run -e blynk` để xác nhận.