#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// Настройки
const bool DEBUG_OUTPUT = false;
const unsigned long MEASUREMENT_INTERVAL = 1000;
const int NUM_BATTERIES = 10;
const int DISPLAY_PAGE_SIZE = 5;

// Конфигурация мультиплексора
const int muxS0 = D8;
const int muxS1 = D7; 
const int muxS2 = D6;
const int muxS3 = D5;
const int muxOut = A0;

// Конфигурация бузера
const int buzzerPin1 = D3;
const int buzzerPin2 = D4;
const int buzzerTone = 1000;
const int buzzerDuration = 200;

// Конфигурация кнопок
const int pageButtonPin = D0; // Кнопка переключения страниц
const int modeButtonPin = D3; // Кнопка переключения режима отображения

// Конфигурация дисплея
LiquidCrystal_I2C lcd(0x27, 16, 2);

const char* ssid = "Battery-Monitor";
const char* password = "";

// Коэффициенты делителя напряжения
const float voltageDividerRatios[10] = {
  12.0, 12.6, 12.8, 13.8, 13.2, 
  13.2, 13.2, 13.5, 12.9, 16.6
};

ESP8266WebServer server(80);
DNSServer dnsServer;

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

BatteryData batteries[NUM_BATTERIES];
float rawVoltages[NUM_BATTERIES];
unsigned long lastMeasurementTime = 0;
bool displayInitialized = false;

// Переменные для управления отображением
int currentPage = 0;
bool showPercentage = false; // false - показывать напряжение, true - проценты

