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

## 5) Hành vi AP/GPIO2/LED đã chốt (Blynk local web stack)
- Endpoint Web mới: `POST /ap_mode` (auth bắt buộc).
  - `state=1|on|true` -> bật Rescue AP.
  - `state=0|off|false` -> tắt Rescue AP, trả về STA nếu có cấu hình WiFi.
- GPIO2 (PIN_BTN_RESET) all-in-one theo nhấn-nhả:
  - Nhấn ngắn (<3s): toggle AP ON/OFF.
  - Giữ >=3s và <10s rồi thả: reboot.
  - Giữ >=10s rồi thả: factory reset + reboot.
- LED vàng `PIN_LED_WARN` (GPIO16):
  - AP ON: chớp liên tục (non-blocking).
  - AP OFF: tắt.
  - Trigger reboot: flash 1 nhịp.
  - Trigger factory reset: flash 3 nhịp.
- Mapping runtime 4 LED:
  - Blue `GPIO13`: ON khi có mạng STA (`isConnected && !isApMode`).
  - Green `GPIO14`: ON khi hệ thống sẵn sàng (`!isFirstBoot && !pendingReboot`).
  - Red `GPIO4`: ON khi lockout bảo mật (`isLockedOut`).
  - Yellow `GPIO16`: trạng thái AP/reset như trên.
- Blynk đã có VPIN giám sát LED:
  - `V6` Blue, `V7` Green, `V8` Red, `V9` Yellow.

## 6) Việc kế tiếp (ngoài code)
- Soak test board thật cho cả Blynk và RainMaker theo checklist runtime.
- Nếu public GitHub/CV: ưu tiên mô tả kiến trúc, test matrix, và ảnh thực tế vận hành.
