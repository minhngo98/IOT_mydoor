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
                <input type="password" id="admin_pass" required placeholder="Ít nhất 6 ký tự" minlength="6" maxlength="64">
            </div>
            <div class="form-group">
                <label for="admin_pass_confirm">Nhập Lại Mật Khẩu</label>
                <input type="password" id="admin_pass_confirm" required placeholder="Nhập lại để xác nhận" minlength="6" maxlength="64">
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
                return;
            }

            if (pass.length < 6) {
                errorBox.textContent = "Mật khẩu phải có ít nhất 6 ký tự!";
                errorBox.style.display = 'block';
                return;
            }

            errorBox.style.display = 'none';
            btn.textContent = "Đang lưu...";
            btn.disabled = true;

            const formData = new URLSearchParams();
            formData.append('admin_user', user);
            formData.append('admin_pass', pass);

            fetch('/setup_first_boot', { method: 'POST', body: formData })
                .then(response => {
                    if (response.ok) {
                        alert("Thành công! Vui lòng đăng nhập lại với tài khoản vừa tạo.");
                        window.location.reload();
                    } else {
                        errorBox.textContent = "Có lỗi xảy ra, vui lòng thử lại!";
                        errorBox.style.display = 'block';
                        btn.textContent = "Tạo Tài Khoản Admin";
                        btn.disabled = false;
                    }
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
    <title>MyDoor Config - Bảng Điều Khiển</title>
    <style>
        :root { --primary: #0f172a; --bg: #f8fafc; --card: #ffffff; --border: #e2e8f0; --accent: #3b82f6; --text: #334155; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background: var(--bg); color: var(--text); padding: 20px; line-height: 1.5; }
        .container { max-width: 600px; margin: 0 auto; }
        h1 { text-align: center; color: var(--primary); margin-bottom: 30px; font-weight: 800; letter-spacing: -0.5px; }
        .card { background: var(--card); border-radius: 12px; padding: 25px; margin-bottom: 20px; box-shadow: 0 4px 6px -1px rgba(0, 0, 0, 0.05), 0 2px 4px -1px rgba(0, 0, 0, 0.03); border: 1px solid var(--border); }
        .card-header { font-size: 1.25rem; font-weight: 600; color: var(--primary); margin-bottom: 15px; border-bottom: 2px solid var(--border); padding-bottom: 10px; }
        .form-group { margin-bottom: 15px; }
        label { display: block; margin-bottom: 5px; font-weight: 500; font-size: 0.9rem; }
        input[type="text"], input[type="password"], select { width: 100%; padding: 10px; border: 1px solid var(--border); border-radius: 6px; font-size: 1rem; box-sizing: border-box; transition: border-color 0.2s; }
        input[type="text"]:focus, input[type="password"]:focus, select:focus { outline: none; border-color: var(--accent); box-shadow: 0 0 0 3px rgba(59, 130, 246, 0.1); }
        .btn { display: inline-block; background: var(--accent); color: white; padding: 10px 15px; border: none; border-radius: 6px; cursor: pointer; font-size: 1rem; font-weight: 600; text-align: center; transition: background 0.2s; width: 100%; }
        .btn:hover { background: #2563eb; }
        .btn-scan { background: #475569; margin-bottom: 10px; }
        .btn-scan:hover { background: #334155; }
        .btn-danger { background: #ef4444; }
        .btn-danger:hover { background: #dc2626; }
        .btn-warning { background: #f59e0b; margin-bottom: 10px; }
        .btn-warning:hover { background: #d97706; }
        .secret-list { border: 1px solid var(--border); border-radius: 6px; overflow: hidden; margin-bottom: 15px; }
        .secret-row { display: grid; grid-template-columns: 145px 1fr; gap: 10px; padding: 10px; border-bottom: 1px solid var(--border); align-items: center; }
        .secret-row:last-child { border-bottom: 0; }
        .secret-label { color: #64748b; font-size: 0.9rem; }
        .secret-value { font-family: ui-monospace, SFMono-Regular, Consolas, "Liberation Mono", monospace; word-break: break-all; }
        .time-flex { display: flex; gap: 10px; }
        .time-flex select { flex: 1; }
        .days-grid { display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; margin-top: 10px; }
        .day-checkbox { display: flex; align-items: center; gap: 5px; font-size: 0.9rem; }
        .status { padding: 10px; border-radius: 6px; margin-bottom: 15px; display: none; text-align: center; font-weight: 500; }
        .status.success { background: #dcfce7; color: #166534; display: block; }
        .status.error { background: #fee2e2; color: #991b1b; display: block; }
        .loader { display: inline-block; width: 16px; height: 16px; border: 2px solid #fff; border-bottom-color: transparent; border-radius: 50%; animation: rotation 1s linear infinite; margin-left: 10px; vertical-align: middle; }
        @keyframes rotation { 0% { transform: rotate(0deg); } 100% { transform: rotate(360deg); } }
    </style>
</head>
<body>
    <div class="container">
        <h1>⚙️ MyDoor Config</h1>

        <!-- TAB: MẠNG WIFI -->
        <div class="card">
            <div class="card-header">📡 Kết Nối WiFi & Cloud</div>
            <button type="button" class="btn btn-scan" id="btnScan" onclick="scanWifi()">Quét Mạng Xung Quanh</button>
            <div id="wifiStatus"></div>

            <form id="wifiForm" onsubmit="saveWifi(event)">
                <div class="form-group">
                    <label for="ssid">Tên Mạng (SSID)</label>
                    <select id="ssid" name="ssid" required>
                        <option value="">-- Bấm Quét Mạng để chọn --</option>
                    </select>
                </div>
                <div class="form-group">
                    <label for="password">Mật Khẩu WiFi</label>
                    <input type="password" id="password" name="password" placeholder="Nhập mật khẩu WiFi">
                </div>

                <hr style="border: 0; border-top: 1px dashed var(--border); margin: 20px 0;">
                <label style="font-weight: 600; color: var(--primary); margin-bottom: 10px; display: block;">Cấu Hình Blynk IoT</label>

                <div class="form-group">
                    <label for="blynk_tmpl">Blynk Template ID</label>
                    <input type="text" id="blynk_tmpl" name="blynk_tmpl" placeholder="VD: TMPLxxxxxx">
                </div>
                <div class="form-group">
                    <label for="blynk_name">Blynk Template Name</label>
                    <input type="text" id="blynk_name" name="blynk_name" placeholder="VD: MyDoor">
                </div>
                <div class="form-group">
                    <label for="blynk_auth">Blynk Auth Token</label>
                    <input type="text" id="blynk_auth" name="blynk_auth" placeholder="Chuỗi mã Token 32 ký tự">
                </div>

                <button type="submit" class="btn" id="btnSaveWifi">Lưu WiFi & Cloud</button>
            </form>
        </div>

        <!-- TAB: HẸN GIỜ NGUỒN TỔNG -->
        <div class="card">
            <div class="card-header">⏰ Hẹn Giờ Nguồn Tổng (Relay 4)</div>
            <form id="scheduleForm" onsubmit="saveSchedule(event)">
                <div class="form-group">
                    <label for="timezone">Múi Giờ (Timezone Thế Giới)</label>
                    <select id="timezone" name="timezone"></select>
                </div>
                <div class="form-group">
                    <label>Giờ Bật Nguồn (Ví dụ: Sáng dậy)</label>
                    <div class="time-flex">
                        <select id="on_hour" name="on_hour"></select>
                        <select id="on_min" name="on_min"></select>
                    </div>
                </div>
                <div class="form-group">
                    <label>Giờ Tắt Nguồn (Ví dụ: Đi ngủ)</label>
                    <div class="time-flex">
                        <select id="off_hour" name="off_hour"></select>
                        <select id="off_min" name="off_min"></select>
                    </div>
                </div>
                <div class="form-group">
                    <label>Ngày Áp Dụng trong tuần:</label>
                    <div class="days-grid">
                        <label class="day-checkbox"><input type="checkbox" name="day_1" value="1"> T2</label>
                        <label class="day-checkbox"><input type="checkbox" name="day_2" value="1"> T3</label>
                        <label class="day-checkbox"><input type="checkbox" name="day_3" value="1"> T4</label>
                        <label class="day-checkbox"><input type="checkbox" name="day_4" value="1"> T5</label>
                        <label class="day-checkbox"><input type="checkbox" name="day_5" value="1"> T6</label>
                        <label class="day-checkbox"><input type="checkbox" name="day_6" value="1"> T7</label>
                        <label class="day-checkbox"><input type="checkbox" name="day_0" value="1"> CN</label>
                    </div>
                </div>
                <button type="submit" class="btn" id="btnSaveSchedule">Lưu Hẹn Giờ</button>
            </form>
        </div>

        <!-- TAB: BẢO MẬT & HỆ THỐNG -->
        <div class="card">
            <div class="card-header">🔐 Quản Trị Hệ Thống</div>
            <form id="adminForm" onsubmit="saveAdmin(event)" style="margin-bottom: 20px;">
                <div class="form-group">
                    <label for="admin_user">Tài khoản Admin mới</label>
                    <input type="text" id="admin_user" name="admin_user" placeholder="VD: admin" required minlength="4" maxlength="32">
                </div>
                <div class="form-group">
                    <label for="admin_pass">Mật khẩu Admin mới</label>
                    <input type="password" id="admin_pass" name="admin_pass" placeholder="Nhập mật khẩu mới" required minlength="6" maxlength="64">
                </div>
                <button type="submit" class="btn">Đổi Thông Tin Đăng Nhập</button>
            </form>

            <hr style="border: 0; border-top: 1px solid var(--border); margin: 20px 0;">

            <div id="credentialStatus"></div>
            <div class="secret-list">
                <div class="secret-row">
                    <span class="secret-label">Rescue AP SSID</span>
                    <span class="secret-value" id="rescueSsid">-</span>
                </div>
                <div class="secret-row">
                    <span class="secret-label">Rescue AP Password</span>
                    <span class="secret-value" id="rescuePassMask">-</span>
                </div>
                <div class="secret-row">
                    <span class="secret-label">OTA User</span>
                    <span class="secret-value" id="otaUser">-</span>
                </div>
                <div class="secret-row">
                    <span class="secret-label">OTA Password</span>
                    <span class="secret-value" id="otaPassMask">-</span>
                </div>
            </div>
            <button type="button" class="btn btn-warning" id="btnRotateRescue" onclick="rotateCredential('/rotate_rescue_ap', 'btnRotateRescue', 'Xoay mat khau Rescue AP? Thiet bi dang o AP mode se khoi dong lai AP voi mat khau moi.')">Xoay Mật Khẩu Rescue AP</button>
            <button type="button" class="btn btn-warning" id="btnRotateOta" onclick="rotateCredential('/rotate_ota', 'btnRotateOta', 'Xoay mat khau OTA? Credential moi se khong hien thi lai tren giao dien.')">Xoay Mật Khẩu OTA</button>

            <hr style="border: 0; border-top: 1px solid var(--border); margin: 20px 0;">

            <a href="/update" style="text-decoration: none;"><button type="button" class="btn" style="background:#10b981; margin-bottom: 10px;">☁️ Nạp Firmware (OTA Update)</button></a>
            <button type="button" class="btn btn-danger" onclick="rebootESP()">🔄 Khởi Động Lại Hệ Thống</button>
        </div>
    </div>

    <script>
        // Khởi tạo các select box Giờ/Phút và Timezone
        function initSelects() {
            let tzHTML = '';
            for(let i=-12; i<=14; i++) {
                let sign = i >= 0 ? '+' : '';
                let label = i == 7 ? 'UTC +7 (Việt Nam, Thái Lan)' : `UTC ${sign}${i}`;
                tzHTML += `<option value="${i}" ${i==7 ? 'selected' : ''}>${label}</option>`;
            }
            document.getElementById('timezone').innerHTML = tzHTML;

            let hourHTML = '';
            for(let i=0; i<24; i++) hourHTML += `<option value="${i}">${i < 10 ? '0'+i : i}</option>`;
            document.getElementById('on_hour').innerHTML = hourHTML;
            document.getElementById('off_hour').innerHTML = hourHTML;

            let minHTML = '';
            for(let i=0; i<60; i++) minHTML += `<option value="${i}">${i < 10 ? '0'+i : i}</option>`;
            document.getElementById('on_min').innerHTML = minHTML;
            document.getElementById('off_min').innerHTML = minHTML;
        }
        initSelects();

        // Lấy config cũ từ ESP32 khi load trang
        async function loadDeviceConfig() {
            const data = await fetch('/get_config').then(r => r.json());
            document.getElementById('timezone').value = data.timezone !== undefined ? data.timezone : 7;
            document.getElementById('on_hour').value = data.on_hour;
            document.getElementById('on_min').value = data.on_min;
            document.getElementById('off_hour').value = data.off_hour;
            document.getElementById('off_min').value = data.off_min;
            document.getElementById('blynk_tmpl').value = data.blynk_tmpl || '';
            document.getElementById('blynk_name').value = data.blynk_name || '';
            document.getElementById('blynk_auth').value = data.blynk_auth || '';
            document.getElementById('rescueSsid').textContent = data.rescue_ssid || '-';
            document.getElementById('rescuePassMask').textContent = data.rescue_pass_mask || '-';
            document.getElementById('otaUser').textContent = data.ota_user || '-';
            document.getElementById('otaPassMask').textContent = data.ota_pass_mask || '-';

            for(let i=0; i<7; i++) {
                const day = document.querySelector(`input[name="day_${i}"]`);
                day.checked = !!(data.days && (data.days & (1 << i)));
            }
        }
        loadDeviceConfig();

        function showStatus(elemId, msg, isError = false) {
            const el = document.getElementById(elemId);
            el.className = `status ${isError ? 'error' : 'success'}`;
            el.innerHTML = msg;
            setTimeout(() => { el.style.display = 'none'; }, 3000);
        }

        async function scanWifi() {
            const btn = document.getElementById('btnScan');
            btn.innerHTML = 'Đang quét... <span class="loader"></span>';
            btn.disabled = true;
            try {
                const res = await fetch('/scan');
                const networks = await res.json();
                let html = '<option value="">-- Chọn Mạng WiFi --</option>';
                // Loại bỏ WiFi trùng tên và chọn sóng mạnh nhất
                const unique = {};
                networks.forEach(n => {
                    if(!unique[n.ssid] || unique[n.ssid].rssi < n.rssi) unique[n.ssid] = n;
                });

                Object.values(unique).sort((a,b) => b.rssi - a.rssi).forEach(n => {
                    const signal = n.rssi > -60 ? '📶 Rất mạnh' : (n.rssi > -80 ? '📶 Khá' : '📶 Yếu');
                    html += `<option value="${n.ssid}">${n.ssid} (${signal})</option>`;
                });
                document.getElementById('ssid').innerHTML = html;
                btn.innerHTML = 'Quét Lại';
            } catch(e) {
                alert('Lỗi khi quét WiFi!');
                btn.innerHTML = 'Quét Lại';
            }
            btn.disabled = false;
        }

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
        function saveAdmin(e) { submitForm(e, '/save_admin', e.target.querySelector('button').id = 'btnSaveAdmin'); }

        async function rotateCredential(url, btnId, message) {
            if(!confirm(message)) return;

            const btn = document.getElementById(btnId);
            const oldText = btn.innerHTML;
            btn.innerHTML = 'Đang xoay... <span class="loader"></span>';
            btn.disabled = true;

            try {
                const res = await fetch(url, { method: 'POST' });
                if(res.ok) {
                    showStatus('credentialStatus', 'Credential đã được xoay. Secret mới được lưu trong NVS và không hiển thị lại trên giao diện.');
                    await loadDeviceConfig();
                } else {
                    showStatus('credentialStatus', 'Không thể xoay credential.', true);
                }
            } catch(err) {
                showStatus('credentialStatus', 'Mất kết nối tới ESP32.', true);
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
