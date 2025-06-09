#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

// Конфигурация мультиплексора
const int muxS0 = D0;
const int muxS1 = D3;
const int muxS2 = D4;
const int muxS3 = D5;
const int muxOut = A0;

// Индивидуальные коэффициенты делителя напряжения для каждого канала
const float voltageDividerRatios[16] = {
  12.0,  // C0
  12.6,   // C1
  12.8,  // C2
  13.8,  // C3
  13.2,  // C4
  13.2,  // C5
  13.2,  // C6
  13.5,  // C7
  12.9,  // C8
  16.8,  // C9
  13.0,   // C10
  13.0,   // C11
  13.0,   // C12
  13.0,   // C13
  13.0,   // C14
  13.0    // C15
};

// Конфигурация бузера
const int buzzerPin1 = D6;
const int buzzerPin2 = D7;
const int buzzerTone = 1000; // Частота звука (Гц)
const int buzzerDuration = 200; // Длительность сигнала (мс)

// Настройки
const bool DEBUG_OUTPUT = false; // Флаг вывода отладочной информации в Serial
const unsigned long MEASUREMENT_INTERVAL = 1000; // Интервал измерений (мс)

ESP8266WebServer server(80);
DNSServer dnsServer;

const char* ssid = "Battery-Monitor";
const char* password = "";

struct BatteryData {
  float voltage;
  float minVoltage;
  float maxVoltage;
  bool wasCritical;
  bool wasLow;
  String status;
  int percentage;
  bool alarmTriggered;
};

