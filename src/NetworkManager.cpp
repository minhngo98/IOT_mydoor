#include <Arduino.h>
#include <HardwareSerial.h>
#include "NetworkManager.h"
#include "WebUI.h"

// Do lỗi macro trùng lặp HTTP_GET/POST giữa AsyncWebServer và thư viện WebServer (của ElegantOTA),
// ta dùng trực tiếp webrequestmethod
#define ASYNC_GET HTTP_GET
#define ASYNC_POST HTTP_POST

NetworkManager netManager;

// ISR Handler cho nút BOOT (Phải đặt ở ngoài class)
void IRAM_ATTR isr_boot_button() {
  netManager.handleInterruptBoot();
}

NetworkManager::NetworkManager() : server(80), isApMode(false), isConnected(false),
  lastWiFiCheck(0), apStartTime(0), apOfflineTime(0), wifiLostTime(0), wifiLostFlag(false),
  failedAuthCount(0), lockoutStartTime(0), isLockedOut(false), interruptTriggered(false) {
}

void NetworkManager::handleInterruptBoot() {
  interruptTriggered = true;
}

void NetworkManager::begin() {
  pinMode(PIN_BTN_CONFIG, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BTN_CONFIG), isr_boot_button, FALLING);
  loadConfig();

  // Mạng WiFi chưa được thiết lập, chạy chế độ Access Point
  if (ssid == "") {
    Serial.println("[WIFI] Chua co cau hinh WiFi. Vao che do AP (10.10.10.1)...");
    setupAP();
  } else {
    setupSTA();
  }
}

void NetworkManager::loadConfig() {
  preferences.begin("mydoor", false); // Két sắt NVS tên "mydoor"

  ssid = preferences.getString("ssid", "");
  password = preferences.getString("pass", "");
  ssid2 = preferences.getString("ssid2", "");
  pass2 = preferences.getString("pass2", "");

  adminUser = preferences.getString("admin_user", "admin"); // Mặc định là admin
  adminPass = preferences.getString("admin_pass", "admin");

  blynk_tmpl = preferences.getString("blynk_tmpl", "");
  blynk_name = preferences.getString("blynk_name", "");
  blynk_auth = preferences.getString("blynk_auth", "");

  timezone = preferences.getChar("timezone", 7); // Mặc định UTC+7
  on_hour = preferences.getUChar("on_hour", 6); // Mặc định bật 6h sáng
  on_min = preferences.getUChar("on_min", 0);
  off_hour = preferences.getUChar("off_hour", 23); // Tắt 23h đêm
  off_min = preferences.getUChar("off_min", 0);
  schedule_days = preferences.getUChar("days", 127); // Mặc định cả tuần (1111111 = 127)

  preferences.end();
  Serial.println("[NVS] Da tai cau hinh: WiFi=" + ssid + ", Admin=" + adminUser);
}

void NetworkManager::setupAP() {
  isApMode = true;
  WiFi.mode(WIFI_AP);

  // Set IP tĩnh cho Captive Portal: 10.10.10.1
  IPAddress local_ip(10, 10, 10, 1);
  IPAddress gateway(10, 10, 10, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_ip, gateway, subnet);

  // Ẩn SSID tăng bảo mật
  WiFi.softAP("MyDoor_Setup", "12345678", 1, 1); // 1 = Channel 1, 1 = Hidden SSID
  Serial.println("[AP] Phat Hidden AP. IP: 10.10.10.1 (Pass: 12345678)");

  apStartTime = millis();
  setupWebServer();
}

void NetworkManager::setupSTA() {
  isApMode = false;
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(true); // Tiết kiệm điện, chống nóng

  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("[STA] Dang ket noi toi WiFi Chinh: ");
  Serial.println(ssid);

  // Không chặn (Non-blocking) vòng lặp ở đây. Sẽ kiểm tra trạng thái trong loop()
  setupWebServer();
}

