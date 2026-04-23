#ifndef WEBUI_H
#define WEBUI_H

#include <Arduino.h>

// Giao diện Web Portal cho MyDoor
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>MyDoor Config</title>
  <style>
    :root {
      --primary: #4CAF50;
      --primary-dark: #45a049;
      --bg: #f4f7f6;
      --card-bg: #ffffff;
      --text: #333333;
      --border: #dddddd;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; background-color: var(--bg); color: var(--text); line-height: 1.6; }
    .container { max-width: 600px; margin: 2rem auto; padding: 0 1rem; }
    .header { text-align: center; margin-bottom: 2rem; }
    .header h1 { color: var(--primary-dark); }
    .card { background: var(--card-bg); border-radius: 8px; box-shadow: 0 4px 6px rgba(0,0,0,0.1); margin-bottom: 1.5rem; overflow: hidden; }
    .card-header { background: var(--primary); color: white; padding: 1rem; font-weight: bold; font-size: 1.1rem; cursor: pointer; display: flex; justify-content: space-between; align-items: center; }
    .card-body { padding: 1.5rem; display: none; }
    .card.active .card-body { display: block; }
    .form-group { margin-bottom: 1rem; }
    .form-group label { display: block; margin-bottom: 0.5rem; font-weight: 600; }
    .form-control { width: 100%; padding: 0.75rem; border: 1px solid var(--border); border-radius: 4px; font-size: 1rem; }
    .btn { display: inline-block; background: var(--primary); color: white; padding: 0.75rem 1.5rem; border: none; border-radius: 4px; cursor: pointer; font-size: 1rem; text-align: center; width: 100%; transition: background 0.3s; }
    .btn:hover { background: var(--primary-dark); }
    .btn-secondary { background: #6c757d; }
    .btn-secondary:hover { background: #5a6268; }
    .btn-danger { background: #dc3545; }
    .btn-danger:hover { background: #c82333; }
    .btn-small { padding: 0.5rem 1rem; width: auto; margin-top: 0.5rem; }
    .network-list { max-height: 150px; overflow-y: auto; border: 1px solid var(--border); border-radius: 4px; margin-bottom: 1rem; }
    .network-item { padding: 0.5rem; border-bottom: 1px solid var(--border); cursor: pointer; display: flex; justify-content: space-between; }
    .network-item:last-child { border-bottom: none; }
    .network-item:hover { background-color: #f1f1f1; }
    .days-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(60px, 1fr)); gap: 0.5rem; }
    .day-checkbox { display: flex; align-items: center; }
    .day-checkbox input { margin-right: 0.5rem; }
    .time-inputs { display: flex; gap: 1rem; }
    .time-inputs .form-group { flex: 1; }
    .alert { padding: 1rem; border-radius: 4px; margin-bottom: 1rem; display: none; }
    .alert-success { background-color: #d4edda; color: #155724; border: 1px solid #c3e6cb; }
    .alert-danger { background-color: #f8d7da; color: #721c24; border: 1px solid #f5c6cb; }
    .spinner { display: none; width: 20px; height: 20px; border: 3px solid rgba(255,255,255,0.3); border-radius: 50%; border-top-color: white; animation: spin 1s ease-in-out infinite; margin-left: 10px; vertical-align: middle; }
    @keyframes spin { to { transform: rotate(360deg); } }
    .wifi-section { border: 1px solid #e0e0e0; padding: 1rem; border-radius: 6px; margin-bottom: 1.5rem; background: #fafafa; }
    .wifi-section h3 { margin-bottom: 1rem; font-size: 1.1rem; color: #555; }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>MyDoor Control</h1>
      <p>Cấu hình hệ thống cửa cuốn IoT</p>
    </div>

    <div id="alertBox" class="alert"></div>

    <!-- WiFi & Cloud Config -->
    <div class="card active" id="card-wifi">
      <div class="card-header" onclick="toggleCard('card-wifi')">
        <span>1. Cấu hình WiFi & Cloud</span>
        <span>▼</span>
      </div>
      <div class="card-body">
        <form id="wifiForm" onsubmit="saveWifi(event)">
          <div class="form-group">
            <label>Mạng WiFi xung quanh:</label>
            <button type="button" class="btn btn-secondary btn-small" onclick="scanWifi()" id="btnScan">
              Quét WiFi <div class="spinner" id="scanSpinner"></div>
            </button>
            <div id="networkList" class="network-list" style="display: none; margin-top: 10px;"></div>
          </div>

          <div class="wifi-section">
            <h3>Wi-Fi Chính</h3>
            <div class="form-group">
              <label for="ssid">Tên WiFi (SSID):</label>
              <input type="text" id="ssid" class="form-control" required>
            </div>
            <div class="form-group">
              <label for="password">Mật khẩu WiFi:</label>
              <input type="password" id="password" class="form-control">
            </div>
          </div>

          <div class="wifi-section">
            <h3>Wi-Fi Phụ (Dự phòng)</h3>
            <div class="form-group">
              <label for="ssid2">Tên WiFi (SSID):</label>
              <input type="text" id="ssid2" class="form-control" placeholder="Không bắt buộc">
            </div>
            <div class="form-group">
              <label for="pass2">Mật khẩu WiFi:</label>
              <input type="password" id="pass2" class="form-control">
            </div>
          </div>

          <hr style="margin: 1.5rem 0; border: 0; border-top: 1px solid var(--border);">
          <div class="form-group">
            <label for="blynk_tmpl">Blynk Template ID:</label>
            <input type="text" id="blynk_tmpl" class="form-control" placeholder="TMPLxxxxxx">
          </div>
          <div class="form-group">
            <label for="blynk_name">Blynk Device Name:</label>
            <input type="text" id="blynk_name" class="form-control" placeholder="MyDoor">
          </div>
          <div class="form-group">
            <label for="blynk_auth">Blynk Auth Token:</label>
            <input type="text" id="blynk_auth" class="form-control">
          </div>
          <button type="submit" class="btn">Lưu & Khởi động lại</button>
        </form>
      </div>
    </div>

    <!-- Hẹn giờ Nguồn Tổng -->
    <div class="card" id="card-schedule">
      <div class="card-header" onclick="toggleCard('card-schedule')">
        <span>2. Hẹn giờ Nguồn Tổng (Relay 4)</span>
        <span>▼</span>
      </div>
      <div class="card-body">
        <form id="scheduleForm" onsubmit="saveSchedule(event)">
          <div class="form-group">
            <label for="timezone">Múi giờ (UTC):</label>
            <input type="number" id="timezone" class="form-control" value="7" min="-12" max="14">
          </div>
          <div class="time-inputs">
            <div class="form-group">
              <label>Giờ BẬT (Sáng):</label>
              <div style="display: flex; gap: 0.5rem;">
                <input type="number" id="on_hour" class="form-control" placeholder="Giờ (0-23)" min="0" max="23" required>
                <input type="number" id="on_min" class="form-control" placeholder="Phút (0-59)" min="0" max="59" required>
              </div>
            </div>
            <div class="form-group">
              <label>Giờ TẮT (Tối):</label>
              <div style="display: flex; gap: 0.5rem;">
                <input type="number" id="off_hour" class="form-control" placeholder="Giờ (0-23)" min="0" max="23" required>
                <input type="number" id="off_min" class="form-control" placeholder="Phút (0-59)" min="0" max="59" required>
              </div>
            </div>
          </div>
          <div class="form-group">
            <label>Các ngày lặp lại:</label>
            <div class="days-grid">
              <label class="day-checkbox"><input type="checkbox" id="day_0" value="1"> CN</label>
              <label class="day-checkbox"><input type="checkbox" id="day_1" value="1"> T2</label>
              <label class="day-checkbox"><input type="checkbox" id="day_2" value="1"> T3</label>
              <label class="day-checkbox"><input type="checkbox" id="day_3" value="1"> T4</label>
              <label class="day-checkbox"><input type="checkbox" id="day_4" value="1"> T5</label>
              <label class="day-checkbox"><input type="checkbox" id="day_5" value="1"> T6</label>
              <label class="day-checkbox"><input type="checkbox" id="day_6" value="1"> T7</label>
            </div>
          </div>
          <button type="submit" class="btn btn-secondary">Lưu Lịch Trình</button>
        </form>
      </div>
    </div>

    <!-- Tài khoản Admin -->
    <div class="card" id="card-admin">
      <div class="card-header" onclick="toggleCard('card-admin')">
        <span>3. Đổi Mật Khẩu Admin</span>
        <span>▼</span>
      </div>
      <div class="card-body">
        <form id="adminForm" onsubmit="saveAdmin(event)">
          <div class="form-group">
            <label for="admin_user">Tên đăng nhập mới:</label>
            <input type="text" id="admin_user" class="form-control" required>
          </div>
          <div class="form-group">
            <label for="admin_pass">Mật khẩu mới:</label>
            <input type="password" id="admin_pass" class="form-control" required>
          </div>
          <button type="submit" class="btn btn-secondary">Lưu Tài Khoản</button>
        </form>
      </div>
    </div>

    <!-- Hệ thống -->
    <div class="card" id="card-system">
      <div class="card-header" onclick="toggleCard('card-system')">
        <span>4. Hệ Thống</span>
        <span>▼</span>
      </div>
      <div class="card-body" style="text-align: center;">
        <button type="button" class="btn btn-secondary" style="margin-bottom: 1rem;" onclick="window.location.href='/update'">Cập nhật Firmware (OTA)</button>
        <button type="button" class="btn btn-danger" onclick="reboot()">Khởi động lại ESP32</button>
      </div>
    </div>
  </div>

  <script>
    // Toggle accordion cards
    function toggleCard(id) {
      document.querySelectorAll('.card').forEach(card => {
        if(card.id === id) {
          card.classList.toggle('active');
        } else {
          card.classList.remove('active');
        }
      });
    }

    // Show alert messages
    function showAlert(msg, isSuccess) {
      const box = document.getElementById('alertBox');
      box.textContent = msg;
      box.className = 'alert ' + (isSuccess ? 'alert-success' : 'alert-danger');
      box.style.display = 'block';
      setTimeout(() => { box.style.display = 'none'; }, 5000);
    }

    // Load current config on start
    window.onload = function() {
      fetch('/get_config')
        .then(response => response.json())
        .then(data => {
          document.getElementById('blynk_tmpl').value = data.blynk_tmpl || '';
          document.getElementById('blynk_name').value = data.blynk_name || '';
          document.getElementById('blynk_auth').value = data.blynk_auth || '';

          document.getElementById('ssid2').value = data.ssid2 || '';

          document.getElementById('timezone').value = data.timezone;
          document.getElementById('on_hour').value = data.on_hour;
          document.getElementById('on_min').value = data.on_min;
          document.getElementById('off_hour').value = data.off_hour;
          document.getElementById('off_min').value = data.off_min;

          let days = data.days;
          for(let i=0; i<7; i++) {
            document.getElementById('day_' + i).checked = (days & (1 << i)) !== 0;
          }
        })
        .catch(err => console.error('Error loading config:', err));
    };

    let targetSsidField = 'ssid';

    // Scan WiFi
    function scanWifi() {
      const btn = document.getElementById('btnScan');
      const spinner = document.getElementById('scanSpinner');
      const list = document.getElementById('networkList');

      btn.disabled = true;
      spinner.style.display = 'inline-block';
      list.innerHTML = '<div style="padding: 10px; text-align: center;">Đang quét...</div>';
      list.style.display = 'block';

      fetch('/scan')
        .then(response => response.json())
        .then(networks => {
          list.innerHTML = '';
          if(networks.length === 0) {
            list.innerHTML = '<div style="padding: 10px; text-align: center;">Không tìm thấy mạng nào</div>';
          } else {
            networks.forEach(net => {
              const div = document.createElement('div');
              div.className = 'network-item';
              div.innerHTML = `<span>${net.ssid}</span> <span>${net.rssi} dBm</span>`;
              div.onclick = () => {
                document.getElementById(targetSsidField).value = net.ssid;
                document.getElementById(targetSsidField === 'ssid' ? 'password' : 'pass2').focus();
              };
              list.appendChild(div);
            });
            // Nút chuyển đổi chọn cho WiFi Chính/Phụ
            const switchTargetDiv = document.createElement('div');
            switchTargetDiv.innerHTML = `<button type="button" class="btn btn-secondary btn-small" onclick="targetSsidField = targetSsidField === 'ssid' ? 'ssid2' : 'ssid'; alert('Đang điền cho ' + (targetSsidField === 'ssid' ? 'WiFi Chính' : 'WiFi Phụ'));" style="width:100%; margin-top:10px;">Đổi mục tiêu điền (Đang chọn: \${targetSsidField === 'ssid' ? 'WiFi Chính' : 'WiFi Phụ'})</button>`;
            list.appendChild(switchTargetDiv);
          }
        })
        .catch(err => {
          list.innerHTML = '<div style="padding: 10px; text-align: center; color: red;">Lỗi khi quét mạng</div>';
        })
        .finally(() => {
          btn.disabled = false;
          spinner.style.display = 'none';
        });
    }

    // Save WiFi
    function saveWifi(e) {
      e.preventDefault();
      const formData = new URLSearchParams();
      formData.append('ssid', document.getElementById('ssid').value);
      formData.append('password', document.getElementById('password').value);
      formData.append('ssid2', document.getElementById('ssid2').value);
      formData.append('pass2', document.getElementById('pass2').value);
      formData.append('blynk_tmpl', document.getElementById('blynk_tmpl').value);
      formData.append('blynk_name', document.getElementById('blynk_name').value);
      formData.append('blynk_auth', document.getElementById('blynk_auth').value);

      fetch('/save_wifi', { method: 'POST', body: formData })
        .then(response => {
          if(response.ok) {
            showAlert('Lưu WiFi thành công! Thiết bị đang khởi động lại...', true);
          } else {
            showAlert('Lỗi khi lưu WiFi!', false);
          }
        });
    }

    // Save Schedule
    function saveSchedule(e) {
      e.preventDefault();
      const formData = new URLSearchParams();
      formData.append('timezone', document.getElementById('timezone').value);
      formData.append('on_hour', document.getElementById('on_hour').value);
      formData.append('on_min', document.getElementById('on_min').value);
      formData.append('off_hour', document.getElementById('off_hour').value);
      formData.append('off_min', document.getElementById('off_min').value);

      for(let i=0; i<7; i++) {
        if(document.getElementById('day_' + i).checked) {
          formData.append('day_' + i, '1');
        }
      }

      fetch('/save_schedule', { method: 'POST', body: formData })
        .then(response => {
          if(response.ok) showAlert('Lưu lịch trình thành công!', true);
          else showAlert('Lỗi khi lưu lịch trình!', false);
        });
    }

    // Save Admin
    function saveAdmin(e) {
      e.preventDefault();
      const formData = new URLSearchParams();
      formData.append('admin_user', document.getElementById('admin_user').value);
      formData.append('admin_pass', document.getElementById('admin_pass').value);

      fetch('/save_admin', { method: 'POST', body: formData })
        .then(response => {
          if(response.ok) showAlert('Lưu tài khoản Admin thành công! Hãy tải lại trang và đăng nhập lại.', true);
          else showAlert('Lỗi khi lưu tài khoản!', false);
        });
    }

    // Reboot
    function reboot() {
      if(confirm('Bạn có chắc chắn muốn khởi động lại thiết bị không?')) {
        fetch('/reboot', { method: 'POST' })
          .then(() => showAlert('Đang khởi động lại...', true));
      }
    }
  </script>
</body>
</html>
)rawliteral";

#endif // WEBUI_H
