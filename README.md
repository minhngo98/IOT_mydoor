# MyDoor IoT - Smart Roller Door Controller

![Build Status](https://img.shields.io/badge/build-passing-brightgreen)
![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![Framework](https://img.shields.io/badge/framework-Arduino-orange)
![Architecture](https://img.shields.io/badge/architecture-FreeRTOS%20Dual--Core-red)
![License](https://img.shields.io/badge/license-MIT-green)

Firmware điều khiển cửa cuốn dựa trên vi điều khiển ESP32, sử dụng kiến trúc **FreeRTOS Dual-Core** giúp hệ thống hoạt động ổn định, phản hồi thời gian thực và an toàn.

---

## 🌟 Tính năng chính

- **Kiến trúc FreeRTOS Dual-Core**: Phân tách tác vụ (Network ở Core 0, Điều khiển phần cứng ở Core 1) đảm bảo nút bấm luôn phản hồi ngay lập tức (<10ms).
- **Tự động phục hồi (Self-Healing)**: Tích hợp Hardware/Task Watchdog Timer (8s) và giám sát RAM để tự động reboot khi hệ thống treo hoặc cạn bộ nhớ.
- **Bảo vệ Flash (Wear Leveling)**: Sử dụng NVS với thuật toán "Dirty Flag" (chỉ ghi khi có thay đổi) để tối đa hóa tuổi thọ bộ nhớ Flash của ESP32.
- **Zero-Glitch Boot**: Khởi tạo GPIO an toàn, chốt trạng thái Relay trước khi cấp nguồn, chống hiện tượng chớp giật hoặc tự mở cửa khi mất điện có lại.
- **Quản lý AP Thông minh (Rescue AP)**: Tự động phát WiFi ẩn kèm chu kỳ bật/tắt (10 phút ON / 5 phút OFF) khi mất Internet để cho phép cấu hình lại mà không gây nhiễu RF.
- **Bảo mật cao**:
  - Hỗ trợ Rate Limiting (khóa AP nếu nhập sai mật khẩu nhiều lần).
  - Giới hạn kích thước dữ liệu đầu vào chống lỗi tràn bộ nhớ (Buffer Overflow).
  - Giao diện WebUI (Captive Portal) được lưu trong PROGMEM để tiết kiệm RAM.

---

## 🔌 Sơ đồ Mạch điện & Cấu trúc mã nguồn

- **Sơ đồ nguyên lý:** `docs/diagram_electric.svg` (Bạn có thể xem trực tiếp ảnh SVG trên GitHub).
- **Tài liệu vẽ mạch gốc:** `draw/mainboard.qet` (Mở bằng QElectroTech).

---

## 📂 Cấu trúc mã nguồn

- `include/Config.h`: Khai báo chân GPIO, Watchdog, hằng số cấu hình.
- `include/NetworkManager.h` & `src/NetworkManager.cpp`: Xử lý WiFi, Async WebServer, Captive Portal, Blynk, OTA và NTP trên Core 0.
- `include/ControlLogic.h` & `src/ControlLogic.cpp`: Xử lý logic Relay, Queue lệnh, bảo vệ Flash NVS trên Core 1.
- `src/main.cpp`: Khởi tạo FreeRTOS, phân bổ Task và cấu hình Watchdog.

---

## 🚀 Hướng dẫn Cài đặt & Sử dụng

### 1. Biên dịch (PlatformIO)
1. Mở dự án bằng VS Code có cài đặt [PlatformIO](https://platformio.org/).
2. Đảm bảo cấu hình phân vùng `min_spiffs.csv` trong `platformio.ini`.
3. Bấm **Build** cho môi trường `env:blynk`.
4. Kết nối ESP32 và bấm **Upload**.

### 2. Cấu hình Lần Đầu
1. Cấp nguồn, hệ thống sẽ phát WiFi: **`Security_MyMinh`** (Mật khẩu: `Aurora@04011998!`).
2. Nếu không thấy WiFi, nhấn giữ nút BOOT trong 3 giây.
3. Kết nối và truy cập `http://10.10.10.1` (Captive Portal).
4. Thiết lập WiFi Chính, Múi giờ, thông tin Blynk (Template ID, Device Name, Auth Token) và lưu lại.

---

## 🛠 Thư viện sử dụng
- [ESPAsyncWebServer](https://github.com/mathieucarbou/ESPAsyncWebServer)
- [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA)
- [Blynk](https://github.com/blynkkk/blynk-library)

---

## 📝 Giấy phép
Dự án được phân phối dưới giấy phép [MIT](LICENSE).