void NetworkManager::setupWebServer() {
  // Giao diện chính (Bắt buộc Đăng Nhập HTTP Basic Auth)
  server.on("/", ASYNC_GET, [](AsyncWebServerRequest *request){
    if (netManager.isLockedOut) {
        return request->send(429, "text/plain", "Too Many Requests. Locked for 30 minutes.");
    }
    if(!request->authenticate(netManager.adminUser.c_str(), netManager.adminPass.c_str())) {
      netManager.failedAuthCount++;
      if (netManager.failedAuthCount >= 5) {
          netManager.isLockedOut = true;
          netManager.lockoutStartTime = millis();
          Serial.println("[SECURITY] Đã khóa truy cập AP 30 phút do sai Pass 5 lần!");
      }
      return request->requestAuthentication("MyDoor Config Admin");
    }

    // Auth thành công -> Reset
    netManager.failedAuthCount = 0;
    netManager.apStartTime = millis(); // Reset chu kỳ 10 phút vì có người xài
    request->send_P(200, "text/html", index_html);
  });

  // API Quét WiFi (JSON)
  server.on("/scan", ASYNC_GET, [](AsyncWebServerRequest *request){
    if (netManager.isLockedOut) return request->send(429, "text/plain", "Locked");
    if(!request->authenticate(netManager.adminUser.c_str(), netManager.adminPass.c_str()))
      return request->send(401, "application/json", "{\"error\":\"Unauthorized\"}");

    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    request->send(200, "application/json", json);
  });

  // API Lấy Cấu Hình Cũ để hiện lên Form
  server.on("/get_config", ASYNC_GET, [](AsyncWebServerRequest *request){
    if (netManager.isLockedOut) return request->send(429, "text/plain", "Locked");
    String json = "{\"timezone\":" + String(netManager.timezone) +
                  ",\"on_hour\":" + String(netManager.on_hour) +
                  ",\"on_min\":" + String(netManager.on_min) +
                  ",\"off_hour\":" + String(netManager.off_hour) +
                  ",\"off_min\":" + String(netManager.off_min) +
                  ",\"days\":" + String(netManager.schedule_days) +
                  ",\"blynk_tmpl\":\"" + netManager.blynk_tmpl + "\"" +
                  ",\"blynk_name\":\"" + netManager.blynk_name + "\"" +
                  ",\"blynk_auth\":\"" + netManager.blynk_auth + "\"" +
                  ",\"ssid2\":\"" + netManager.ssid2 + "\"}";
    request->send(200, "application/json", json);
  });

  // API Lưu WiFi & Cloud (Sẽ reboot sau 3 giây)
  server.on("/save_wifi", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (netManager.isLockedOut) return request->send(429, "text/plain", "Locked");
    if(!request->authenticate(netManager.adminUser.c_str(), netManager.adminPass.c_str())) return request->send(401);

    if(request->hasParam("ssid", true) && request->hasParam("password", true)) {
      String newSSID = request->getParam("ssid", true)->value();
      String newPass = request->getParam("password", true)->value();

      Preferences p; p.begin("mydoor", false);
      p.putString("ssid", newSSID);
      p.putString("pass", newPass);
      if(request->hasParam("ssid2", true)) p.putString("ssid2", request->getParam("ssid2", true)->value());
      if(request->hasParam("pass2", true)) p.putString("pass2", request->getParam("pass2", true)->value());
      if(request->hasParam("blynk_tmpl", true)) p.putString("blynk_tmpl", request->getParam("blynk_tmpl", true)->value());
      if(request->hasParam("blynk_name", true)) p.putString("blynk_name", request->getParam("blynk_name", true)->value());
      if(request->hasParam("blynk_auth", true)) p.putString("blynk_auth", request->getParam("blynk_auth", true)->value());
      p.end();

      request->send(200, "text/plain", "OK");
      delay(2000); ESP.restart();
    } else request->send(400, "text/plain", "Missing args");
  });

  // API Lưu Lịch Trình Relay 4
  server.on("/save_schedule", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (netManager.isLockedOut) return request->send(429, "text/plain", "Locked");
    if(!request->authenticate(netManager.adminUser.c_str(), netManager.adminPass.c_str())) return request->send(401);

    Preferences p; p.begin("mydoor", false);
    if(request->hasParam("timezone", true)) p.putChar("timezone", request->getParam("timezone", true)->value().toInt());
    if(request->hasParam("on_hour", true)) p.putUChar("on_hour", request->getParam("on_hour", true)->value().toInt());
    if(request->hasParam("on_min", true))  p.putUChar("on_min", request->getParam("on_min", true)->value().toInt());
    if(request->hasParam("off_hour", true)) p.putUChar("off_hour", request->getParam("off_hour", true)->value().toInt());
    if(request->hasParam("off_min", true))  p.putUChar("off_min", request->getParam("off_min", true)->value().toInt());

    uint8_t days = 0;
    for(int i=0; i<7; i++) {
      if(request->hasParam("day_" + String(i), true)) days |= (1 << i);
    }
    p.putUChar("days", days);
    p.end();

    netManager.loadConfig(); // Load lại ngay vào RAM

    // Cập nhật lại NTP
    configTime(netManager.timezone * 3600, 0, "pool.ntp.org", "time.nist.gov");

    request->send(200, "text/plain", "OK");
  });

  // API Lưu Tài Khoản Admin
  server.on("/save_admin", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (netManager.isLockedOut) return request->send(429, "text/plain", "Locked");
    if(!request->authenticate(netManager.adminUser.c_str(), netManager.adminPass.c_str())) return request->send(401);

    if(request->hasParam("admin_user", true) && request->hasParam("admin_pass", true)) {
      Preferences p; p.begin("mydoor", false);
      p.putString("admin_user", request->getParam("admin_user", true)->value());
      p.putString("admin_pass", request->getParam("admin_pass", true)->value());
      p.end();
      netManager.loadConfig();
      request->send(200, "text/plain", "OK");
    } else request->send(400, "text/plain", "Bad Request");
  });

  // API Reboot
  server.on("/reboot", ASYNC_POST, [](AsyncWebServerRequest *request){
    if (netManager.isLockedOut) return request->send(429, "text/plain", "Locked");
    if(!request->authenticate(netManager.adminUser.c_str(), netManager.adminPass.c_str())) return request->send(401);
    request->send(200, "text/plain", "Rebooting");
    delay(1000); ESP.restart();
  });

  // Khởi động ElegantOTA (/update) -> Nạp file .bin từ trình duyệt
  ElegantOTA.begin(&server, adminUser.c_str(), adminPass.c_str());

  server.begin();
  Serial.println("[WEB] Web Server & OTA san sang.");
}

