# Hướng dẫn Claude cho MyDoor IoT (Bản rút gọn)

## 1) Lệnh chuẩn
- Build Blynk: `python -m platformio run -e blynk`
- Build RainMaker: `python -m platformio run -e rainmaker`
- Upload Blynk: `python -m platformio run -e blynk -t upload`
- Upload RainMaker: `python -m platformio run -e rainmaker -t upload`
- Clean: `python -m platformio run -e <env> -t clean`

## 2) Kiến trúc bắt buộc
- Core 0: Network/Web/Cloud/OTA (non-blocking)
- Core 1: Relay & IO thời gian thực
- Core 0 -> Core 1 chỉ qua queue (`executeRemoteCommand`), không gọi relay trực tiếp từ cloud/web callback.

## 3) Trạng thái hiện tại đã chốt
- Build pass: `blynk`, `rainmaker`.
- `platformio.ini` đã tách deps theo env.
- RainMaker đã compile theo API core hiện tại.
- RainMaker không bật local WebUI runtime (gọi `setupWebServer()` chỉ khi `USE_LOCAL_WEB_STACK`).
- RainMaker có recovery mất mạng dài hạn: tự restart provisioning và tự dừng khi có IP lại.
- RainMaker state cửa không còn hardcode `STOPPED`; đã suy diễn theo lệnh gần nhất (`UP/DOWN/STOPPED`).
- Blynk giữ lockout 30 phút, guard replay sau reconnect, OTA auth đồng bộ theo admin.
- UI first-boot đã đồng bộ policy mật khẩu >= 8 ký tự.

## 4) Rule bảo mật/vận hành
- Không dùng `delay()` trong callback web async.
- Giữ `isOtaRunning` khi OTA để tránh reboot giữa chừng.
- Log hệ thống qua `logEvent()`.
- Giữ branding Rescue AP mặc định:
  - SSID: `SmartHomebyMinh`
  - Pass: `04011998`

## 5) Việc kế tiếp (ngoài code)
- Soak test board thật cho cả Blynk và RainMaker theo checklist runtime.
- Nếu public GitHub/CV: ưu tiên mô tả kiến trúc, test matrix, và ảnh thực tế vận hành.
