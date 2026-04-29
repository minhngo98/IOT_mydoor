# OPERATION GUIDE (No-Code)

Mục tiêu: người không rành kỹ thuật vẫn nạp firmware, cấu hình, và đổi qua lại Blynk/RainMaker.

## 1) Chuẩn bị
- Máy tính đã cài VSCode + PlatformIO.
- Cáp USB data cho ESP32.
- Điện thoại có WiFi + Bluetooth.

## 2) Lấy file firmware (.bin)
Mở terminal tại thư mục dự án:

- Blynk:
  - `python -m platformio run -e blynk`
  - File tạo ra: `.pio/build/blynk/firmware.bin`

- RainMaker:
  - `python -m platformio run -e rainmaker`
  - File tạo ra: `.pio/build/rainmaker/firmware.bin`

## 3) Nạp firmware bằng cáp USB (lần đầu)
- Nạp Blynk: `python -m platformio run -e blynk -t upload`
- Nạp RainMaker: `python -m platformio run -e rainmaker -t upload`

Đợi dòng `SUCCESS` rồi khởi động lại thiết bị.

## 4) Dùng bản Blynk (cấu hình Web)
### 4.1 Vào Rescue AP
- WiFi mặc định: **SmartHomebyMinh**
- Mật khẩu: **04011998**
- Mở trình duyệt: `http://10.10.10.1`

### 4.2 Thiết lập lần đầu
1. Tạo tài khoản Admin (mật khẩu tối thiểu 8 ký tự).
2. Đăng nhập.
3. Vào tab Network & Cloud:
   - Nhập WiFi chính/phụ.
   - Nhập Blynk Template ID / Template Name / Auth Token.
4. Bấm Lưu, chờ reboot.

### 4.3 OTA (đổi firmware không cần cáp)
- Vào `http://<IP-thiết-bị>/update`
- Đăng nhập bằng tài khoản Admin.
- Chọn file `.bin` cần nạp -> Upload -> chờ reboot.

## 5) Dùng bản RainMaker (cấu hình App)
1. Nạp firmware RainMaker.
2. Mở app **ESP RainMaker** trên điện thoại.
3. Bật Bluetooth, Add Device.
4. Chọn WiFi nhà và hoàn tất provisioning.

Khi mất mạng dài, thiết bị tự bật lại provisioning; có mạng lại sẽ tự dừng.

## 6) Đổi qua lại Blynk <-> RainMaker
### Từ Blynk sang RainMaker
- OTA hoặc cáp USB nạp firmware RainMaker.
- Mở app RainMaker để claim lại thiết bị.

### Từ RainMaker sang Blynk
- OTA hoặc cáp USB nạp firmware Blynk.
- Vào Web để kiểm tra/cập nhật cloud Blynk.

## 7) Lỗi thường gặp
- Không chạy được `pio`: dùng `python -m platformio ...`.
- Upload lỗi cổng COM: rút cáp, chọn lại cổng, nạp lại.
- Không vào được Web: kiểm tra đã kết nối đúng WiFi thiết bị.
- Sai mật khẩu nhiều lần: hệ thống khóa đăng nhập 30 phút.

## 8) Checklist bàn giao
- Build pass `blynk` và `rainmaker`.
- Web Blynk login OK.
- OTA upload OK.
- RainMaker add device OK.
- Điều khiển cửa/đèn/nguồn OK.
