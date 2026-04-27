# MyDoor IoT - Cửa Cuốn Thông Minh (Dual-Core ESP32)

![Build Status](https://img.shields.io/badge/build-passing-brightgreen)
![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![Framework](https://img.shields.io/badge/framework-Arduino-orange)
![Architecture](https://img.shields.io/badge/architecture-FreeRTOS%20Dual--Core-red)
![License](https://img.shields.io/badge/license-MIT-green)

MyDoor IoT là Firmware cấp công nghiệp (Production-ready) điều khiển cửa cuốn và thiết bị phụ trợ (đèn chiếu sáng) sử dụng vi điều khiển ESP32.

---

## 📸 Giao diện trực quan (Gallery)

> **Lưu ý:** Phần này sẽ được cập nhật hình ảnh sau.
> 
> <!-- [Ảnh chụp WebUI Dashboard] -->
> <!-- [Ảnh chụp App Blynk] -->
> <!-- [Ảnh chụp Tính năng Hẹn giờ] -->

---

## 🌟 Điểm nổi bật về Kỹ thuật (Technical Highlights)

- **Kiến trúc FreeRTOS Dual-Core**: Phân luồng hoàn toàn. Core 0 xử lý WiFi/Web/Blynk (Non-blocking), Core 1 chuyên biệt điều khiển Relay (Real-time).
- **An toàn Đa luồng (Thread-safety)**: Giao tiếp liên lõi thông qua `QueueHandle_t` (Lệnh điều khiển) và `SemaphoreHandle_t` (Đồng bộ thời gian NTP).
- **Zero-Glitch Boot**: Chốt trạng thái GPIO Relay trong `setup()` trước khi khởi tạo hệ điều hành RTOS, triệt tiêu hoàn toàn rủi ro cửa tự mở khi có điện.
- **Tự phục hồi (Self-Healing)**: Tích hợp Hardware Watchdog (8s) và giám sát RAM (Reboot nếu Free Heap < 20KB).
- **Bảo vệ Flash (Wear Leveling)**: Sử dụng NVS kết hợp thuật toán "Dirty Flag" (chỉ ghi bộ nhớ khi trạng thái thực sự thay đổi).
- **Quản lý AP & Bảo mật**: 
  - Tự động phát Rescue AP (`HomeSmartbyMinh`) kèm chu kỳ 10 phút ON / 5 phút OFF khi mất Internet.
  - Bắt buộc thiết lập tài khoản Admin ở lần đầu (First Boot Setup).
  - Rate Limiting: Khóa cổng cấu hình 30 phút nếu nhập sai mật khẩu 5 lần (Chống Brute-force).
- **Terminal Logging**: Nhật ký hoạt động (Ring buffer) được lưu tại RAM và đồng bộ liên tục lên Dashboard WebUI và App Blynk (VPIN_TERMINAL).

---

## 📂 Cấu trúc Mã nguồn (Project Structure)

```text
MyDoor-IoT/
├── docs/                               # Tài liệu dự án
│   ├── all_diagram_electric.svg        # Sơ đồ nguyên lý (SVG)
│   └── software_architecture.md        # Luồng xử lý và phân bổ Core chi tiết
├── draw/
│   └── mainboard.qet                   # File gốc sơ đồ mạch (QElectroTech)
├── include/
│   ├── Config.h                        # Chân GPIO, Hằng số, Timeout, Watchdog
│   ├── ControlLogic.h                  # Logic Phần cứng (Core 1)
│   ├── NetworkManager.h                # Logic Mạng & WebServer (Core 0)
│   └── WebUI.h                         # Mã HTML/CSS/JS (Captive Portal)
├── src/
│   ├── ControlLogic.cpp                # Xử lý Relay, Queue, Interrupts, Memory WDT
│   ├── NetworkManager.cpp              # Async WebServer, Blynk, NTP, OTA, AP Cycle
│   └── main.cpp                        # Khởi tạo FreeRTOS Task và Zero-Glitch Boot
└── platformio.ini                      # Cấu hình biên dịch (min_spiffs.csv)
```

---

## 🚀 Hướng dẫn Cài đặt & Vận hành

### 1. Build & Nạp Code
1. Mở dự án bằng **VS Code** + **PlatformIO**.
2. Đảm bảo cấu hình phân vùng `min_spiffs.csv` trong `platformio.ini`. 
   *(Lưu ý: Bật `extra_scripts = pre:scripts/erase_nvs.py` nếu muốn xóa sạch NVS trước khi nạp).*
3. Chạy lệnh: `pio run -e blynk -t upload` để nạp vào ESP32.

### 2. Thiết lập Lần đầu (First Boot)
1. Cấp nguồn, hệ thống phát WiFi mặc định: **`HomeSmartbyMinh`** (Mật khẩu: `04011998`).
2. Truy cập `http://10.10.10.1`. Hệ thống bắt buộc tạo **Tài khoản Admin**.
3. Đăng nhập và cấu hình WiFi nhà bạn, Blynk Token, Lịch hẹn giờ (Cấp Nguồn Tổng / Đèn).

### 3. Nút nhấn Vật lý (GPIO 2 & GPIO 15)
- **Nút Đèn (GPIO 15):** Nhấn nhả (Toggle) để bật/tắt đèn tại chỗ. Trạng thái tự động đồng bộ lên App.
- **Nút Reset (GPIO 2):**
   - **Nhấn giữ 3s**: Reboot thiết bị.
   - **Nhấn giữ 10s**: Factory Reset (Xóa trắng NVS, đèn Vàng nháy 5 lần).

---

## 🛠 Thư viện nền tảng (Dependencies)
- [ESPAsyncWebServer](https://github.com/mathieucarbou/ESPAsyncWebServer) (Bất đồng bộ WebUI)
- [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA) (Cập nhật Firmware qua mạng)
- [Blynk](https://github.com/blynkkk/blynk-library) (Nền tảng Cloud IoT)

---

## 📝 Giấy phép
Dự án được phân phối dưới giấy phép [MIT](LICENSE).