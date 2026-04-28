# MyDoor IoT - Cửa Cuốn Thông Minh (Blynk & ESP RainMaker)

![Build Status](https://img.shields.io/badge/build-passing-brightgreen)
![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![Framework](https://img.shields.io/badge/framework-Arduino-orange)
![Architecture](https://img.shields.io/badge/architecture-FreeRTOS%20Dual--Core-red)
![License](https://img.shields.io/badge/license-MIT-green)

MyDoor IoT là Firmware cấp công nghiệp (Professional Grade) điều khiển cửa cuốn và thiết bị phụ trợ (đèn chiếu sáng) sử dụng vi điều khiển ESP32. Đặc biệt, hệ thống hỗ trợ song song 2 nền tảng Cloud độc lập: **Blynk** và **ESP RainMaker**, cho phép chuyển đổi Firmware (OTA) linh hoạt mà không làm mất trạng thái hay kết nối mạng cơ bản.

---

## 📸 Giao diện trực quan (Gallery)

> **Lưu ý:** Phần này sẽ được cập nhật hình ảnh thực tế sau.
> 
> <!-- [Ảnh chụp WebUI Dashboard] -->
> <!-- [Ảnh chụp App Blynk] -->
> <!-- [Ảnh chụp App ESP RainMaker] -->
> <!-- [Ảnh chụp Tính năng Hẹn giờ] -->

---

## 🌟 Tính năng nổi bật (Key Features)

- **Dual-Firmware (Blynk / RainMaker)**: Xây dựng chung một mã nguồn `NetworkManager`, dùng `#ifdef` để compile riêng bản Blynk hoặc bản RainMaker. Hai bản có thể cập nhật đè lên nhau qua OTA.
- **Kiến trúc FreeRTOS Dual-Core**: Phân luồng hoàn toàn. Core 0 xử lý WiFi/Web/Blynk/RainMaker (Non-blocking), Core 1 chuyên biệt điều khiển Relay (Real-time).
- **An toàn Đa luồng (Thread-safety)**: 
  - Giao tiếp liên lõi thông qua `QueueHandle_t` cho việc gửi nhận lệnh điều khiển.
  - Sử dụng `SemaphoreHandle_t` (Mutex) và Ring Buffer để xử lý đồng bộ Log Event lên Cloud, loại bỏ nguy cơ giẫm đạp bộ nhớ (Race Condition).
- **Zero-Glitch Boot & Chống Brick**: 
  - Chốt trạng thái GPIO Relay ở mức an toàn ngay trong `setup()` trước khi khởi tạo FreeRTOS.
  - Tự động bỏ qua hàm kiểm tra tràn RAM (`monitorHeap`) khi đang thực hiện OTA, chống nguy cơ thiết bị treo giữa chừng làm hỏng Firmware.
- **Quản lý NVS Coexistence (Bảo vệ Flash)**: 
  - Cấu hình WiFi fallback và trạng thái Relay luôn được lưu độc lập qua `Preferences` (của Arduino), tách biệt với vùng NVS dành cho PKI certs của ESP RainMaker. Khi đổi Firmware, trạng thái cửa và mạng không bị xóa.
- **Bảo mật & Quản lý Mạng Khẩn cấp**: 
  - Khôi phục mạng khẩn cấp (Rescue AP): Khi mất kết nối WiFi 5 phút, thiết bị sẽ tự động phát sóng WiFi ảo (đối với bản Blynk) hoặc tự bật lại BLE Provisioning (đối với bản RainMaker) để người dùng cài đặt mạng.
  - Tự động tắt chế độ Khẩn cấp (Rescue AP / BLE) nếu đường truyền Internet được khôi phục trở lại, đảm bảo an ninh cho hệ thống.
  - Bắt buộc thiết lập tài khoản Admin ở lần đầu (First Boot Setup).
  - 100% API của Captive Portal hoạt động bằng Cờ Trạng Thái (Flags). Không sử dụng hàm `delay()` trong Callback, tránh hiện tượng DDoS hoặc treo luồng `async_tcp`.
  - Rate Limiting: Khóa cổng cấu hình 30 phút nếu nhập sai mật khẩu Admin 5 lần.

---

## 🔌 Kiến trúc Hệ thống & Sơ đồ mạch (Architecture & Schematics)

Sơ đồ nguyên lý toàn bộ hệ thống được thiết kế bằng QElectroTech. Xem chi tiết tại `docs/all_diagram_electric.svg`.

<p align="center">
  <img src="docs/all_diagram_electric.svg" width="100%" alt="Sơ đồ nguyên lý MyDoor IoT">
</p>

*Tham khảo chi tiết về luồng xử lý và phân bổ Core tại tài liệu [Software Architecture](docs/software_architecture.md).*

---

## 📂 Cấu trúc Mã nguồn (File Structure)

```text
MyDoor-IoT/
├── docs/                               # Tài liệu dự án
│   ├── all_diagram_electric.svg        # Sơ đồ nguyên lý (SVG)
│   └── software_architecture.md        # Luồng xử lý và phân bổ Core chi tiết
├── draw/
│   └── mainboard.qet                   # File gốc sơ đồ mạch (QElectroTech)
├── include/
│   ├── Config.h                        # Chân GPIO, Hằng số, Timeout, Watchdog
│   ├── ControlLogic.h                  # Logic Phần cứng (Core 1, Thread-safety)
│   ├── NetworkManager.h                # Logic Mạng & WebServer (Core 0, Dual Cloud)
│   └── WebUI.h                         # Mã HTML/CSS/JS (Captive Portal linh hoạt)
├── src/
│   ├── ControlLogic.cpp                # Xử lý Relay, Queue, Mutex, Memory WDT
│   ├── NetworkManager.cpp              # Async WebServer, Blynk/RainMaker, NTP, OTA
│   └── main.cpp                        # Khởi tạo FreeRTOS Task và Zero-Glitch Boot
├── platformio.ini                      # Cấu hình biên dịch (env:blynk, env:rainmaker)
├── partitions_rmaker.csv               # Phân vùng 6000 bytes nvs cho RainMaker
└── min_spiffs.csv                      # Phân vùng tiêu chuẩn cho Blynk
```

---

## 🚀 Hướng dẫn Cài đặt & Sử dụng

### 1. Build & Nạp Code
Sử dụng **VS Code** + **PlatformIO**. Trong `platformio.ini` đã chia sẵn 2 môi trường:
- Nạp bản Blynk: `pio run -e blynk -t upload`
- Nạp bản RainMaker: `pio run -e rainmaker -t upload`

*(Cả 2 môi trường đều được thiết lập tự động dùng file phân vùng bộ nhớ phù hợp).*

### 2. Thiết lập Lần đầu (First Boot) với bản Blynk
1. Cấp nguồn, hệ thống phát WiFi mặc định: **`HomeSmartbyMinh`** (Mật khẩu: `04011998`).
2. Truy cập `http://10.10.10.1`. Hệ thống bắt buộc tạo **Tài khoản Admin** (để bảo mật OTA và WebUI).
3. Đăng nhập và cấu hình WiFi nhà bạn, Blynk Token, Lịch hẹn giờ.

### 3. Cài đặt mạng qua ESP RainMaker (BLE Provisioning)
Nếu nạp bản RainMaker, thiết bị sẽ mở Bluetooth.
1. Tải App **ESP RainMaker** trên điện thoại.
2. Bật Bluetooth, quét và thêm thiết bị (Add Device).
3. Nhập mật khẩu WiFi mạng nhà bạn qua App RainMaker. Toàn bộ UI điều khiển cửa, đèn, nguồn sẽ tự động được đồng bộ lên App.

### 4. Đổi Firmware qua lại (OTA)
Hệ thống sử dụng ElegantOTA. Bạn có thể truy cập `http://<IP_ESP32>/update` (đăng nhập bằng tài khoản Admin đã tạo).
- Chọn file `firmware.bin` của Blynk hoặc RainMaker để upload.
- Vì sử dụng cơ chế lưu trữ *NVS Coexistence*, bạn có thể chuyển Firmware mượt mà. Nếu chuyển về Blynk, WiFi cũ vẫn còn trong bộ nhớ đệm `Preferences`. Nếu chuyển sang RainMaker, nó sẽ phát BLE để bạn claim thiết bị.

---

## 🛠 Thư viện nền tảng (Dependencies)
- [ESPAsyncWebServer](https://github.com/mathieucarbou/ESPAsyncWebServer) (Bất đồng bộ WebUI)
- [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA) (Cập nhật Firmware OTA)
- [Blynk](https://github.com/blynkkk/blynk-library) (Nền tảng Cloud IoT)
- Thư viện Core ESP-IDF RainMaker (Tích hợp sẵn)

---

## 📝 Giấy phép
Dự án được phân phối dưới giấy phép [MIT](LICENSE).