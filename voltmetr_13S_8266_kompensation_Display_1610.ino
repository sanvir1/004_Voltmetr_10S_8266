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

// Конфигурация энкодера
const int encoderCLK = D0;  // Пин CLK энкодера
const int encoderDT = D3;   // Пин DT энкодера
const int encoderSW = D4;   // Пин кнопки энкодера (режим отображения)

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

// Переменные для энкодера
int encoderLastState = LOW;
int encoderCurrentState;
unsigned long lastEncoderDebounceTime = 0;

// Буферы для дисплея
char line0[17] = {0};
char line1[17] = {0};
char newLine0[17] = {0};
char newLine1[17] = {0};

#include "htmlPage.h"  // Веб страница полностью готовая подключается

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
    
    // Инициализация энкодера
    pinMode(encoderCLK, INPUT_PULLUP);
    // pinMode(encoderDT, INPUT_PULLUP);
    pinMode(encoderSW, INPUT_PULLUP);
    encoderLastState = digitalRead(encoderCLK);
    
    // Инициализация данных батарей
    for (int i = 0; i < NUM_BATTERIES; i++) {
        batteries[i] = {0.0, 0.0, 0.0, false, false, "Инициализация", 0, false};
        rawVoltages[i] = 0.0;
    }
    
    // Инициализация дисплея
    Wire.begin(D2, D1);
    lcd.init();
    delay(100); // Важная задержка!
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Voltmetr 10S");
    lcd.setCursor(0, 1);
    lcd.print("v2.3 +Encoder");
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
    checkEncoder();
    
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

    // Очищаем дисплей только при переключении страниц или режимов
    static int lastPage = -1;
    static bool lastShowPercentage = !showPercentage;
    
    if (currentPage != lastPage || showPercentage != lastShowPercentage) {
        lcd.clear();
        lastPage = currentPage;
        lastShowPercentage = showPercentage;
    }

    int startBattery = currentPage * DISPLAY_PAGE_SIZE;
    int endBattery = min(startBattery + DISPLAY_PAGE_SIZE, NUM_BATTERIES);

    // Первая строка (первые 3 элемента)
    lcd.setCursor(0, 0);
    for (int i = startBattery; i < min(startBattery + 3, endBattery); i++) {
        if (i > startBattery) lcd.print(" ");
        
        if (showPercentage) {
            lcd.print(batteries[i].percentage);
            lcd.print("%");
        } else {
            lcd.print(batteries[i].voltage, 1);
            lcd.print("v");
        }
    }

    // Вторая строка (оставшиеся 2 элемента + номер страницы)
    lcd.setCursor(0, 1);
    for (int i = startBattery + 3; i < endBattery; i++) {
        if (i > startBattery + 3) lcd.print(" ");
        
        if (showPercentage) {
            lcd.print(batteries[i].percentage);
            lcd.print("%");
        } else {
            lcd.print(batteries[i].voltage, 1);
            lcd.print("v");
        }
    }

    // Номер страницы (фиксированная позиция)
    lcd.setCursor(12, 1);
    lcd.print("P");
    lcd.print(currentPage + 1);
}

void checkEncoder() {
    static unsigned long lastModeDebounceTime = 0;
    static int lastModeButtonState = HIGH;
    static int modeButtonState = HIGH;
    
    // Обработка энкодера (переключение страниц)
    encoderCurrentState = digitalRead(encoderCLK);
    if (encoderCurrentState != encoderLastState) {
        if (digitalRead(encoderCLK) != encoderCurrentState) {
            // Поворот вправо
            currentPage = (currentPage + 1) % ((NUM_BATTERIES + DISPLAY_PAGE_SIZE - 1) / DISPLAY_PAGE_SIZE);
        } else {
            // Поворот влево
            currentPage = (currentPage - 1 + ((NUM_BATTERIES + DISPLAY_PAGE_SIZE - 1) / DISPLAY_PAGE_SIZE)) % ((NUM_BATTERIES + DISPLAY_PAGE_SIZE - 1) / DISPLAY_PAGE_SIZE);
        }
        lcd.clear();
        updateDisplay();
    }
    encoderLastState = encoderCurrentState;
    
    // Обработка кнопки энкодера (переключение режима)
    int modeReading = digitalRead(encoderSW);
    if (modeReading != lastModeButtonState) {
        lastModeDebounceTime = millis();
    }
    
    if ((millis() - lastModeDebounceTime) > 50) {
        if (modeReading != modeButtonState) {
            modeButtonState = modeReading;
            
            if (modeButtonState == LOW) {
                showPercentage = !showPercentage;
                lcd.clear();
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
    // for (int i = 0; i < 3; i++) {
    //     tone(buzzerPin1, buzzerTone, buzzerDuration);
    //     tone(buzzerPin2, buzzerTone, buzzerDuration);
    //     delay(buzzerDuration + 100);
    // }
}