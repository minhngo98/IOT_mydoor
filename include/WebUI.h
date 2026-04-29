#ifndef WEBUI_H
#define WEBUI_H

#include <Arduino.h>

const char setup_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MyDoor - Thiết lập lần đầu</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: #f8fafc; color: #334155; padding: 20px; line-height: 1.5; display: flex; align-items: center; justify-content: center; min-height: 100vh; margin: 0; }
        .card { background: #ffffff; border-radius: 12px; padding: 30px; width: 100%; max-width: 400px; box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.05); border: 1px solid #e2e8f0; }
        h1 { text-align: center; color: #0f172a; margin-top: 0; }
        .desc { text-align: center; margin-bottom: 20px; font-size: 0.95rem; color: #64748b; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; font-weight: 500; font-size: 0.9rem; }
        input[type="text"], input[type="password"] { width: 100%; padding: 10px; border: 1px solid #e2e8f0; border-radius: 6px; font-size: 1rem; box-sizing: border-box; }
        input[type="text"]:focus, input[type="password"]:focus { outline: none; border-color: #3b82f6; box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.1); }
        .btn { display: inline-block; background: #3b82f6; color: white; padding: 12px; border: none; border-radius: 6px; cursor: pointer; font-size: 1rem; font-weight: 600; width: 100%; transition: background 0.2s; margin-top: 10px; }
        .btn:hover { background: #2563eb; }
        .alert { padding: 10px; border-radius: 6px; margin-bottom: 15px; display: none; text-align: center; font-size: 0.9rem; }
        .alert-error { background: #fee2e2; color: #991b1b; display: block; }
    </style>
</head>
<body>
    <div class="card">
        <h1>Bảo Mật MyDoor</h1>
        <p class="desc">Chào mừng bạn! Vui lòng tạo tài khoản Quản trị viên (Admin) cho lần đầu sử dụng. Tài khoản này dùng để đăng nhập vào trang cấu hình ở những lần sau.</p>
        <div id="errorBox" class="alert alert-error" style="display: none;"></div>
        <form onsubmit="setupAdmin(event)">
            <div class="form-group">
                <label for="admin_user">Tên Đăng Nhập</label>
                <input type="text" id="admin_user" required placeholder="Ví dụ: mydoor_admin" minlength="4" maxlength="32">
            </div>
            <div class="form-group">
                <label for="admin_pass">Mật Khẩu</label>
                <input type="password" id="admin_pass" required placeholder="Ít nhất 8 ký tự" minlength="8" maxlength="64">
            </div>
            <div class="form-group">
                <label for="admin_pass_confirm">Nhập Lại Mật Khẩu</label>
                <input type="password" id="admin_pass_confirm" required placeholder="Nhập lại để xác nhận" minlength="8" maxlength="64">
            </div>
            <button type="submit" class="btn" id="btnSubmit">Tạo Tài Khoản Admin</button>
        </form>
    </div>

    <script>
        function setupAdmin(e) {
            e.preventDefault();
            const user = document.getElementById('admin_user').value.trim();
            const pass = document.getElementById('admin_pass').value;
            const confirmPass = document.getElementById('admin_pass_confirm').value;
            const errorBox = document.getElementById('errorBox');
            const btn = document.getElementById('btnSubmit');

            if (pass !== confirmPass) {
                errorBox.textContent = "Mật khẩu không khớp. Vui lòng nhập lại!";
                errorBox.style.display = 'block';
                btn.textContent = 'Tạo Tài Khoản Admin';
                btn.disabled = false;
                return;
            }

            if (pass.length < 8) {
                errorBox.textContent = "Mật khẩu phải có ít nhất 8 ký tự!";
                errorBox.style.display = 'block';
                btn.textContent = 'Tạo Tài Khoản Admin';
                btn.disabled = false;
                return;
            }

            errorBox.style.display = 'none';
            btn.textContent = "Đang lưu...";
            btn.disabled = true;

            const fd = new FormData();
            fd.append('admin_user', user);
            fd.append('admin_pass', pass);

            fetch('/setup_first_boot', { method: 'POST', body: fd })
                .then(async res => {
                    if (!res.ok) {
                        const msg = await res.text();
                        throw new Error(msg || 'Không thể tạo tài khoản admin');
                    }
                    alert('Tạo tài khoản Admin thành công. Thiết bị sẽ chuyển sang trang đăng nhập cấu hình.');
                    window.location.href = '/';
                })
                .catch(err => {
                    errorBox.textContent = err.message || 'Có lỗi xảy ra khi lưu tài khoản Admin.';
                    errorBox.style.display = 'block';
                    btn.textContent = 'Tạo Tài Khoản Admin';
                    btn.disabled = false;
                });
        }
    </script>
</body>
</html>
)rawliteral";

const char* index_html = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MyDoor Config - Bảng Điều Khiển (MOCK PREVIEW)</title>
    <style>
        :root { --primary: #0f172a; --bg: #f8fafc; --card: #ffffff; --border: #e2e8f0; --accent: #3b82f6; --text: #334155; --tab-inactive: #94a3b8; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: var(--bg); color: var(--text); padding: 20px; line-height: 1.5; }
        .container { max-width: 600px; margin: 0 auto; }
        h1 { text-align: center; color: var(--primary); margin-bottom: 20px; font-weight: 800; letter-spacing: -0.5px; }

        /* Tabs Styling */
        .tabs { display: flex; gap: 5px; margin-bottom: -1px; overflow-x: auto; padding-bottom: 1px; }
        .tab-btn { background: #e2e8f0; color: var(--tab-inactive); border: 1px solid var(--border); border-bottom: none; padding: 12px 20px; cursor: pointer; border-radius: 8px 8px 0 0; font-weight: 600; font-size: 0.95rem; transition: all 0.2s; white-space: nowrap; }
        .tab-btn:hover { background: #cbd5e1; color: var(--text); }
        .tab-btn.active { background: var(--card); color: var(--accent); border-top: 3px solid var(--accent); border-bottom: 1px solid var(--card); z-index: 2; position: relative; }
        .tab-content { display: none; background: var(--card); border: 1px solid var(--border); border-radius: 0 8px 8px 8px; padding: 25px; box-shadow: 0 4px 6px -1px rgba(0,0,0,0.05); }
        .tab-content.active { display: block; }

        /* General Styling inside Tabs */
        .section-title { font-size: 1.15rem; font-weight: 600; color: var(--primary); margin-top: 0; margin-bottom: 15px; padding-bottom: 10px; border-bottom: 1px solid var(--border); }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; font-weight: 500; font-size: 0.9rem; }
        input[type="text"], input[type="password"], select { width: 100%; padding: 10px; border: 1px solid var(--border); border-radius: 6px; font-size: 1rem; box-sizing: border-box; transition: border-color 0.2s; }
        input[type="text"]:focus, input[type="password"]:focus, select:focus { outline: none; border-color: var(--accent); box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.1); }
        .btn { display: inline-block; background: var(--accent); color: white; padding: 10px 15px; border: none; border-radius: 6px; cursor: pointer; font-size: 1rem; font-weight: 600; text-align: center; transition: background 0.2s; width: 100%; }
        .btn:hover { background: #2563eb; }
        .btn-scan { background: #475569; margin-bottom: 15px; }
        .btn-scan:hover { background: #334155; }
        .btn-danger { background: #ef4444; }
        .btn-danger:hover { background: #dc2626; }
        .btn-warning { background: #f59e0b; margin-bottom: 10px; }
        .btn-warning:hover { background: #d97706; }

        /* Dashboard Control Grid */
        .control-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 25px; }
        .control-box { border: 1px solid var(--border); border-radius: 8px; padding: 15px; text-align: center; background: #f8fafc; display: flex; flex-direction: column; }
        .control-title { font-weight: 600; margin-bottom: 10px; color: var(--primary); }
        .status-badge { display: inline-block; padding: 4px 12px; border-radius: 20px; font-size: 0.85rem; font-weight: bold; margin-bottom: 15px; align-self: center; }
        .status-on { background: #dcfce7; color: #166534; }
        .status-off { background: #fee2e2; color: #991b1b; }
        .relay-btns { display: flex; gap: 8px; margin-top: auto; }
        .relay-btns .btn { padding: 8px; font-size: 0.9rem; flex: 1; margin: 0; box-sizing: border-box; }

        /* Other elements */
        .secret-list { border: 1px solid var(--border); border-radius: 6px; overflow: hidden; margin-bottom: 15px; }
        .secret-row { display: grid; grid-template-columns: 145px 1fr; gap: 10px; padding: 10px; border-bottom: 1px solid var(--border); align-items: center; }
        .secret-row:last-child { border-bottom: 0; }
        .secret-label { color: #64748b; font-size: 0.9rem; }
        .secret-value { font-family: monospace; word-break: break-all; }
        .time-flex { display: flex; gap: 10px; }
        .time-flex select { flex: 1; }
        .days-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; margin-top: 10px; }
        .day-checkbox { display: flex; align-items: center; gap: 5px; font-size: 0.9rem; }
        .preview-badge { position: fixed; top: 10px; right: 10px; background: #ef4444; color: white; padding: 5px 10px; border-radius: 4px; font-size: 0.8rem; font-weight: bold; z-index: 1000; box-shadow: 0 2px 4px rgba(0,0,0,0.2); }
        .loader { display: inline-block; width: 14px; height: 14px; border: 2px solid #fff; border-bottom-color: transparent; border-radius: 50%; animation: rotation 1s linear infinite; vertical-align: middle; }
        @keyframes rotation { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
        .status-msg { padding: 10px; border-radius: 6px; margin-bottom: 15px; display: none; text-align: center; font-weight: 500; }
        .status-msg.success { background: #dcfce7; color: #166534; display: block; }
        .status-msg.error { background: #fee2e2; color: #991b1b; display: block; }
    </style>
</head>
<body>
    
    <div class="container">
        <h1>⚙️ MyDoor Config</h1>

        <div class="tabs">
            <button class="tab-btn active" onclick="openTab(event, 'tab-dash')">Dashboard</button>
            <button class="tab-btn" onclick="openTab(event, 'tab-network')">Network & Cloud</button>
            <button class="tab-btn" onclick="openTab(event, 'tab-security')">Bảo mật & WiFi</button>
            <button class="tab-btn" onclick="openTab(event, 'tab-system')">Hệ thống</button>
        </div>

        <!-- TAB 1: DASHBOARD -->
        <div id="tab-dash" class="tab-content active">
            <h2 class="section-title">📊 Điều Khiển Trực Tiếp</h2>

            <div class="control-grid" style="grid-template-columns: 1fr;">
                <div class="control-box">
                    <div class="control-title">Nguồn Tổng (Relay 4)</div>
                    <span id="powerStatus" class="status-badge status-off">OFF</span>
                    <div class="relay-btns" style="justify-content: center;">
                        <button class="btn" style="background:#10b981" onclick="toggleRelay('power', true)">BẬT</button>
                        <button class="btn btn-danger" onclick="toggleRelay('power', false)">TẮT</button>
                    </div>
                </div>
            </div>

            <div class="control-grid">
                <div class="control-box">
                    <div class="control-title">Cửa Nhà (Relay 1-2-3)</div>
                    <span id="doorStatus" class="status-badge status-off">DỪNG</span>
                    <div class="relay-btns" style="justify-content: center;">
                        <button class="btn" style="background:#10b981" onclick="toggleRelay('door_up', true)">LÊN</button>
                        <button class="btn btn-warning" onclick="toggleRelay('door_stop', true)">DỪNG</button>
                        <button class="btn" style="background:#3b82f6" onclick="toggleRelay('door_down', true)">XUỐNG</button>
                    </div>
                </div>
                <div class="control-box">
                    <div class="control-title">Đèn Sáng (Relay 5)</div>
                    <span id="lightStatus" class="status-badge status-off">OFF</span>
                    <div class="relay-btns" style="justify-content: center;">
                        <button class="btn" style="background:#10b981" onclick="toggleRelay('light', true)">BẬT</button>
                        <button class="btn btn-danger" onclick="toggleRelay('light', false)">TẮT</button>
                    </div>
                </div>
            </div>

            <h2 class="section-title" style="margin-top: 30px;">📝 Terminal Logs</h2>
            <div style="background: #000; padding: 10px; border-radius: 6px; border: 1px solid #333;">
                <textarea id="terminalLog" readonly style="width: 100%; height: 150px; background: transparent; color: #0f0; border: none; font-family: monospace; font-size: 0.85em; resize: none; outline: none;">[12:00:00] ESP32 MOCK SERVER STARTED
[12:00:05] WiFi Connected (Mock)
[12:00:08] Blynk Cloud Ready (Mock)
[12:01:20] Nguon Box: BAT (WebUI)
[12:05:00] Den: BAT (Timer)
</textarea>
            </div>
            <button class="btn btn-scan" onclick="fetchLogsMock()" style="margin-top: 10px; margin-bottom: 20px;">Làm Mới Log</button>

            <h2 class="section-title" style="margin-top: 30px;">⏰ Cài Đặt Hẹn Giờ</h2>
            <form id="scheduleForm" onsubmit="saveSchedule(event)">
                <div class="form-group" style="display: flex; align-items: center; gap: 10px;">
                    <label for="timezone" style="margin: 0; white-space: nowrap;">Múi Giờ:</label>
                    <select id="timezone" name="timezone" style="width: auto;"></select>
                </div>

                <div style="background: #f1f5f9; padding: 10px; border-radius: 8px; margin-bottom: 15px; overflow-x: auto;">
                    <table style="width: 100%; border-collapse: collapse; font-size: 0.9rem; min-width: 400px;">
                        <tr>
                            <th style="text-align: left; padding: 8px; border: 1px solid var(--border); background: #e2e8f0;">Thiết bị</th>
                            <th style="text-align: center; padding: 8px; border: 1px solid var(--border); background: #e2e8f0;">Giờ Bật</th>
                            <th style="text-align: center; padding: 8px; border: 1px solid var(--border); background: #e2e8f0;">Giờ Tắt</th>
                        </tr>
                        <tr>
                            <td style="padding: 10px; font-weight: 500; border: 1px solid var(--border); background: #ffffff;">Nguồn (R4)</td>
                            <td style="padding: 10px; border: 1px solid var(--border); background: #ffffff; text-align: center;">
                                <div class="time-flex" style="justify-content: center;">
                                    <select id="onHour" name="onHour" style="padding: 5px; max-width: 70px;"></select>
                                    <select id="onMin" name="onMin" style="padding: 5px; max-width: 70px;"></select>
                                </div>
                            </td>
                            <td style="padding: 10px; border: 1px solid var(--border); background: #ffffff; text-align: center;">
                                <div class="time-flex" style="justify-content: center;">
                                    <select id="offHour" name="offHour" style="padding: 5px; max-width: 70px;"></select>
                                    <select id="offMin" name="offMin" style="padding: 5px; max-width: 70px;"></select>
                                </div>
                            </td>
                        </tr>
                        <tr>
                            <td style="padding: 10px; font-weight: 500; border: 1px solid var(--border); background: #ffffff;">Đèn (R5)</td>
                            <td style="padding: 10px; border: 1px solid var(--border); background: #ffffff; text-align: center;">
                                <div class="time-flex" style="justify-content: center;">
                                    <select id="lightOnHour" name="l_onHour" style="padding: 5px; max-width: 70px;"></select>
                                    <select id="lightOnMin" name="l_onMin" style="padding: 5px; max-width: 70px;"></select>
                                </div>
                            </td>
                            <td style="padding: 10px; border: 1px solid var(--border); background: #ffffff; text-align: center;">
                                <div class="time-flex" style="justify-content: center;">
                                    <select id="lightOffHour" name="l_offHour" style="padding: 5px; max-width: 70px;"></select>
                                    <select id="lightOffMin" name="l_offMin" style="padding: 5px; max-width: 70px;"></select>
                                </div>
                            </td>
                        </tr>
                    </table>
                </div>

                <div class="form-group">
                    <label>Ngày Áp Dụng trong tuần:</label>
                    <div class="days-grid">
                        <label class="day-checkbox"><input type="checkbox" name="day_1" value="1" checked> T2</label>
                        <label class="day-checkbox"><input type="checkbox" name="day_2" value="1" checked> T3</label>
                        <label class="day-checkbox"><input type="checkbox" name="day_3" value="1" checked> T4</label>
                        <label class="day-checkbox"><input type="checkbox" name="day_4" value="1" checked> T5</label>
                        <label class="day-checkbox"><input type="checkbox" name="day_5" value="1" checked> T6</label>
                        <label class="day-checkbox"><input type="checkbox" name="day_6" value="1" checked> T7</label>
                        <label class="day-checkbox"><input type="checkbox" name="day_0" value="1" checked> CN</label>
                    </div>
                </div>

                <div class="form-group">
                    <label>Ngày Áp Dụng cho đèn trong tuần:</label>
                    <div class="days-grid">
                        <label class="day-checkbox"><input type="checkbox" name="l_day_1" value="1" checked> T2</label>
                        <label class="day-checkbox"><input type="checkbox" name="l_day_2" value="1" checked> T3</label>
                        <label class="day-checkbox"><input type="checkbox" name="l_day_3" value="1" checked> T4</label>
                        <label class="day-checkbox"><input type="checkbox" name="l_day_4" value="1" checked> T5</label>
                        <label class="day-checkbox"><input type="checkbox" name="l_day_5" value="1" checked> T6</label>
                        <label class="day-checkbox"><input type="checkbox" name="l_day_6" value="1" checked> T7</label>
                        <label class="day-checkbox"><input type="checkbox" name="l_day_0" value="1" checked> CN</label>
                    </div>
                </div>
                <button type="submit" class="btn" id="btnSaveSchedule">Lưu Lịch Trình</button>
            </form>
        </div>

        <!-- TAB 2: NETWORK & CLOUD -->
        <div id="tab-network" class="tab-content">
            <h2 class="section-title">📡 Mạng WiFi Chính</h2>
            <form id="wifiForm" onsubmit="saveWifi(event)">
                <div style="overflow-x: auto; margin-bottom: 15px;">
                    <table style="width: 100%; border-collapse: collapse; text-align: left; min-width: 500px;">
                        <tr>
                            <th style="padding: 8px; border: 1px solid var(--border); background: #e2e8f0; width: 15%;"></th>
                            <th style="padding: 8px; border: 1px solid var(--border); background: #e2e8f0; width: 35%;">WiFi Ưu Tiên 1</th>
                            <th style="padding: 8px; border: 1px solid var(--border); background: #e2e8f0; width: 35%;">WiFi Ưu Tiên 2</th>
                            <th style="padding: 8px; border: 1px solid var(--border); background: #e2e8f0; text-align: center; width: 15%;">
                                <button type="button" class="btn btn-scan" id="btnScan" onclick="scanWifiMock()" style="margin: 0; padding: 6px 12px; font-size: 0.85rem;">Quét WiFi</button>
                            </th>
                        </tr>
                        <tr>
                            <td style="padding: 8px; font-weight: 500; border: 1px solid var(--border); background: #f8fafc;">SSID</td>
                            <td style="padding: 8px; border: 1px solid var(--border);">
                                <select id="ssid" name="ssid" style="padding: 8px; width: 100%; box-sizing: border-box;" required>
                                    <option value="">-- Chọn Mạng --</option>
                                    <option value="Viettel_5G_Home" selected>Viettel_5G_Home</option>
                                </select>
                            </td>
                            <td style="padding: 8px; border: 1px solid var(--border);">
                                <select id="ssid2" name="ssid2" style="padding: 8px; width: 100%; box-sizing: border-box;">
                                    <option value="">-- Chọn Mạng --</option>
                                    <option value="Wifi_HangXom">Wifi_HangXom</option>
                                </select>
                            </td>
                            <td style="padding: 8px; border: 1px solid var(--border); background: #f8fafc;"></td>
                        </tr>
                        <tr>
                            <td style="padding: 8px; font-weight: 500; border: 1px solid var(--border); background: #f8fafc;">Mật khẩu</td>
                            <td style="padding: 8px; border: 1px solid var(--border);">
                                <input type="password" id="password" name="password" placeholder="Mật khẩu 1" style="padding: 8px; width: 100%; box-sizing: border-box;">
                            </td>
                            <td style="padding: 8px; border: 1px solid var(--border);">
                                <input type="password" id="password2" name="pass2" placeholder="Mật khẩu 2" style="padding: 8px; width: 100%; box-sizing: border-box;">
                            </td>
                            <td style="padding: 8px; border: 1px solid var(--border); background: #f8fafc;"></td>
                        </tr>
                    </table>
                </div>

                <hr style="border: 0; border-top: 1px dashed var(--border); margin: 20px 0;">
                <h2 class="section-title" style="margin-top: 30px;">☁️ Cấu Hình Đám Mây (Cloud)</h2>

#ifdef USE_BLYNK
                <div id="blynkFields">
                    <div class="form-group">
                        <label for="blynkTemplate">Blynk Template ID</label>
                        <input type="text" id="blynkTemplate" name="blynkTemplate" placeholder="VD: TMPLxxxxxx">
                    </div>
                    <div class="form-group">
                        <label for="blynkName">Blynk Template Name</label>
                        <input type="text" id="blynkName" name="blynkName" placeholder="VD: MyDoor">
                    </div>
                    <div class="form-group">
                        <label for="blynkAuth">Blynk Auth Token</label>
                        <input type="text" id="blynkAuth" name="blynkAuth" placeholder="Chuỗi mã Token 32 ký tự">
                    </div>
                </div>
#endif
#ifdef USE_RAINMAKER
                <div id="rainmakerInfo" style="padding: 15px; background: #e0f2fe; border-left: 4px solid #3b82f6; border-radius: 4px; margin-bottom: 15px; font-size: 0.95rem;">
                    <strong>Đang chạy ESP RainMaker</strong><br>
                    Cấu hình WiFi phía trên sẽ được dùng làm dự phòng. Vui lòng sử dụng <b>App ESP RainMaker</b> trên điện thoại để Thêm thiết bị (Provisioning qua Bluetooth).
                </div>
#endif

                <button type="submit" class="btn" id="btnSaveWifi">Lưu WiFi & Cloud</button>
            </form>
        </div>

        <!-- TAB 3: ACCOUNT & SECURITY -->
        <div id="tab-security" class="tab-content">
            <h2 class="section-title">👤 Cập Nhật Tài Khoản Admin (WebUI)</h2>
            <form id="adminForm" onsubmit="saveAdmin(event)">
                <p style="font-size: 0.9rem; color: #64748b; margin-bottom: 15px;">Bạn có thể đổi tên đăng nhập riêng. Nếu để trống mật khẩu, hệ thống sẽ giữ nguyên mật khẩu Admin hiện tại.</p>
                <div class="form-group">
                    <label>Tài khoản Admin</label>
                    <input type="text" id="admin_user_new" name="admin_user" placeholder="VD: admin" required>
                </div>
                <div class="form-group">
                    <label>Mật khẩu Admin mới</label>
                    <input type="password" id="admin_pass_new" name="admin_pass" placeholder="Để trống nếu không muốn đổi mật khẩu">
                </div>
                <button type="submit" class="btn" id="btnSaveAdmin">Lưu Tài Khoản Admin</button>
            </form>

            <h2 class="section-title" style="margin-top: 30px;">🆘 Quản Lý WiFi Khẩn Cấp (Rescue AP) & OTA</h2>
            <p style="font-size: 0.9rem; color: #64748b; margin-bottom: 15px;">Mạng Rescue AP sẽ tự phát ra (IP 10.10.10.1) nếu thiết bị không bắt được Wifi nhà bạn trong vòng 5 phút. Hãy đặt mật khẩu thủ công để dễ nhớ.</p>
            <form id="rescueOtaForm" onsubmit="saveRescueOta(event)">
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 15px; margin-bottom: 15px;">
                    <div style="background: #f8fafc; padding: 15px; border-radius: 8px; border: 1px solid var(--border);">
                        <h3 style="font-size: 1rem; margin-top: 0; color: var(--primary);">Rescue AP</h3>
                        <div class="form-group">
                            <label>SSID (Tên mạng phát ra)</label>
                            <input type="text" id="rescue_ap_ssid" name="rescue_ap_ssid" required>
                        </div>
                        <div class="form-group">
                            <label>Mật khẩu Rescue AP</label>
                            <input type="password" id="rescue_ap_pass" name="rescue_ap_pass" placeholder="Nhập mật khẩu (để trống: không đổi)">
                        </div>
                    </div>

                    <div style="background: #f8fafc; padding: 15px; border-radius: 8px; border: 1px solid var(--border);">
                        <h3 style="font-size: 1rem; margin-top: 0; color: var(--primary);">Cập nhật OTA</h3>
                        <div class="form-group">
                            <label>Tài khoản OTA</label>
                            <input type="text" id="ota_user" name="ota_user" required>
                        </div>
                        <div class="form-group">
                            <label>Mật khẩu OTA</label>
                            <input type="password" id="ota_pass" name="ota_pass" placeholder="Nhập mật khẩu (để trống: không đổi)">
                        </div>
                    </div>
                </div>
                <button type="submit" class="btn" id="btnSaveRescueOta">Lưu Cấu Hình Rescue AP & OTA</button>
            </form>
        </div>

        <!-- TAB 4: SYSTEM -->
        <div id="tab-system" class="tab-content">
            <h2 class="section-title">⚙️ Quản Trị Vi Điều Khiển</h2>

            <div style="background: #fffbeb; border: 1px solid #fde68a; padding: 20px; border-radius: 8px; margin-bottom: 20px;">
                <h3 style="margin-top:0; color: #b45309;">☁️ Cập Nhật Firmware (OTA)</h3>
                <p style="font-size: 0.95rem; color: #78350f;">Cho phép nạp file `.bin` mới trực tiếp qua WiFi mà không cần cắm cáp USB.</p>
                <a href="/update" style="text-decoration: none;">
                    <button type="button" class="btn" style="background:#10b981;">Mở Trang Nạp Firmware (ElegantOTA)</button>
                </a>
            </div>

            <div style="background: #fef2f2; border: 1px solid #fecaca; padding: 20px; border-radius: 8px;">
                <h3 style="margin-top:0; color: #b91c1c;">⚠️ Khởi Động Lại</h3>
                <p style="font-size: 0.95rem; color: #7f1d1d;">Thiết bị sẽ đóng toàn bộ Relay, ngắt kết nối WiFi và khởi động lại vi xử lý ESP32.</p>
                <button type="button" class="btn btn-danger" onclick="rebootESP()">🔄 Khởi Động Lại Hệ Thống</button>
            </div>
        </div>
    </div>

    <script>
        function openTab(evt, tabId) {
            document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
            document.querySelectorAll('.tab-btn').forEach(el => el.classList.remove('active'));
            document.getElementById(tabId).classList.add('active');
            evt.currentTarget.classList.add('active');
        }

        function toggleRelay(type, turnOn) {
            if (type.startsWith('door')) {
                let cmd = type.replace('door_', '');
                fetch('/control', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: 'cmd=' + cmd
                });
            } else if (type === 'power') {
                fetch('/power', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: 'state=' + (turnOn ? '1' : '0')
                }).then(() => updateStatus());
            } else if (type === 'light') {
                fetch('/light', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                    body: 'state=' + (turnOn ? '1' : '0')
                }).then(() => updateStatus());
            }
        }

        async function fetchLogs() {
            try {
                const res = await fetch('/logs');
                const text = await res.text();
                const ta = document.getElementById('terminalLog');
                ta.value = text;
                ta.scrollTop = ta.scrollHeight;
            } catch(e) {}
        }

        function fetchLogsMock() {
            fetchLogs();
        }

        async function updateStatus() {
            try {
                const data = await fetch('/get_config').then(r => r.json());

                const pStatus = document.getElementById('powerStatus');
                pStatus.textContent = data.power_box_on ? 'ON' : 'OFF';
                pStatus.className = 'status-badge ' + (data.power_box_on ? 'status-on' : 'status-off');

                const lStatus = document.getElementById('lightStatus');
                if (lStatus) {
                    lStatus.textContent = data.light_on ? 'ON' : 'OFF';
                    lStatus.className = 'status-badge ' + (data.light_on ? 'status-on' : 'status-off');
                }

                fetchLogs();
            } catch (e) {}
        }
        setInterval(updateStatus, 3000);

        function initSelects() {
            let tzHTML = '';
            for(let i=-12; i<=14; i++) {
                let sign = i >= 0 ? '+' : '';
                let label = i == 7 ? 'UTC +7 (Việt Nam)' : `UTC ${sign}${i}`;
                tzHTML += `<option value="${i}">${label}</option>`;
            }
            document.getElementById('timezone').innerHTML = tzHTML;

            let hourHTML = '';
            for(let i=0; i<24; i++) hourHTML += `<option value="${i}">${i < 10 ? '0'+i : i}</option>`;
            ['onHour','offHour','lightOnHour','lightOffHour'].forEach(id => {
                if(document.getElementById(id)) document.getElementById(id).innerHTML = hourHTML;
            });

            let minHTML = '';
            for(let i=0; i<60; i++) minHTML += `<option value="${i}">${i < 10 ? '0'+i : i}</option>`;
            ['onMin','offMin','lightOnMin','lightOffMin'].forEach(id => {
                if(document.getElementById(id)) document.getElementById(id).innerHTML = minHTML;
            });
        }
        initSelects();

        async function loadDeviceConfig() {
            try {
                const data = await fetch('/get_config').then(r => r.json());
                if(data.timezone !== undefined) document.getElementById('timezone').value = data.timezone;
                if(data.onHour !== undefined) document.getElementById('onHour').value = data.onHour;
                if(data.onMin !== undefined) document.getElementById('onMin').value = data.onMin;
                if(data.offHour !== undefined) document.getElementById('offHour').value = data.offHour;
                if(data.offMin !== undefined) document.getElementById('offMin').value = data.offMin;

                if(data.l_onHour !== undefined) document.getElementById('lightOnHour').value = data.l_onHour;
                if(data.l_onMin !== undefined) document.getElementById('lightOnMin').value = data.l_onMin;
                if(data.l_offHour !== undefined) document.getElementById('lightOffHour').value = data.l_offHour;
                if(data.l_offMin !== undefined) document.getElementById('lightOffMin').value = data.l_offMin;

                if(document.getElementById('blynkTemplate') && data.blynkTemplate) document.getElementById('blynkTemplate').value = data.blynkTemplate;
                if(document.getElementById('blynkName') && data.blynkName) document.getElementById('blynkName').value = data.blynkName;
                if(document.getElementById('blynkAuth') && data.blynkAuth) document.getElementById('blynkAuth').value = data.blynkAuth;

                if(data.rescue_ssid) document.getElementById('rescue_ap_ssid').value = data.rescue_ssid;
                if(data.admin_user) {
                    document.getElementById('ota_user').value = data.admin_user;
                    document.getElementById('admin_user_new').value = data.admin_user;
                }

                for(let i=0; i<7; i++) {
                    const day = document.querySelector(`input[name="day_${i}"]`);
                    if (day) day.checked = !!(data.days && (data.days & (1 << i)));

                    const lightDay = document.querySelector(`input[name="l_day_${i}"]`);
                    if (lightDay) lightDay.checked = !!(data.lightScheduleDays && (data.lightScheduleDays & (1 << i)));
                }

                updateStatus();
            } catch(e) {}
        }
        loadDeviceConfig();

        async function scanWifi() {
            const btn = document.getElementById('btnScan');
            btn.innerHTML = 'Đang quét... <span class="loader"></span>';
            btn.disabled = true;
            try {
                const res = await fetch('/scan');
                const networks = await res.json();
                let html = '<option value="">-- Chọn Mạng WiFi --</option>';
                const unique = {};
                networks.forEach(n => {
                    if(!unique[n.ssid] || unique[n.ssid].rssi < n.rssi) unique[n.ssid] = n;
                });
                Object.values(unique).sort((a,b) => b.rssi - a.rssi).forEach(n => {
                    const signal = n.rssi > -60 ? '📶 Rất mạnh' : (n.rssi > -80 ? '📶 Khá' : '📶 Yếu');
                    html += `<option value="${n.ssid}">${n.ssid} (${signal})</option>`;
                });
                document.getElementById('ssid').innerHTML = html;
                document.getElementById('ssid2').innerHTML = html;
                btn.innerHTML = 'Quét Lại';
            } catch(e) {
                alert('Lỗi khi quét WiFi!');
                btn.innerHTML = 'Quét Lại';
            }
            btn.disabled = false;
        }

        function scanWifiMock() { scanWifi(); }

        async function submitForm(e, url, btnId) {
            e.preventDefault();
            const btn = document.getElementById(btnId);
            const oldText = btn.innerHTML;
            btn.innerHTML = 'Đang lưu... <span class="loader"></span>';
            btn.disabled = true;

            const fd = new FormData(e.target);
            try {
                const res = await fetch(url, { method: 'POST', body: fd });
                if(res.ok) alert('Lưu thành công!');
                else alert('Có lỗi xảy ra!');
            } catch(err) {
                alert('Mất kết nối tới ESP32!');
            }
            btn.innerHTML = oldText;
            btn.disabled = false;
        }

        function saveWifi(e) { submitForm(e, '/save_wifi', 'btnSaveWifi'); }
        function saveSchedule(e) { submitForm(e, '/save_schedule', 'btnSaveSchedule'); }
        function saveAdmin(e) { submitForm(e, '/save_admin', 'btnSaveAdmin'); }

        async function saveRescueOta(e) {
            e.preventDefault();
            const btn = document.getElementById('btnSaveRescueOta');
            const oldText = btn.innerHTML;
            btn.innerHTML = 'Đang lưu... <span class="loader"></span>';
            btn.disabled = true;

            const fd = new FormData(e.target);

            const rescueFd = new FormData();
            rescueFd.append('rescue_ap_ssid', fd.get('rescue_ap_ssid'));
            if(fd.get('rescue_ap_pass')) rescueFd.append('rescue_ap_pass', fd.get('rescue_ap_pass'));

            const adminFd = new FormData();
            adminFd.append('admin_user', fd.get('ota_user'));
            if(fd.get('ota_pass')) adminFd.append('admin_pass', fd.get('ota_pass'));

            try {
                let r1 = await fetch('/save_rescue_ap', { method: 'POST', body: rescueFd });
                let r2 = await fetch('/save_admin', { method: 'POST', body: adminFd });

                if (r1.ok && r2.ok) {
                    alert('Lưu cấu hình Rescue AP và OTA thành công!');
                } else {
                    alert('Có lỗi xảy ra khi lưu cấu hình.');
                }
            } catch(err) {
                alert('Mất kết nối tới ESP32!');
            }

            btn.innerHTML = oldText;
            btn.disabled = false;
        }

        async function rebootESP() {
            if(confirm('Bạn có chắc muốn Khởi động lại hệ thống?')) {
                await fetch('/reboot', { method: 'POST' });
                alert('Hệ thống đang khởi động lại. Vui lòng kết nối lại WiFi nhà bạn sau 10 giây.');
            }
        }
    </script>
</body>
</html>
)rawliteral";

#endif // WEBUI_H
