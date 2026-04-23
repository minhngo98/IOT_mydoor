# MyDoor IoT - Industrial Smart Roller Door Controller

![Build Status](https://img.shields.io/badge/build-passing-brightgreen)
![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![Framework](https://img.shields.io/badge/framework-Arduino-orange)
![Architecture](https://img.shields.io/badge/architecture-FreeRTOS%20Dual--Core-red)
![License](https://img.shields.io/badge/license-MIT-green)

Dự án điều khiển cửa cuốn IoT mã nguồn mở cấp độ công nghiệp (Industrial-grade) dựa trên vi điều khiển ESP32. Được thiết kế với kiến trúc **FreeRTOS Dual-Core**, dự án hướng tới sự **ổn định tuyệt đối 24/7 trong suốt 10 năm**. Tích hợp cách ly quang điện thực thụ, Web Config Captive Portal, OTA Updates, và cơ chế tự phục hồi lỗi (Self-Healing Watchdog).

---

## 🌟 Tính năng nổi bật & Kiến trúc Kỹ thuật

Dự án này giải quyết triệt để các bài toán hóc búa nhất của IoT thực tế: Chống treo máy, chống rò rỉ RAM, mòn Flash và mất mạng.

*   **Kiến trúc FreeRTOS Dual-Core**: 
    *   **Core 0 (PRO_CPU):** Xử lý WiFi, Async WebServer, NTP, Blynk (Ưu tiên thấp).
    *   **Core 1 (APP_CPU):** Xử lý ngắt nút bấm (Interrupts), điều khiển Relay, Watchdog (Ưu tiên cao).
    *   *Kết quả:* Nút bấm vật lý luôn phản hồi tức thì (<10ms) kể cả khi rớt mạng hay Web Server bị tấn công DDoS.
*   **Cơ chế Tự phục hồi (Self-Healing)**:
    *   **Hardware WDT & Task WDT (8s):** Cả 2 Core đều bị giám sát chặt chẽ. Nếu 1 Core treo, toàn bộ hệ thống tự động Reboot.
    *   **Heap Monitoring:** Liên tục giám sát RAM, tự động giải phóng hoặc Reboot nếu `FreeHeap` < 20KB để chống tràn bộ nhớ (Memory Leak).
    *   **Daily Maintenance:** Tự động Reboot làm mới WiFi Stack vào 03:00 AM mỗi ngày (chỉ khi hệ thống Idle và đã đồng bộ NTP).
*   **Chiến lược Rescue AP (Chống khóa cửa ngoài)**:
    *   Tự động phát Access Point (AP) ẩn (Hidden SSID) khi mất kết nối Internet > 5 phút.
    *   **Chu kỳ AP thông minh:** 10 phút ON / 5 phút OFF để giảm nhiệt chip và nhiễu RF.
    *   **Bảo mật:** Web cứu hộ yêu cầu Basic Auth. Tích hợp Rate Limiting: Sai mật khẩu 5 lần sẽ khóa AP 30 phút.
    *   **Interrupt Wake-up:** Nhấn giữ nút BOOT 5s để ép đánh thức AP ngay lập tức dù đang trong chu kỳ OFF.
*   **Bảo vệ Flash 10 Năm (Wear Leveling)**:
    *   Sử dụng thư viện `Preferences` (NVS) thay vì EEPROM.
    *   Thuật toán **Dirty Flag**: Chỉ ghi dữ liệu vào Flash khi trạng thái Relay thực sự thay đổi, giảm thiểu tối đa số chu kỳ Ghi/Xóa.
*   **Zero-Glitch Boot**:
    *   Thuật toán khởi động ghim điện áp: `Read NVS -> digitalWrite() -> pinMode()`. Đảm bảo Relay không bao giờ bị chớp giật hay tự mở cửa khi nhà vừa có điện lại.

---

## 🔌 Sơ đồ Mạch điện (Circuit Diagram)

Bản vẽ hệ thống điện công nghiệp (Đã tích hợp RC Snubber, Varistor, Opto-Isolator):

![Sơ đồ nguyên lý mạch ESP32 MyDoor](docs/diagram_electric.svg)

*(Nếu hình ảnh không hiển thị trên Github, vui lòng xem trực tiếp file `docs/diagram_electric.svg`)*

---

## 📂 Cấu trúc mã nguồn

```text
IOT_Mydoor/
 ├── include/
 │   ├── Config.h           # Định nghĩa chân GPIO, WDT, Priority, Timer, VPIN
 │   ├── NetworkManager.h   # Khai báo lớp Quản lý Mạng, AP Cycle, Rate Limit
 │   └── WebUI.h            # HTML/CSS/JS giao diện Web nội bộ
 ├── src/
 │   ├── main.cpp           # Khởi tạo Dual-Core FreeRTOS, Zero-Glitch Boot, Control Logic
 │   └── NetworkManager.cpp # Code triển khai Web Server Async, Captive Portal, OTA
 ├── docs/
 │   ├── hardware_wiring.md # Hướng dẫn đấu nối điện
 │   └── diagram_electric.svg # Bản vẽ mạch điện SVG
 ├── platformio.ini         # Cấu hình môi trường biên dịch (Blynk/RainMaker)
 └── README.md
```

---

## 🚀 Hướng dẫn Cài đặt & Sử dụng

### 1. Biên dịch Mã nguồn (PlatformIO)
Dự án được tối ưu hóa cho [PlatformIO](https://platformio.org/) trên VS Code.
1. Mở thư mục dự án trong VS Code có cài sẵn PlatformIO.
2. File `platformio.ini` đã cấu hình sẵn phân vùng lớn (`min_spiffs.csv`), cờ `-fpermissive` và giải quyết xung đột thư viện AsyncWebServer/ElegantOTA.
3. Bấm **Build (✓)** chọn môi trường `env:blynk`.
4. Cắm cáp ESP32 và bấm **Upload (→)**.

### 2. Cấu hình Lần Đầu (Captive Portal)
1. Khi khởi động lần đầu, hệ thống phát WiFi ẩn tên **`MyDoor_Setup`** (Mật khẩu: `12345678`).
2. Nếu không thấy WiFi, nhấn giữ nút BOOT trong 5 giây.
3. Kết nối vào mạng trên và truy cập `http://10.10.10.1`.
4. Đăng nhập: `admin` / `admin` (Nên đổi ngay trong lần đầu).
5. Quét và nhập WiFi Chính (Bắt buộc), WiFi Phụ (Dự phòng).
6. Nhập `Blynk Template ID`, `Device Name`, và `Auth Token`.
7. Thiết lập Múi giờ, Giờ bật/tắt an toàn cho Nguồn Box cửa. Bấm Lưu & Khởi động lại.

---

## 🛠 Thư viện C/C++ cốt lõi
*   **FreeRTOS** (Tích hợp sẵn trong ESP-IDF) - Quản lý đa tiến trình.
*   [ESPAsyncWebServer](https://github.com/mathieucarbou/ESPAsyncWebServer) - Phục vụ hàng chục kết nối Web cùng lúc không nghẽn.
*   [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA) - Giao diện nạp OTA `.bin` không cần cáp.
*   [Blynk (1.3.2)](https://github.com/blynkkk/blynk-library) - Giao thức kết nối App điện thoại thời gian thực.

---

## 📝 Giấy phép (License)
Dự án được phân phối dưới giấy phép MIT - Tự do sử dụng, chỉnh sửa và thương mại hóa. Vui lòng giữ lại ghi nguồn tác giả.