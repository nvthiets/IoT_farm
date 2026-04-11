const ctx = document.getElementById('envChart').getContext('2d');
Chart.defaults.color = '#111';
Chart.defaults.font.weight = '600';
Chart.defaults.font.size = 13;

const envChart = new Chart(ctx, {
    type: 'line',
    data: {
        labels: [], 
        datasets: [
            { label: 'Nhiệt độ (°C)', data: [], borderColor: '#ff4757', backgroundColor: 'rgba(255, 71, 87, 0.25)', borderWidth: 3, pointRadius: 4, tension: 0.4, fill: true, yAxisID: 'y' },
            { label: 'Độ ẩm (%)', data: [], borderColor: '#1e90ff', backgroundColor: 'rgba(30, 144, 255, 0.25)', borderWidth: 3, pointRadius: 4, tension: 0.4, fill: true, yAxisID: 'y1' }
        ]
    },
    options: {
        responsive: true, maintainAspectRatio: false, interaction: { mode: 'index', intersect: false },
        scales: {
            x: { grid: { color: 'rgba(0,0,0,0.08)' } },
            y: { type: 'linear', display: true, position: 'left', min: 10, max: 50, ticks: { color: '#ff4757' }, grid: { color: 'rgba(0,0,0,0.08)' } },
            y1: { type: 'linear', display: true, position: 'right', min: 30, max: 100, ticks: { color: '#1e90ff' }, grid: { drawOnChartArea: false } }
        }
    }
});

// --- HÀM GHI LOG ---
function addLog(msg) {
    const box = document.getElementById('system-log');
    const time = new Date().toLocaleTimeString('vi-VN', {hour12: false});
    box.innerHTML += `<div style="margin-bottom: 6px; border-bottom: 1px dashed rgba(0,0,0,0.2); padding-bottom: 4px;"><span style="color:#000; font-weight:bold;">[${time}]</span> ${msg}</div>`;
    box.scrollTop = box.scrollHeight;
}

const mqttClient = mqtt.connect('ws://172.20.10.2:8000/mqtt');
let lastChartUpdateTime = 0; 

mqttClient.on('connect', function () {
    document.getElementById('mqtt-status').innerText = "(Đã kết nối WiFi)";
    document.getElementById('mqtt-status').style.color = "#20c997";
    addLog("✅ Đã kết nối thành công tới trạm MQTT HiveMQ.");
    
    mqttClient.subscribe('smartfarm/sensors');
    mqttClient.subscribe('smartfarm/status/relay1');
    mqttClient.subscribe('smartfarm/status/relay2');
    mqttClient.subscribe('smartfarm/status/mode');
    mqttClient.subscribe('smartfarm/status/buzzer');
    mqttClient.subscribe('smartfarm/status/threshold/temp');
    mqttClient.subscribe('smartfarm/status/threshold/soil');
});

mqttClient.on('error', function (error) { 
    addLog(`❌ Lỗi kết nối MQTT: ${error}`); 
});