void NetworkManager::checkAPCycle() {
  if (isApMode) {
      if (millis() - apStartTime >= AP_CYCLE_ON_MS) {
          // Hết 10 phút, Tắt AP, nghỉ 5 phút
          Serial.println("[AP CYCLE] AP da bat 10 phut, tat AP trong 5 phut...");
          isApMode = false;
          WiFi.softAPdisconnect(true);
          WiFi.mode(WIFI_STA); // Trả về STA để dò mạng
          apOfflineTime = millis();
      }
  } else {
      if (wifiLostFlag && millis() - apOfflineTime >= AP_CYCLE_OFF_MS) {
          // Hết 5 phút nghỉ, bật lại AP 10 phút
          Serial.println("[AP CYCLE] AP da nghi 5 phut, bat lai AP 10 phut...");
          setupAP();
      }
  }
}

void NetworkManager::loop() {
  // Xử lý Ngắt Nút BOOT (Wake-up AP)
  if (interruptTriggered) {
    interruptTriggered = false;
    static unsigned long lastPress = 0;
    if (millis() - lastPress > 2000) { // Chống dội phím cơ bản
        lastPress = millis();
        // Kiểm tra xem nút có được giữ 5 giây không (Cần hàm non-blocking hoặc delay ngắn)
        int holdTime = 0;
        while(digitalRead(PIN_BTN_CONFIG) == LOW && holdTime < 50) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            holdTime++;
        }
        if (holdTime >= 50) {
            Serial.println("\n[SYSTEM] BAT CHE DO CAU HINH WIFI (AP) DO NGUOI DUNG BAM NUT!");
            isLockedOut = false; // Mở khóa AP luôn
            failedAuthCount = 0;
            WiFi.disconnect(true);
            setupAP();
        }
    }
  }

  ElegantOTA.loop();

  // Quản lý Khóa AP 30 Phút
  if (isLockedOut && millis() - lockoutStartTime >= AP_LOCKOUT_MS) {
      isLockedOut = false;
      failedAuthCount = 0;
      Serial.println("[SECURITY] Hết 30 phút khóa AP. Mở khóa.");
  }

  // Quản lý WiFi Non-blocking
  if (!isApMode) {
    if (WiFi.status() == WL_CONNECTED) {
      isConnected = true;
      wifiLostFlag = false;
    } else {
      isConnected = false;

      // Ghi nhận thời điểm mất mạng lần đầu
      if (!wifiLostFlag) {
          wifiLostFlag = true;
          wifiLostTime = millis();
      }

      // Quản lý Reconnect Wi-Fi Chính / Phụ
      if (millis() - lastWiFiCheck > WIFI_TIMEOUT_MS) {
        lastWiFiCheck = millis();

        static bool trySecondary = false;

        Serial.println("[WIFI] Mat ket noi, dang thu lai...");
        WiFi.disconnect();

        // Luân phiên thử Wi-Fi 1 và Wi-Fi 2
        if (trySecondary && ssid2.length() > 0) {
            Serial.println("Thu ket noi Wi-Fi phu: " + ssid2);
            WiFi.begin(ssid2.c_str(), pass2.c_str());
        } else {
            WiFi.begin(ssid.c_str(), password.c_str());
        }
        trySecondary = !trySecondary;
      }

      // Kích hoạt AP tự động nếu mất mạng liên tục 5 phút
      if (wifiLostFlag && millis() - wifiLostTime >= 300000 && !isApMode) {
          Serial.println("[AP] Mat ket noi 5 phut, tu dong bat AP!");
          setupAP();
      }
    }
  } else {
      // Nếu đang trong AP Mode, gọi hàm quản lý chu kỳ 10/5
      checkAPCycle();
  }
}