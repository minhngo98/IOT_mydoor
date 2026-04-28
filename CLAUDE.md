# Hướng dẫn dành cho Claude trong dự án MyDoor IoT

## 1. Lệnh thường dùng (Build & Flash)
- **Build Firmware Blynk**: `pio run -e blynk`
- **Build Firmware RainMaker**: `pio run -e rainmaker`
- **Upload Firmware Blynk**: `pio run -e blynk -t upload`
- **Upload Firmware RainMaker**: `pio run -e rainmaker -t upload`
- **Xóa sạch Flash (Erase NVS)**: `pio run -t erase` (hoặc cấu hình `extra_scripts = pre:scripts/erase_nvs.py` trong platformio.ini)

## 2. Kiến trúc Hệ thống (Architecture Guidelines)
- **Dual-Core FreeRTOS**: 
  - `Core 0`: Dành riêng cho xử lý Mạng, WebServer (ESPAsyncWebServer), Cloud (Blynk/RainMaker), OTA, NTP. Tất cả phải là **Non-blocking**.
  - `Core 1`: Dành riêng cho điều khiển Phần cứng (Relay, Nút bấm). Đảm bảo tính Real-time tuyệt đối, độ trễ 0ms.
- **An toàn Đa luồng (Thread-Safety)**: 
  - Mọi giao tiếp từ Core 0 sang Core 1 phải thông qua `QueueHandle_t` (ví dụ: `commandQueue`). Không được gọi trực tiếp hàm điều khiển phần cứng từ luồng của WebServer/Cloud.
  - Thao tác trên các biến dùng chung (ví dụ: chuỗi Log) phải được bảo vệ bằng `SemaphoreHandle_t` (Mutex).
- **Dual-Firmware (NVS Coexistence)**: 
  - Dự án dùng chung mã nguồn, phân tách qua Macro (`#ifdef USE_BLYNK`, `#ifdef USE_RAINMAKER`).
  - Dữ liệu mạng fallback và trạng thái Relay luôn lưu ở `Preferences` (namespace `mydoor`), phân vùng này dùng chung cho cả 2 Firmware để đảm bảo đổi Firmware OTA không bị mất dữ liệu cơ bản. RainMaker dùng vùng NVS mặc định (yêu cầu phân vùng 6000 bytes) cho chứng chỉ riêng.

## 3. Tiêu chuẩn Code (Coding Conventions)
- **Tuyệt đối Không dùng `delay()`**: Trong các API Callback của WebServer (`ESPAsyncWebServer`), không sử dụng hàm `delay()`. Mọi độ trễ bắt buộc phải dùng Cờ trạng thái (Flags) + `millis()` trong hàm `loop()` để không làm đóng băng luồng `async_tcp` gây WDT Panic.
- **Zero-Glitch Boot**: Các chân GPIO điều khiển Relay (`PIN_RELAY_UP`, `PIN_RELAY_DOWN`, `PIN_RELAY_STOP`, `PIN_RELAY_POWER`, `PIN_RELAY_LIGHT`) phải được khởi tạo và chốt cứng mức an toàn (OFF) ngay trong hàm `ControlLogic::initGPIO()` trước khi bất kỳ tiến trình FreeRTOS nào khởi chạy.
- **Phòng chống Brick khi OTA**: Khi đang có tiến trình nạp Firmware OTA (ElegantOTA), bắt buộc phải set cờ `isOtaRunning = true` để tạm ngưng hệ thống giám sát tràn RAM (`monitorHeap`), tránh việc thiết bị tự Reboot giữa lúc đang Flash làm hỏng bộ nhớ.
- **Log Event (Nhật ký)**: Mọi sự kiện của hệ thống phải được lưu qua hàm `logEvent()`. Hàm này đẩy log vào mảng RAM Ring-buffer (15 dòng) để hiển thị trên WebUI. Việc đồng bộ log lên Cloud phải được tách riêng (dùng `syncLogsToCloud()`) và chỉ gọi ở luồng Core 0 để tránh Crash thư viện Blynk.

## 4. Bảo mật & Fallback (Security & Resilience)
- **First Boot Setup**: Bắt buộc người dùng tạo tài khoản Admin ở lần khởi động đầu tiên (hoặc sau khi Factory Reset). Không hard-code tài khoản mặc định vào ROM.
- **Khôi phục Mạng Khẩn Cấp (Rescue AP)**: Khi mất kết nối WiFi quá 5 phút, hệ thống sẽ tự phát Rescue AP (với Blynk) hoặc bật lại BLE Provisioning (với RainMaker). Khi mạng có lại, phải tự động dập tắt ngay lập tức kết nối khẩn cấp này để bảo mật.
- **Rate Limiting**: Các endpoint cần xác thực (checkAuth) phải khóa 30 phút nếu đăng nhập sai 5 lần để chống Brute-force.
- **Tự Phục Hồi**: Hệ thống phải có Hardware Watchdog (WDT) và Daily Reboot vào 3 AM để làm sạch phân mảnh RAM.

## 5. Lịch sử & Quy trình Phát triển Hiện tại
- **Đã hoàn thành**: 
  - Thiết kế và hoàn thiện giao diện WebUI (Mock HTML) theo chuẩn UI/UX nhẹ, gọn, Responsive.
  - Đã tích hợp bản Web Preview (`web_preview_index.html`, `web_preview_setup.html`) vào mã nguồn C++ (`include/WebUI.h`).
  - Đã chuyển đổi toàn bộ Javascript mô phỏng sang sử dụng `fetch()` gọi các endpoint thật trên ESP32 (`/power`, `/light`, `/control`, `/save_wifi`, `/save_schedule`, `/save_rescue_ap`, `/save_admin`, `/get_config`, `/logs`).
- **Bước tiếp theo**: 
  - Nạp firmware vào ESP32 thật thông qua PlatformIO để kiểm thử end-to-end các API WebUI có giao tiếp chính xác với `NetworkManager.cpp` không.
  - Theo dõi việc lưu trữ Preferences khi submit Form (WiFi, Hẹn Giờ).
- **Quy trình Xử lý Lỗi**:
  - **Lỗi không nhận lệnh `pio`**: Do biến môi trường Windows thiếu đường dẫn PlatformIO. Cách khắc phục: Hãy chủ động mở Terminal tích hợp sẵn của PlatformIO trong VSCode và gõ lệnh build/upload ở đó.
  - **Lỗi giao diện WebUI không hoạt động**: Bật DevTools F12 của trình duyệt, kiểm tra tab Console và Network xem API `fetch()` có bị trả về mã lỗi 400/404/500 không. So sánh chính xác tên các tham số `name="xxx"` ở thẻ input HTML với `request->arg("xxx")` trong C++.
  - **Lỗi Crash / WDT Panic**: Kiểm tra lại nguyên tắc số 3 (Không dùng `delay()` trong callback). Rà soát xung đột mutex/queue giữa WebServer và Core điều khiển.