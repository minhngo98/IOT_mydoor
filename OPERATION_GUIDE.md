# TÀI LIỆU VẬN HÀNH & KIỂM ĐỊNH HỆ THỐNG MYDOOR (ESP32 DUAL-CORE)

Tài liệu này hướng dẫn cách cài đặt, vận hành và kiểm thử hệ thống điều khiển cửa cuốn MyDoor phiên bản ổn định (Production 1.0).

---

## 1. VẬN HÀNH QUA WEB CONFIG (MẠNG NỘI BỘ)
*Chỉ dùng khi mất mạng hoặc thiết bị mới tinh (First Boot).*

*   **Truy cập Rescue AP (Khi mất mạng):** Nếu hệ thống mất kết nối cả 2 mạng WiFi (Mạng chính và Mạng phụ) quá 5 phút, thiết bị sẽ tự động phát WiFi cấu hình. Tên và Mật khẩu WiFi này là do bạn tự cài đặt trước đó, hoặc sẽ dùng mặc định là `SmartHomebyMinh` (Mật khẩu: `04011998`) nếu bạn chưa đổi.
*   **Trang cấu hình (WebUI):** Dùng điện thoại kết nối vào WiFi Rescue AP, mở trình duyệt vào địa chỉ `http://10.10.10.1`.
*   **Đăng ký Admin (First Boot):** Nếu là máy mới, trang web sẽ bắt buộc bạn tạo 1 tài khoản `Admin` và `Password` (Pass phải > 8 ký tự). Tài khoản này dùng để đăng nhập WebUI và OTA sau này.
*   **Cài đặt thông số:**
    *   **Mạng & Cloud:** Nhập tên và Mật khẩu cho **2 mạng WiFi** (WiFi chính và WiFi dự phòng, để mạch tự động đảo đổi khi 1 mạng bị rớt). Nếu chạy bản Blynk, dán thêm mã `Blynk Template`, `Name`, `Auth Token`. Bấm `Lưu & Khởi động lại`.
    *   **Lịch trình (Schedule):** Hẹn giờ tự động Đóng/Cắt Nguồn tổng Box Cửa và Đèn. Chọn các ngày trong tuần.
    *   **Điều khiển tại chỗ (Local Control):** Bấm trực tiếp các nút [LÊN] [XUỐNG] [DỪNG] [Nguồn Box] [Đèn] trên web mà không cần Internet.
    *   **Xem Log:** Theo dõi các sự kiện (Ai mở cửa, khi nào, có lỗi RAM/NTP gì không) ở mục `Terminal Logs`.

---

## 2. VẬN HÀNH QUA APP BLYNK (INTERNET)
*Yêu cầu nạp Firmware bản `blynk` (`pio run -e blynk`).*

*   **Giao diện App:** Tạo các Widget tương ứng với Virtual PIN đã quy định trong code (`Config.h`).
    *   `V0`: Nút nhấn nhả (Button) - **MỞ CỬA**
    *   `V1`: Nút nhấn nhả (Button) - **ĐÓNG CỬA**
    *   `V2`: Nút nhấn nhả (Button) - **DỪNG**
    *   `V3`: Công tắc (Switch) - **BẬT/TẮT Nguồn Box** (Chống trộm cạy cửa ban đêm).
    *   `V5`: Công tắc (Switch) - **BẬT/TẮT Đèn**
    *   `V4`: Widget Terminal - **Nhận lịch sử Log** đổ về từ thiết bị theo thời gian thực.
*   **Lưu ý:** Lệnh điều khiển từ App sẽ bị từ chối nếu thiết bị đang mất mạng (Cloud offline) để đảm bảo an toàn.

---

## 3. VẬN HÀNH QUA ESP RAINMAKER (APP ESP RAINMAKER)
*Yêu cầu nạp Firmware bản `rainmaker` (`pio run -e rainmaker`).*

