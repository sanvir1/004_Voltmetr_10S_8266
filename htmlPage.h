// htmlPage.h
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Мониторинг аккумуляторов</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            margin: 0;
            padding: 20px;
            background-color: #f5f5f5;
        }
        h1 {
            color: #333;
        }
        .battery-container {
            display: flex;
            flex-wrap: wrap;
            justify-content: center;
            gap: 20px;
            margin-top: 30px;
        }
        .battery {
            background-color: white;
            border-radius: 10px;
            padding: 15px;
            box-shadow: 0 4px 8px rgba(0,0,0,0.1);
            width: 220px;
            transition: all 0.3s ease;
            border: 3px solid #ddd;
        }
        .battery.normal {
            background-color: #e8f5e9;
            border-color: #27ae60;
        }
        .battery.low {
            background-color: #ffebee;
            border-color: #f39c12;
        }
        .battery.critical {
            background-color: #fff3e0;
            border-color: #e74c3c;
        }
        .battery.was-critical {
            border-color: #e74c3c !important;
        }
        .battery.was-low {
            border-color: #f39c12 !important;
        }
        .battery.updating {
            /*background-color: #fffde7;*/
        }
        .voltage {
            font-size: 24px;
            font-weight: bold;
            color: #2c3e50;
            margin: 10px 0;
        }
        .percentage {
            font-size: 20px;
            font-weight: bold;
            margin: 5px 0;
        }
        .battery-number {
            font-size: 18px;
            color: #7f8c8d;
        }
        .status {
            font-size: 16px;
            margin-top: 5px;
            font-weight: bold;
        }
        .voltage-range {
            font-size: 14px;
            margin-top: 5px;
            color: #555;
        }
        .normal {
            color: #27ae60;
        }
        .low {
            color: #e74c3c;
        }
        .high {
            color: #f39c12;
        }
        .update-info {
            margin-top: 20px;
            color: #7f8c8d;
            font-size: 14px;
        }
        .timestamp {
            font-size: 12px;
            color: #bdc3c7;
            margin-top: 5px;
        }
        .refresh-btn {
            margin: 20px;
            padding: 10px 20px;
            background-color: #3498db;
            color: white;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            font-size: 16px;
        }
        .refresh-btn:hover {
            background-color: #2980b9;
        }
    </style>
</head>
<body>
    <h1>Мониторинг напряжения аккумуляторов (10S)</h1>
    <p>Данные обновляются автоматически каждую секунду</p>
    
    <button class="refresh-btn" onclick="forceRefresh()">Обновить вручную</button>
    
    <div class="battery-container" id="batteries">
        <!-- Батарейки будут созданы скриптом -->
    </div>

    <div class="update-info" id="updateInfo">Ожидание первого обновления...</div>

    <script>
        // Создаем элементы для всех каналов
        function createBatteryElements() {
            const container = document.getElementById('batteries');
            container.innerHTML = '';
            
            for (let i = 1; i <= 13; i++) {
                container.innerHTML += `
                    <div class="battery" id="battery-${i-1}">
                        <div class="battery-number">Канал C${i}</div>
                        <div class="voltage" id="voltage-${i-1}"></div>
                        <div class="percentage">--%</div>
                        <div class="voltage-range" id="range-${i-1}">мин: --.-- В, макс: --.-- В</div>
                        <div class="status">Ожидание данных</div>
                        <div class="timestamp"></div>
                    </div>
                `;
            }
        }

        // Принудительное обновление всех каналов
        function forceRefresh() {
            document.getElementById('updateInfo').textContent = "Запущено ручное обновление...";
            updateAllBatteries();
        }

        // Обновление данных для одного канала
        function updateBattery(channel) {
            const batteryElement = document.getElementById(`battery-${channel}`);
            if (!batteryElement) return;
            
            batteryElement.classList.add('updating');
            
            fetch(`/data?channel=${channel}`)
                .then(response => {
                    if (!response.ok) throw new Error(`Ошибка HTTP: ${response.status}`);
                    return response.json();
                })
                .then(data => {
                    console.log(`Данные для C${channel+1}:`, data);
                    
                    const voltageElement = document.getElementById(`voltage-${channel}`);
                    const percentageElement = batteryElement.querySelector('.percentage');
                    const rangeElement = document.getElementById(`range-${channel}`);
                    const statusElement = batteryElement.querySelector('.status');
                    const timestampElement = batteryElement.querySelector('.timestamp');
                    
                    if (voltageElement) {
                        if (data.status === "Нет данных") {
                            voltageElement.textContent = "";
                        } else {
                            voltageElement.textContent = `${data.voltage.toFixed(2)} В`;
                        }
                    }
                    
                    if (percentageElement) percentageElement.textContent = `${data.percentage}%`;
                    if (rangeElement) rangeElement.textContent = `мин: ${data.minVoltage.toFixed(2)} В, макс: ${data.maxVoltage.toFixed(2)} В`;
                    if (statusElement) {
                        statusElement.textContent = data.status;
                        statusElement.className = 'status ' + getStatusClass(data.voltage);
                    }
                    if (timestampElement) {
                        const now = new Date();
                        timestampElement.textContent = `Обновлено: ${now.toLocaleTimeString()}`;
                    }
                    
                    // Обновляем класс подложки в зависимости от напряжения
                    let batteryClass = 'battery ' + getBackgroundClass(data.voltage);
                    if (data.wasCritical) {
                        batteryClass += ' was-critical';
                    } else if (data.wasLow) {
                        batteryClass += ' was-low';
                    }
                    batteryElement.className = batteryClass;
                    
                    updateLastRefreshTime();
                })
                .catch(error => {
                    console.error(`Ошибка при обновлении C${channel+1}:`, error);
                    const statusElement = batteryElement.querySelector('.status');
                    if (statusElement) {
                        statusElement.textContent = "Ошибка обновления";
                        statusElement.className = 'status low';
                    }
                })
                .finally(() => {
                    batteryElement.classList.remove('updating');
                });
        }

        function getStatusClass(voltage) {
            if (voltage < 0.5) return 'low';
            if (voltage < 3.2) return 'high';
            if (voltage > 4.2) return 'high';
            return 'normal';
        }

        function getBackgroundClass(voltage) {
            if (voltage < 1.0) return 'critical';
            if (voltage < 3.2) return 'low';
            if (voltage > 4.2) return 'low';
            return 'normal';
        }

        function updateLastRefreshTime() {
            const now = new Date();
            document.getElementById('updateInfo').textContent = 
                `Последнее обновление: ${now.toLocaleTimeString()}`;
        }

        // Обновление всех каналов с задержкой
        function updateAllBatteries() {
            for (let i = 0; i < 13; i++) {
                setTimeout(() => updateBattery(i), i * 200);
            }
        }

        // Автоматическое обновление
        function startAutoRefresh() {
            updateAllBatteries();
            setTimeout(startAutoRefresh, 1000); // Каждые 1 сек
        }

        // Инициализация при загрузке
        window.onload = function() {
            createBatteryElements();
            startAutoRefresh();
        };
    </script>
</body>
</html>
)rawliteral";