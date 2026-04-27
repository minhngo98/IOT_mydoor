# MyDoor IoT - Smart Roller Door Controller

![Build Status](https://img.shields.io/badge/build-passing-brightgreen)
![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![Framework](https://img.shields.io/badge/framework-Arduino-orange)
![Architecture](https://img.shields.io/badge/architecture-FreeRTOS%20Dual--Core-red)
![License](https://img.shields.io/badge/license-MIT-green)

Firmware điều khiển cửa cuốn và đèn chiếu sáng dựa trên vi điều khiển ESP32, sử dụng kiến trúc **FreeRTOS Dual-Core** giúp hệ thống hoạt động ổn định, phản hồi thời gian thực và an toàn.

---

## 📸 Giao diện trực quan (Gallery)

> **Lưu ý:** Phần này sẽ được cập nhật hình ảnh sau.
> 
> <!-- [Ảnh chụp WebUI Dashboard] -->
> <!-- [Ảnh chụp App Blynk] -->
> <!-- [Ảnh chụp Tính năng Hẹn giờ] -->

---

## 🌟 Tính năng nổi bật

- **Kiến trúc FreeRTOS Dual-Core**: Phân tách tác vụ (Network ở Core 0, Điều khiển phần cứng ở Core 1) đảm bảo nút bấm luôn phản hồi ngay lập tức (<10ms). Giao tiếp giữa 2 lõi thông qua Queue và Mutex để đảm bảo **Thread-safety**.
- **Tự động phục hồi (Self-Healing)**: Tích hợp Hardware/Task Watchdog Timer (8s) và giám sát RAM để tự động reboot khi hệ thống treo hoặc cạn bộ nhớ.
- **Bảo vệ Flash (Wear Leveling)**: Sử dụng NVS với thuật toán "Dirty Flag" (chỉ ghi khi có thay đổi) để tối đa hóa tuổi thọ bộ nhớ Flash của ESP32.
- **Zero-Glitch Boot**: Khởi tạo GPIO an toàn, chốt trạng thái Relay trước khi cấp nguồn, chống hiện tượng chớp giật hoặc tự mở cửa khi mất điện có lại.
- **Quản lý AP Thông minh (Rescue AP)**: Tự động phát WiFi mặc định (`HomeSmartbyMinh`) kèm chu kỳ bật/tắt (10 phút ON / 5 phút OFF) khi mất Internet để cho phép cấu hình lại.
- **Quản lý Cấu hình Tập trung**: WebUI Config hiện đại, hỗ trợ cài đặt WiFi, Thiết lập Giờ Bật/Tắt Cấp Nguồn Tổng và Đèn chiếu sáng.
- **Bảo mật mạnh mẽ**: 
  - Yêu cầu bắt buộc tạo tài khoản (First Boot Setup) ở lần khởi động đầu tiên.
  - Hỗ trợ Rate Limiting chống Brute-force (khóa 30 phút nếu nhập sai 5 lần).
- **Hỗ trợ Nút Bấm Vật Lý (Nút Reset & Đèn)**: 
  - Nhấn giữ 3s để Reboot thiết bị.
  - Nhấn giữ 10s để Khôi phục cài đặt gốc (Factory Reset) và xóa trắng dữ liệu cấu hình.

---

## 🔌 Kiến trúc Hệ thống & Sơ đồ mạch (Architecture & Schematics)

<!-- [Ảnh sơ đồ hệ thống nếu có] -->

- **Tài liệu vẽ mạch gốc:** `draw/mainboard.qet` (Mở bằng QElectroTech).

---

## 📂 Cấu trúc Mã nguồn (File Structure)

- `include/Config.h`: Khai báo chân GPIO, Watchdog, hằng số cấu hình.
- `include/NetworkManager.h` & `src/NetworkManager.cpp`: Xử lý WiFi, Async WebServer, Captive Portal, Blynk, OTA và NTP trên Core 0.
- `include/ControlLogic.h` & `src/ControlLogic.cpp`: Xử lý logic Relay, Đèn chiếu sáng, Nút bấm cứng, Queue lệnh, bảo vệ Flash NVS trên Core 1. Sử dụng Mutex để đảm bảo an toàn truy cập chéo.
- `include/WebUI.h`: Giao diện Cài đặt HTML/CSS/JS được biên dịch trực tiếp.
- `src/main.cpp`: Khởi tạo FreeRTOS, phân bổ Task và cấu hình Watchdog.

---

## 🚀 Hướng dẫn Cài đặt & Sử dụng

### 1. Biên dịch (PlatformIO)
1. Mở dự án bằng VS Code có cài đặt [PlatformIO](https://platformio.org/).
2. Đảm bảo cấu hình phân vùng `min_spiffs.csv` trong `platformio.ini`. (Lưu ý: Bạn có thể bật `extra_scripts = pre:scripts/erase_nvs.py` trong `platformio.ini` nếu muốn xóa sạch NVS trước khi nạp code).
3. Bấm **Build** cho môi trường `env:blynk`.
4. Kết nối ESP32 và bấm **Upload**.

### 2. Trạng thái Xuất Xưởng (Bắt đầu sử dụng)
1. Cấp nguồn, hệ thống sẽ phát WiFi mặc định: **`HomeSmartbyMinh`** (Mật khẩu: `04011998`).
2. Truy cập vào IP `http://10.10.10.1`. Hệ thống sẽ bắt buộc yêu cầu tạo Tài khoản Quản Trị (Admin) để có thể vào được Bảng Điều Khiển.
3. Sau khi tạo tài khoản thành công và đăng nhập, bạn có thể thiết lập WiFi, Múi giờ, cấu hình Blynk (Template ID, Name, Auth Token) và Lịch trình tự động.
4. Nút nhấn chức năng trên mạch (GPIO 2):
   - **Nhấn giữ 3s**: Khởi động lại mạch (Reboot).
   - **Nhấn giữ 10s**: Factory Reset (Xóa toàn bộ NVS và reboot về trạng thái xuất xưởng).

---

## 🛠 Thư viện sử dụng
- [ESPAsyncWebServer](https://github.com/mathieucarbou/ESPAsyncWebServer)
- [ElegantOTA](https://github.com/ayushsharma82/ElegantOTA)
- [Blynk](https://github.com/blynkkk/blynk-library)

---

## 📝 Giấy phép
Dự án được phân phối dưới giấy phép [MIT](LICENSE).