BatteryData batteries[16];
float rawVoltages[16]; // Для хранения "сырых" напряжений
unsigned long lastMeasurementTime = 0;

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
            background-color: #fffde7;
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
    <h1>Мониторинг напряжения аккумуляторов</h1>
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
            
            for (let i = 1; i <= 16; i++) {
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
                            voltageElement.textContent = `${data.voltage.toFixed(1)} В`;
                        }
                    }
                    
                    if (percentageElement) percentageElement.textContent = `${data.percentage}%`;
                    if (rangeElement) rangeElement.textContent = `мин: ${data.minVoltage.toFixed(1)} В, макс: ${data.maxVoltage.toFixed(1)} В`;
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
            for (let i = 0; i < 16; i++) {
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

float readBatteryVoltage(int channel) {
    digitalWrite(muxS0, bitRead(channel, 0));
    digitalWrite(muxS1, bitRead(channel, 1));
    digitalWrite(muxS2, bitRead(channel, 2));
    digitalWrite(muxS3, bitRead(channel, 3));
    delay(2);
    
    // Усреднение 3 измерений
    int sum = 0;
    for (int i = 0; i < 3; i++) {
        sum += analogRead(muxOut);
        delay(5);
    }
    int avg = sum / 3;
    return (avg / 1023.0) * 3.3 * voltageDividerRatios[channel];
}

String getBatteryStatus(float voltage) {
    if (voltage < 0.5) return "Нет данных";
    if (voltage < 2.3) return "Критический разряд";
    if (voltage < 3.2) return "Низкий заряд";
    if (voltage > 4.2) return "Перезаряд";
    return "Норма";
}

int calculatePercentage(float voltage) {
    // Для Li-ion аккумулятора (примерные значения)
    if (voltage >= 4.2) return 100;
    if (voltage <= 3.0) return 0;
    return (int)((voltage - 3.0) / (4.2 - 3.0) * 100);
}

void playAlarmTone() {
    for (int i = 0; i < 3; i++) {
        tone(buzzerPin1, buzzerTone, buzzerDuration);
        tone(buzzerPin2, buzzerTone, buzzerDuration);
        delay(buzzerDuration + 100);
    }
}

void updateBatteryData(int channel) {
    digitalWrite(muxS0, bitRead(channel, 0));
    digitalWrite(muxS1, bitRead(channel, 1));
    digitalWrite(muxS2, bitRead(channel, 2));
    digitalWrite(muxS3, bitRead(channel, 3));
    delay(2);
    
    // Усреднение 3 измерений
    int sum = 0;
    int rawReadings[3];
    for (int i = 0; i < 3; i++) {
        rawReadings[i] = analogRead(muxOut);
        sum += rawReadings[i];
        delay(5);
    }
    int avgRaw = sum / 3;
    
    // Если среднее значение меньше 15, считаем напряжение 0
    float voltage = 0;
    if (avgRaw >= 15) {
        voltage = (avgRaw / 1023.0) * 3.3 * voltageDividerRatios[channel];
    }
    rawVoltages[channel] = voltage; // Сохраняем сырое напряжение
    
    // Вывод сырых данных в консоль
    if (DEBUG_OUTPUT) {
        Serial.printf("C%d: Raw ADC=[%d, %d, %d], Avg=%d -> ", 
                     channel+1, rawReadings[0], rawReadings[1], rawReadings[2], avgRaw);
        if (avgRaw < 15) {
            Serial.print("Ignored (Avg<15), ");
        }
    }
    
    // Для канала C0 используем напряжение как есть
    // Для каналов C1-C15 используем разницу напряжений
    float displayVoltage = voltage;
    if (channel > 0) {
        displayVoltage = voltage - rawVoltages[channel-1];
        // Если предыдущее напряжение было 0, используем текущее как есть
        if (rawVoltages[channel-1] < 0.1) {
            displayVoltage = voltage;
        }
    }
    
    // Округляем до десятых
    displayVoltage = round(displayVoltage * 10) / 10.0;
    voltage = round(voltage * 10) / 10.0;
    
    // Обновляем минимальное и максимальное напряжение (для отображаемого значения)
    if (voltage > 0.1) { // Игнорируем явно ошибочные значения
        if (batteries[channel].minVoltage == 0 || displayVoltage < batteries[channel].minVoltage) {
            batteries[channel].minVoltage = displayVoltage;
        }
        if (displayVoltage > batteries[channel].maxVoltage) {
            batteries[channel].maxVoltage = displayVoltage;
        }
    }
    
    // Проверяем на критический разряд (используем отображаемое напряжение)
    if (displayVoltage < 1.0 && !batteries[channel].alarmTriggered) {
        batteries[channel].wasCritical = true;
        batteries[channel].alarmTriggered = true;
        playAlarmTone();
    }
    // Проверяем на низкий заряд
    else if (displayVoltage < 3.2 && displayVoltage >= 1.0 && !batteries[channel].alarmTriggered) {
        batteries[channel].wasLow = true;
        batteries[channel].alarmTriggered = true;
        playAlarmTone();
    }
    // Если напряжение вернулось в норму, сбрасываем флаг тревоги
    else if (displayVoltage >= 3.2 && displayVoltage <= 4.2) {
        batteries[channel].alarmTriggered = false;
    }
    
    batteries[channel].voltage = displayVoltage;
    batteries[channel].status = getBatteryStatus(displayVoltage);
    batteries[channel].percentage = calculatePercentage(displayVoltage);
    
    if (DEBUG_OUTPUT) {
        if (avgRaw >= 15) {
            Serial.printf("RawV=%.1fV (DivRatio=%.1f), DisplayV=%.1fV (%d%%) (min: %.1fV, max: %.1fV) - %s\n", 
                         voltage, voltageDividerRatios[channel], displayVoltage,
                         batteries[channel].percentage,
                         batteries[channel].minVoltage, 
                         batteries[channel].maxVoltage, 
                         batteries[channel].status.c_str());
        } else {
            Serial.printf("DisplayV=0.0V (0%%) (min: %.1fV, max: %.1fV) - Нет данных\n",
                         batteries[channel].minVoltage,
                         batteries[channel].maxVoltage);
        }
    }
}

void measureAllBatteries() {
    for (int i = 0; i < 16; i++) {
        updateBatteryData(i);
    }
}

void setup() {
    if (DEBUG_OUTPUT) {
        Serial.begin(115200);
        delay(100);
    }
    
    // Настройка мультиплексора
    pinMode(muxS0, OUTPUT);
    pinMode(muxS1, OUTPUT);
    pinMode(muxS2, OUTPUT);
    pinMode(muxS3, OUTPUT);
    pinMode(muxOut, INPUT);
    
    // Настройка бузера
    pinMode(buzzerPin1, OUTPUT);
    pinMode(buzzerPin2, OUTPUT);
    digitalWrite(buzzerPin1, LOW);
    digitalWrite(buzzerPin2, LOW);
    
    // Инициализация данных
    for (int i = 0; i < 16; i++) {
        batteries[i] = {0.0, 0.0, 0.0, false, false, "Инициализация", 0, false};
        rawVoltages[i] = 0.0;
    }
    
    WiFi.softAP(ssid, password);
    if (DEBUG_OUTPUT) {
        Serial.println("AP started");
        Serial.print("IP: ");
        Serial.println(WiFi.softAPIP());
    }
    
    dnsServer.start(53, "*", WiFi.softAPIP());
    
    server.on("/", []() {
        server.send(200, "text/html", htmlPage);
    });
    
    server.on("/data", []() {
        if (!server.hasArg("channel")) {
            server.send(400, "text/plain", "Missing channel parameter");
            return;
        }
        
        int channel = server.arg("channel").toInt();
        if (channel < 0 || channel >= 16) {
            server.send(400, "text/plain", "Invalid channel");
            return;
        }
        
        DynamicJsonDocument doc(256);
        doc["channel"] = channel;
        doc["voltage"] = batteries[channel].voltage;
        doc["minVoltage"] = batteries[channel].minVoltage;
        doc["maxVoltage"] = batteries[channel].maxVoltage;
        doc["wasCritical"] = batteries[channel].wasCritical;
        doc["wasLow"] = batteries[channel].wasLow;
        doc["status"] = batteries[channel].status;
        doc["percentage"] = batteries[channel].percentage;
        
        String json;
        serializeJson(doc, json);
        
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "application/json", json);
    });
    
    server.on("/redirect", []() {
        server.sendHeader("Location", "http://battery-monitor.local");
        server.send(302, "text/plain", "");
    });

    server.onNotFound([]() {
        String host = server.hostHeader();
        if (host != "battery-monitor.local" && host != WiFi.softAPIP().toString()) {
            server.sendHeader("Location", String("http://battery-monitor.local"), true);
            server.send(302, "text/plain", "");
        } else {
            server.send(404, "text/plain", "File Not Found");
        }
    });
    
    server.begin();
    if (DEBUG_OUTPUT) {
        Serial.println("HTTP server started");
    }
    
    // Первое измерение
    measureAllBatteries();
    lastMeasurementTime = millis();
}

void loop() {
    dnsServer.processNextRequest();
    server.handleClient();
    
    // Периодическое измерение напряжения независимо от запросов
    unsigned long currentTime = millis();
    if (currentTime - lastMeasurementTime >= MEASUREMENT_INTERVAL) {
        measureAllBatteries();
        lastMeasurementTime = currentTime;
    }
}