*   **Provisioning (Cấp phép mạng):** Không dùng WiFi Rescue AP. Bạn cấp nguồn cho mạch, bật Bluetooth và WiFi trên điện thoại, mở App `ESP RainMaker`.
*   **Thêm thiết bị:** App sẽ tự động quét thấy thiết bị BLE tên `PROV_...`. Làm theo hướng dẫn trên màn hình để nạp WiFi cho mạch.
*   **Điều khiển:** Sau khi kết nối, Dashboard trên App sẽ tự sinh ra các nút [Up] [Down] [Stop] và công tắc [Power Box], [Light]. Các trạng thái sẽ đồng bộ tức thời (MQTT).

---

## 4. HƯỚNG DẪN ĐỔI/CẬP NHẬT FIRMWARE (OTA)

Hệ thống hỗ trợ nạp chéo (Cross-flash) từ Blynk sang RainMaker và ngược lại mà không cần cắm cáp.

*   **Bước 1 - Chuẩn bị:** Chạy lệnh `pio run -e blynk` hoặc `pio run -e rainmaker` trên máy tính để tạo file `firmware.bin` mới nhất.
*   **Bước 2 - Truy cập Web:** Đảm bảo điện thoại/laptop đang chung mạng WiFi với thiết bị (Hoặc đang kết nối vào Rescue AP `10.10.10.1`).
*   **Bước 3 - Cổng OTA:** Mở trình duyệt, truy cập `http://<IP_THIET_BI>/update` (Ví dụ: `http://10.10.10.1/update` hoặc `http://192.168.1.50/update`).
*   **Bước 4 - Xác thực:** Nhập tài khoản `Admin` đã tạo ở phần First Boot.
*   **Bước 5 - Nạp File:** Chọn file `.bin` vừa build, bấm **Upload**. Đợi thanh tiến trình chạy đến 100%, thiết bị sẽ tự Reboot.
*   *(Lưu ý: Quá trình OTA đã được bọc khóa RAM nên 100% không bị Brick giữa chừng).*

---

## 5. KỊCH BẢN TEST KIỂM ĐỊNH (UAT - CHỨNG MINH ĐỘ BỀN 10 NĂM)

Sau khi ráp mạch, hãy thực hiện bài Test khắc nghiệt sau để nghiệm thu:

1.  **Bài Test: Zero-Glitch (Chống chớp Relay khi cúp điện)**
    *   *Cách test:* Rút nguồn điện 220V của hệ thống, sau đó cắm phích lại đột ngột. Làm liên tục 5 lần.
    *   *Kỳ vọng (PASS):* Cửa cuốn không nhúc nhích dù chỉ 1mm. Đèn xanh/đỏ (Relay điều khiển) không hề chớp lóe sáng lúc vừa có điện.
2.  **Bài Test: Fail-Safe Network (Mạng sập - Điều khiển vẫn mượt)**
    *   *Cách test:* Rút dây mạng của Modem WiFi nhà bạn (Để WiFi vẫn phát nhưng không có Internet). Dùng nút bấm cứng (vật lý) trên mạch hoặc giao diện Web Local `10.10.10.1` để mở cửa.
    *   *Kỳ vọng (PASS):* Cửa phản hồi ngay lập tức (<100ms) không có độ trễ, không bị đơ, không bị Watchdog Reset dù Core 0 đang mải miết dò tìm Cloud. *(Chứng minh Dual-Core hoạt động hoàn hảo).*
3.  **Bài Test: Chống kẹt phím (Anti-Jamming)**
    *   *Cách test:* Giữ chặt nút cứng (Reset/Boot) hoặc nhấn liên tục nút Mở Cửa trên App Blynk như đánh điện tín (10 lần/giây).
    *   *Kỳ vọng (PASS):* Queue (Hàng đợi lệnh) sẽ lọc bớt các lệnh thừa, chỉ nhả Relay đúng 500ms cho mỗi lệnh hợp lệ. Mạch không bị treo (Memory Panic).
4.  **Bài Test: An ninh Nông thôn (Chống trộm dò Pass)**
    *   *Cách test:* Mở Rescue AP lên. Dùng điện thoại cố tình nhập sai mật khẩu WiFi (hoặc nhập sai tài khoản Web Admin) liên tục 5 lần.
    *   *Kỳ vọng (PASS):* Thiết bị từ chối kết nối và tự động "đóng băng" không cho nhập tiếp trong vòng 30 phút. Terminal Log ghi nhận cảnh báo Bruteforce.