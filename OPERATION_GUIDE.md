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

### 4.1.1 Mở/Tắt AP khi Internet vẫn đang hoạt động
- Trên WebUI (đã login Admin), gọi API:
  - Bật AP: `POST /ap_mode` với `state=1`
  - Tắt AP: `POST /ap_mode` với `state=0`
- Có thể dùng trực tiếp từ form/script nội bộ hoặc tool HTTP theo hạ tầng hiện có.
- Lưu ý: endpoint này vẫn áp dụng lockout/auth như các endpoint admin khác.

### 4.1.2 Nút cứng GPIO2 (All-in-one)
- Nhấn ngắn (<3s): bật/tắt Rescue AP.
- Giữ >=3s rồi thả: reboot thiết bị.
- Giữ >=10s rồi thả: factory reset rồi reboot.
- Cơ chế quyết định theo thời điểm thả nút để tránh xung đột 3s/10s.

### 4.1.3 Mapping 4 LED trạng thái hệ thống
- Blue GPIO13: sáng khi có mạng STA (online, không ở AP mode).
- Green GPIO14: sáng khi hệ thống ready (`!first-boot` và không pending reboot).
- Red GPIO4: sáng khi hệ thống bị lockout bảo mật.
- Yellow GPIO16: trạng thái AP/reset:
  - AP đang phát: LED vàng chớp liên tục.
  - AP tắt: LED vàng tắt.
  - Reboot từ GPIO2: LED vàng flash 1 nhịp.
  - Factory reset từ GPIO2: LED vàng flash 3 nhịp.

### 4.1.4 Blynk giám sát LED
- V6: Blue LED state
- V7: Green LED state
- V8: Red LED state
- V9: Yellow LED state

### 4.1.5 Cấu hình Blynk Dashboard đề xuất (Web + Mobile)
1. Tạo 4 Datastream kiểu Integer (0/1):
   - `V6` (LED_BLUE)
   - `V7` (LED_GREEN)
   - `V8` (LED_RED)
   - `V9` (LED_YELLOW)
2. Thêm widget hiển thị cho từng datastream:
   - Ưu tiên widget **LED** (nếu template có sẵn).
   - Nếu không có LED widget, dùng **Labeled Value** hiển thị 0/1.
3. Đặt tên và màu dễ quan sát:
   - V6: Blue / `WiFi_STA`
   - V7: Green / `System_Ready`
   - V8: Red / `Security_Lockout`
   - V9: Yellow / `AP_Reset_Status`
4. Chế độ vận hành:
   - Các widget LED này là **monitor-only**, không dùng để điều khiển.
   - Firmware tự đẩy trạng thái mỗi ~1 giây.

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

### 4.4 Log trạng thái persistent 3 ngày
- Firmware lưu lịch sử log vào NVS (namespace `mydoor_logs`), giữ tối đa 3 ngày gần nhất.
- Sau reboot/mất điện, Blynk V4 sẽ replay lại log cũ -> mới.
- API đọc log:
  - Admin: `GET /logs` (cần auth)
  - Public read-only: `GET /public_logs` (không cần auth)
- Chuẩn tag hiển thị trên log:
  - `[ON]` -> trạng thái ON (màu gợi ý: xanh lá)
  - `[OFF]` -> trạng thái OFF (màu gợi ý: xám)
  - `[AUTO]` -> hành vi automation/override (màu gợi ý: xanh dương)
  - Tag khác -> màu mặc định (trắng)

### 4.5 Rule manual override cho GPIO32/GPIO33
- Nếu user thao tác tay (WebUI/Blynk/RainMaker) và trạng thái tay trái lịch automation hiện tại:
  - Kích hoạt override cho relay tương ứng.
  - Scheduler tạm thời không ép trạng thái relay đó.
- Override chỉ giữ đến mốc lịch kế tiếp (khi lịch đổi ON<->OFF), sau đó tự clear và automation lấy lại quyền.

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

## 8) Runtime test script checklist (board thật)