// Буферы для дисплея
char line0[17] = {0};
char line1[17] = {0};
char newLine0[17] = {0};
char newLine1[17] = {0};

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
            
            for (let i = 1; i <= 10; i++) {
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
            for (let i = 0; i < 10; i++) {
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

void setup() {
    if (DEBUG_OUTPUT) {
        Serial.begin(115200);
        delay(100);
    }
    
    // Инициализация пинов
    pinMode(muxS0, OUTPUT);
    pinMode(muxS1, OUTPUT);
    pinMode(muxS2, OUTPUT);
    pinMode(muxS3, OUTPUT);
    pinMode(muxOut, INPUT);
    
    pinMode(buzzerPin1, OUTPUT);
    pinMode(buzzerPin2, OUTPUT);
    digitalWrite(buzzerPin1, LOW);
    digitalWrite(buzzerPin2, LOW);
    
    pinMode(pageButtonPin, INPUT_PULLUP);
    pinMode(modeButtonPin, INPUT_PULLUP);
    
    // Инициализация данных батарей
    for (int i = 0; i < NUM_BATTERIES; i++) {
        batteries[i] = {0.0, 0.0, 0.0, false, false, "Инициализация", 0, false};
        rawVoltages[i] = 0.0;
    }
    
    // Инициализация дисплея
    Wire.begin(D2, D1);
    lcd.init();
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Voltmetr 10S");
    lcd.setCursor(0, 1);
    lcd.print("v2.3");
    delay(2000);
    displayInitialized = true;
    currentPage = 0; // Начинаем с первой страницы
    updateDisplay();
    
    // Настройка WiFi
    WiFi.softAP(ssid, password);
    
    // Настройка DNS и HTTP сервера
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
        if (channel < 0 || channel >= NUM_BATTERIES) {
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
    
    // Первое измерение
    measureAllBatteries();
    lastMeasurementTime = millis();
}

void loop() {
    dnsServer.processNextRequest();
    server.handleClient();
    checkButtons();
    
    unsigned long currentTime = millis();
    if (currentTime - lastMeasurementTime >= MEASUREMENT_INTERVAL) {
        measureAllBatteries();
        updateDisplay();
        lastMeasurementTime = currentTime;
    }
}

void updateBatteryData(int channel) {
    digitalWrite(muxS0, bitRead(channel, 0));
    digitalWrite(muxS1, bitRead(channel, 1));
    digitalWrite(muxS2, bitRead(channel, 2));
    digitalWrite(muxS3, bitRead(channel, 3));
    delay(2);
    
    int sum = 0;
    int rawReadings[3];
    for (int i = 0; i < 3; i++) {
        rawReadings[i] = analogRead(muxOut);
        sum += rawReadings[i];
        delay(5);
    }
    int avgRaw = sum / 3;
    
    float voltage = 0;
    if (avgRaw >= 15) {
        voltage = (avgRaw / 1023.0) * 3.3 * voltageDividerRatios[channel];
    }
    rawVoltages[channel] = voltage;
    
    float displayVoltage = voltage;
    if (channel > 0) {
        displayVoltage = voltage - rawVoltages[channel-1];
        if (rawVoltages[channel-1] < 0.1) {
            displayVoltage = voltage;
        }
    }
    
    displayVoltage = round(displayVoltage * 10) / 10.0;
    voltage = round(voltage * 10) / 10.0;
    
    if (voltage > 0.1) {
        if (batteries[channel].minVoltage == 0 || displayVoltage < batteries[channel].minVoltage) {
            batteries[channel].minVoltage = displayVoltage;
        }
        if (displayVoltage > batteries[channel].maxVoltage) {
            batteries[channel].maxVoltage = displayVoltage;
        }
    }
    
    if (displayVoltage < 1.0 && !batteries[channel].alarmTriggered) {
        batteries[channel].wasCritical = true;
        batteries[channel].alarmTriggered = true;
        playAlarmTone();
    }
    else if (displayVoltage < 3.2 && displayVoltage >= 1.0 && !batteries[channel].alarmTriggered) {
        batteries[channel].wasLow = true;
        batteries[channel].alarmTriggered = true;
        playAlarmTone();
    }
    else if (displayVoltage >= 3.2 && displayVoltage <= 4.2) {
        batteries[channel].alarmTriggered = false;
    }
    
    batteries[channel].voltage = displayVoltage;
    batteries[channel].status = getBatteryStatus(displayVoltage);
    batteries[channel].percentage = calculatePercentage(displayVoltage);
}

void measureAllBatteries() {
    for (int i = 0; i < NUM_BATTERIES; i++) {
        updateBatteryData(i);
    }
}

void updateDisplay() {
    if (!displayInitialized) return;
    
    memset(newLine0, ' ', 16);
    memset(newLine1, ' ', 16);
    newLine0[16] = '\0';
    newLine1[16] = '\0';
    
    int startBattery = currentPage * DISPLAY_PAGE_SIZE;
    int endBattery = min(startBattery + DISPLAY_PAGE_SIZE, NUM_BATTERIES);
    
    // Формируем первую строку (первые 3 аккумулятора)
    int pos = 0;
    for (int i = startBattery; i < min(startBattery + 3, endBattery); i++) {
        if (i > startBattery) {
            newLine0[pos++] = ' ';
        }
        
        if (showPercentage) {
            // Показываем проценты
            snprintf(&newLine0[pos], 5, "%d%%", batteries[i].percentage);
            pos += strlen(&newLine0[pos]);
        } else {
            // Показываем напряжение
            dtostrf(batteries[i].voltage, 3, 1, &newLine0[pos]);
            pos += 3;
            newLine0[pos++] = 'v';
        }
    }
    
    // Формируем вторую строку (оставшиеся 2 аккумулятора + номер страницы)
    pos = 0;
    for (int i = startBattery + 3; i < endBattery; i++) {
        if (i > startBattery + 3) {
            newLine1[pos++] = ' ';
        }
        
        if (showPercentage) {
            // Показываем проценты
            snprintf(&newLine1[pos], 5, "%d%%", batteries[i].percentage);
            pos += strlen(&newLine1[pos]);
        } else {
            // Показываем напряжение
            dtostrf(batteries[i].voltage, 3, 1, &newLine1[pos]);
            pos += 3;
            newLine1[pos++] = 'v';
        }
    }
    
    // Добавляем номер страницы в конец второй строки
    int pagePos = 12; // Позиция для номера страницы
    if (showPercentage) {
        // В режиме процентов смещаем немного левее
        pagePos = 12;
        // Очищаем область перед номером страницы
        for (int i = pos; i < pagePos; i++) {
            newLine1[i] = ' ';
        }
    }
    newLine1[pagePos] = 'P';
    newLine1[pagePos+1] = '1' + currentPage;
    
    // Обновляем дисплей только если содержимое изменилось
    if (strcmp(line0, newLine0) != 0) {
        lcd.setCursor(0, 0);
        lcd.print(newLine0);
        strcpy(line0, newLine0);
    }
    
    if (strcmp(line1, newLine1) != 0) {
        lcd.setCursor(0, 1);
        lcd.print(newLine1);
        strcpy(line1, newLine1);
    }
}

void checkButtons() {
    static unsigned long lastPageDebounceTime = 0;
    static unsigned long lastModeDebounceTime = 0;
    static int lastPageButtonState = LOW;
    static int lastModeButtonState = LOW;
    static int pageButtonState = LOW;
    static int modeButtonState = LOW;
    
    // Обработка кнопки переключения страниц (D0)
    int pageReading = digitalRead(pageButtonPin);
    if (pageReading != lastPageButtonState) {
        lastPageDebounceTime = millis();
    }
    
    if ((millis() - lastPageDebounceTime) > 50) {
        if (pageReading != pageButtonState) {
            pageButtonState = pageReading;
            
            if (pageButtonState == HIGH) {
                currentPage = (currentPage + 1) % ((NUM_BATTERIES + DISPLAY_PAGE_SIZE - 1) / DISPLAY_PAGE_SIZE);
                lcd.clear(); // Стираю старые данные
                updateDisplay();
            }
        }
    }
    lastPageButtonState = pageReading;
    
    // Обработка кнопки переключения режима (D3)
    int modeReading = digitalRead(modeButtonPin);
    if (modeReading != lastModeButtonState) {
        lastModeDebounceTime = millis();
    }
    
    if ((millis() - lastModeDebounceTime) > 50) {
        if (modeReading != modeButtonState) {
            modeButtonState = modeReading;
            
            if (modeButtonState == HIGH) {
                showPercentage = !showPercentage;
                lcd.clear(); // Стираю старые данные
                updateDisplay();
            }
        }
    }
    lastModeButtonState = modeReading;
}

String getBatteryStatus(float voltage) {
    if (voltage < 0.5) return "Нет данных";
    if (voltage < 2.3) return "Критический разряд";
    if (voltage < 3.2) return "Низкий заряд";
    if (voltage > 4.2) return "Перезаряд";
    return "Норма";
}

int calculatePercentage(float voltage) {
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