mqttClient.on('message', function (topic, message) {
    const msgStr = message.toString();

    if (topic === 'smartfarm/sensors') {
        try {
            const data = JSON.parse(msgStr);
            
            document.getElementById('val-temp').innerText = data.temp.toFixed(1);
            document.getElementById('val-humi').innerText = data.hum.toFixed(1);
            document.getElementById('val-soil').innerText = data.soil;
            document.getElementById('val-light').innerText = data.light;

            document.getElementById('gauge-temp').style.background = `conic-gradient(#ff4757 0% ${(data.temp/50)*100}%, rgba(255,255,255,0.3) 0%)`;
            document.getElementById('gauge-humi').style.background = `conic-gradient(#1e90ff 0% ${data.hum}%, rgba(255,255,255,0.3) 0%)`;
            document.getElementById('gauge-soil').style.background = `conic-gradient(#20c997 0% ${data.soil}%, rgba(255,255,255,0.3) 0%)`;
            document.getElementById('gauge-light').style.background = `conic-gradient(#fca311 0% ${data.light}%, rgba(255,255,255,0.3) 0%)`;

            const currentTime = Date.now();
            if (currentTime - lastChartUpdateTime >= 60000) { 
                envChart.data.labels.push(data.time);
                envChart.data.datasets[0].data.push(data.temp);
                envChart.data.datasets[1].data.push(data.hum);

                if (envChart.data.labels.length > 20) {
                    envChart.data.labels.shift();
                    envChart.data.datasets[0].data.shift();
                    envChart.data.datasets[1].data.shift();
                }
                envChart.update();
                lastChartUpdateTime = currentTime;
            }
        } catch (e) { console.error("Lỗi parse JSON", e); }
    }

    if (topic === 'smartfarm/status/mode') {
        const isAuto = (msgStr === "AUTO");
        document.getElementById('mode-switch').checked = isAuto;
        
        const modeLabel = document.getElementById('mode-label');
        modeLabel.innerText = isAuto ? "TỰ ĐỘNG" : "THỦ CÔNG";
        modeLabel.style.color = isAuto ? "#05c46b" : "#222";
        
        document.querySelectorAll('.manual-control').forEach(ctrl => ctrl.disabled = isAuto);
        document.getElementById('manual-controls-container').style.opacity = isAuto ? "0.4" : "1";
        
        addLog(`[ESP] Chế độ hiện tại: <b>${msgStr}</b>`);
    }
    
    if (topic === 'smartfarm/status/relay1') document.getElementById('btn-relay1').checked = (msgStr === "ON");
    if (topic === 'smartfarm/status/relay2') document.getElementById('btn-relay2').checked = (msgStr === "ON");
    
    if (topic === 'smartfarm/status/buzzer') {
        if (msgStr === "ON") addLog("⚠️ <span style='color:red;'>CẢNH BÁO: Vượt ngưỡng! Đang bật còi...</span>");
        else addLog("✅ Còi đã tự động tắt.");
    }

    if (topic === 'smartfarm/status/threshold/temp') {
        document.getElementById('thresh-temp').value = msgStr;
        document.getElementById('thresh-temp-val').innerText = msgStr + '°C';
    }
    if (topic === 'smartfarm/status/threshold/soil') {
        document.getElementById('thresh-soil').value = msgStr;
        document.getElementById('thresh-soil-val').innerText = msgStr + '%';
    }
});

document.getElementById('mode-switch').addEventListener('change', function() {
    const isAuto = this.checked;
    const modeCmd = isAuto ? "AUTO" : "MANUAL";
    
    document.getElementById('mode-label').innerText = isAuto ? "TỰ ĐỘNG" : "THỦ CÔNG";
    document.getElementById('mode-label').style.color = isAuto ? "#05c46b" : "#222";
    document.querySelectorAll('.manual-control').forEach(ctrl => ctrl.disabled = isAuto);
    document.getElementById('manual-controls-container').style.opacity = isAuto ? "0.4" : "1";
    
    mqttClient.publish('smartfarm/control/mode', modeCmd);
    addLog(`[Web -> ESP] Lệnh đổi chế độ: <b>${modeCmd}</b>`);
});

document.getElementById('thresh-soil').addEventListener('change', function() { 
    mqttClient.publish('smartfarm/control/threshold/soil', this.value);
    addLog(`[Web -> ESP] Đã lưu ngưỡng Ẩm đất: ${this.value}%`);
});
document.getElementById('thresh-soil').addEventListener('input', function() { 
    document.getElementById('thresh-soil-val').innerText = this.value + '%'; 
});

document.getElementById('thresh-temp').addEventListener('change', function() { 
    mqttClient.publish('smartfarm/control/threshold/temp', this.value);
    addLog(`[Web -> ESP] Đã lưu ngưỡng Nhiệt độ: ${this.value}°C`);
});
document.getElementById('thresh-temp').addEventListener('input', function() { 
    document.getElementById('thresh-temp-val').innerText = this.value + '°C'; 
});

document.getElementById('btn-relay1').addEventListener('change', function() {
    const cmd = this.checked ? "ON" : "OFF";
    mqttClient.publish('smartfarm/control/relay1', cmd);
    addLog(`[Web -> ESP] Lệnh thủ công: Quạt -> ${cmd}`);
});

document.getElementById('btn-relay2').addEventListener('change', function() {
    const cmd = this.checked ? "ON" : "OFF";
    mqttClient.publish('smartfarm/control/relay2', cmd);
    addLog(`[Web -> ESP] Lệnh thủ công: Bơm -> ${cmd}`);
});