### 8.1 Pre-check
- [ ] Build pass `blynk` và `rainmaker`.
- [ ] Flash đúng env cần test.
- [ ] Mở Serial Monitor 115200 để quan sát log.

### 8.2 Script test AP qua WebUI (khi internet đang hoạt động)
1. Điều kiện đầu vào:
   - Thiết bị đang online STA, WebUI đăng nhập Admin thành công.
2. Bước thực hiện:
   - Gọi `POST /ap_mode` với `state=1`.
3. Kỳ vọng:
   - Trả về HTTP 200 `OK`.
   - Xuất hiện SSID Rescue AP.
   - LED vàng chớp liên tục.
4. Bước tiếp:
   - Gọi `POST /ap_mode` với `state=0`.
5. Kỳ vọng:
   - Trả về HTTP 200 `OK`.
   - Rescue AP biến mất.
   - Thiết bị vẫn duy trì/khôi phục STA.
   - LED vàng tắt.

### 8.3 Script test GPIO2 all-in-one
1. Nhấn ngắn GPIO2 (<3s):
   - [ ] Lần 1: AP bật.
   - [ ] Lần 2: AP tắt.
2. Giữ GPIO2 >=3s và <10s rồi thả:
   - [ ] Thiết bị reboot.
   - [ ] LED vàng flash 1 nhịp trước reboot.
3. Giữ GPIO2 >=10s rồi thả:
   - [ ] Xóa NVS `mydoor` + `mydoor_state`.
   - [ ] Thiết bị reboot.
   - [ ] LED vàng flash 3 nhịp trước reboot.

### 8.4 Script test AP auto-recovery (mất mạng dài)
1. Điều kiện đầu vào:
   - Thiết bị có cấu hình WiFi hợp lệ.
2. Bước thực hiện:
   - Ngắt internet/router để thiết bị mất mạng >5 phút.
3. Kỳ vọng:
   - [ ] AP tự bật (auto mode).
4. Bước thực hiện:
   - Khôi phục internet.
5. Kỳ vọng:
   - [ ] AP auto mode tự tắt sau khi STA recovered.
   - [ ] Hệ thống quay lại vận hành bình thường.

### 8.5 Script test log persistent + public logs
1. Tạo một số hành động sinh log ON/OFF/AUTO (power/light/AP).
2. Kiểm tra realtime trên V4 Terminal.
3. Reboot thiết bị và kiểm tra:
   - [ ] V4 replay lại history sau reconnect.
4. Gọi API đọc log:
   - [ ] `GET /logs` với admin auth trả về log.
   - [ ] `GET /public_logs` không auth vẫn đọc được log.
5. Kỳ vọng format:
   - [ ] Log có tag `[ON]` / `[OFF]` / `[AUTO]` đúng ngữ nghĩa.

### 8.6 Script test manual override tới mốc lịch kế tiếp
1. Chọn lịch đang OFF cho relay nguồn (GPIO32), user bật tay từ Web/Blynk/RainMaker.
   - [ ] Relay giữ ON, scheduler chưa ép tắt ngay.
2. Đợi đến mốc lịch kế tiếp.
   - [ ] Override tự clear.
   - [ ] Automation lấy lại quyền điều khiển theo lịch.
3. Lặp tương tự cho relay đèn (GPIO33).

### 8.7 Hồi quy chức năng cũ
- [ ] Web Blynk login OK.
- [ ] OTA upload OK.
- [ ] RainMaker add device OK.
- [ ] Điều khiển cửa/đèn/nguồn OK.
- [ ] `/save_rescue_ap` vẫn giữ cấu hình Rescue AP do user chỉnh.
- [ ] Blynk reconnect/guard vẫn hoạt động như trước.
- [ ] Không vi phạm kiến trúc Core0->Core1 queue.

### 8.8 Tiêu chí PASS/FAIL
- PASS: tất cả checkbox đều đạt.
- FAIL: chỉ cần 1 mục không đạt, lưu log Serial + timestamp + bước test và rollback về bản ổn định gần nhất để khoanh vùng.
