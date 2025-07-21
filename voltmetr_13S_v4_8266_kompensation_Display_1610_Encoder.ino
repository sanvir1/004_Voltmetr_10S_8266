#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

// Настройки
const bool DEBUG_OUTPUT = false;
const unsigned long MEASUREMENT_INTERVAL = 1000;
const int NUM_BATTERIES = 13;
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

// Коэффициенты делителя напряжения (точность 0.0001)
float voltageDividerRatios[13] = {
  4.6956,
  7.7641,
  9.9781,
  11.9041,
  12.9452,
  14.0495, 

  14.8149,
  15.6339,
  15.8209,
  16.0339,
  1.0000,
  1.0000,
  1.0000
};

ESP8266WebServer server(80);
DNSServer dnsServer;

struct BatteryData {
  float voltage;
  float minVoltage;
  float maxVoltage;
  float rawVoltages;
  int rawADC;
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
#include "htmlPage_S.h"  // Веб страница полностью готовая подключается

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
        batteries[i].voltage = 0.0;
        batteries[i].minVoltage = 0.0;
        batteries[i].maxVoltage = 0.0;
        batteries[i].rawVoltages = 0.0;
        batteries[i].wasCritical = false;
        batteries[i].wasLow = false;
        batteries[i].status = "Инициализация";
        batteries[i].percentage = 0;
        batteries[i].alarmTriggered = false;
        rawVoltages[i] = 0.0;
    }
    
    // Инициализация дисплея
    Wire.begin(D2, D1);
    lcd.init();
    delay(100); // Важная задержка!
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Voltmetr 13S");
    lcd.setCursor(0, 1);
    lcd.print("v4 +Encoder");
    delay(2000);
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Setting page:");
    lcd.setCursor(0, 1);
    lcd.print(".../s");
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
    
    server.on("/s", []() {
        server.send(200, "text/html", htmlPage_S);
    });

    server.on("/getCoefficients", HTTP_GET, []() {
        if (DEBUG_OUTPUT) {
            Serial.println("Received request for coefficients");
        }

        DynamicJsonDocument doc(512);
        JsonArray coeffArray = doc.to<JsonArray>();

        for (int i = 0; i < NUM_BATTERIES; i++) {
            coeffArray.add(voltageDividerRatios[i]);
        }

        String json;
        serializeJson(doc, json);
        server.send(200, "application/json", json);
    });


    server.on("/updateCoefficient", HTTP_POST, []() {
        if (!server.hasArg("plain")) {
            server.send(400, "text/plain", "Bad Request");
            return;
        }
        
        DynamicJsonDocument doc(256);
        deserializeJson(doc, server.arg("plain"));
        
        int channel = doc["channel"];
        float coefficient = doc["coefficient"];
        
        if (DEBUG_OUTPUT) {
            Serial.printf("Updated coefficient C%d: %.4f\n", channel+1, coefficient);
        }


        if (channel >= 0 && channel < NUM_BATTERIES) {
            voltageDividerRatios[channel] = coefficient;
            server.send(200, "application/json", "{\"success\":true}");
        } else {
            server.send(400, "application/json", "{\"success\":false}");
        }
    });

    server.on("/getRawData", []() {
        DynamicJsonDocument doc(512);
        JsonArray rawArray = doc.createNestedArray("rawVoltages");
        
        for (int i = 0; i < NUM_BATTERIES; i++) {
            rawArray.add(rawVoltages[i]);
        }
        
        String json;
        serializeJson(doc, json);
        server.send(200, "application/json", json);
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
        doc["rawADC"] = batteries[channel].rawADC;
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
    for (int i = 0; i < 3; i++) {
        sum += analogRead(muxOut);
        delay(5);
    }
    int avgRaw = sum / 3;
    batteries[channel].rawADC = avgRaw; 
    batteries[channel].rawVoltages = (avgRaw / 1023.0) * 3.3; // Сохраняем напряжение до применения коэффициента
    float voltage = (avgRaw / 1023.0) * 3.3 * voltageDividerRatios[channel];
    rawVoltages[channel] = voltage;     


    // расчет напряжения
    float displayVoltage;
    if (channel == 0) {
        // Для первого канала берем абсолютное значение первого аккумулятора
        displayVoltage = voltage;
    } else if (channel == 1) {
        // Для второго канала - разницу между первым и вторым измерением
        displayVoltage = voltage - rawVoltages[0];
    } else {
        // Для остальных каналов - стандартный расчет
        displayVoltage = voltage - rawVoltages[channel-1];
        if (rawVoltages[channel-1] < 0.1) {
            displayVoltage = voltage;
        }
    }
    
    // Округление до 0.01V
    displayVoltage = round(displayVoltage * 10000) / 10000.0;
    
    // Если акккумулятор не подключен, то не рассчитываем напряжение
    if (displayVoltage < 0.3) {
        displayVoltage = 0;
    }

    // Обновление минимального и максимального напряжения
    if (displayVoltage > 0.1) {
        if (batteries[channel].minVoltage == 0 || displayVoltage < batteries[channel].minVoltage) {
            batteries[channel].minVoltage = displayVoltage;
        }
        if (displayVoltage > batteries[channel].maxVoltage) {
            batteries[channel].maxVoltage = displayVoltage;
        }
    }
    
    // Обновление статуса батареи
    batteries[channel].voltage = displayVoltage;
    batteries[channel].status = getBatteryStatus(displayVoltage);
    batteries[channel].percentage = calculatePercentage(displayVoltage);
    
    if (DEBUG_OUTPUT) {
        Serial.printf("C%d: Raw=%.4fV, Calc=%.4fV, Coeff=%.4f\n", 
                      channel+1, 
                      (avgRaw / 1023.0) * 3.3,
                      displayVoltage,
                      voltageDividerRatios[channel]);
    }

    // Сброс флагов тревоги при нормальном напряжении
    if (displayVoltage >= 3.2 && displayVoltage <= 4.2) {
        batteries[channel].alarmTriggered = false;
    }
}

void measureAllBatteries() {
    for (int i = 0; i < NUM_BATTERIES; i++) {
        updateBatteryData(i);
    }
}

void updateDisplay() {
    if (!displayInitialized) return;

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
            lcd.print(batteries[i].voltage, 2);
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
            lcd.print(batteries[i].voltage, 2);
        }
    }

    // Номер страницы (фиксированная позиция)
    lcd.setCursor(12, 1);
    lcd.print("P");
    lcd.print(currentPage + 1);
    //lcd.print("/3"); // Всего 3 страницы
}

// Обновите функцию checkEncoder():
void checkEncoder() {
    static unsigned long lastModeDebounceTime = 0;
    static int lastModeButtonState = HIGH;
    static int modeButtonState = HIGH;
    static bool buttonPressed = false;
    
    // Обработка энкодера (переключение страниц)
    encoderCurrentState = digitalRead(encoderCLK);
    if (encoderCurrentState != encoderLastState) {
        if (digitalRead(encoderCLK) != encoderCurrentState) {
            // Поворот вправо
            currentPage = (currentPage + 1) % 3; // 3 страницы
        } else {
            // Поворот влево
            //currentPage = (currentPage - 1 + 3) % 3; // 3 страницы
            currentPage = (currentPage + 1) % 3; // 3 страницы
        }
        lcd.clear();
        updateDisplay();
    }
    encoderLastState = encoderCurrentState;
    
    // Обработка кнопки энкодера
    int modeReading = digitalRead(encoderSW);
    
    if (modeReading != lastModeButtonState) {
        lastModeDebounceTime = millis();
    }
    
    if ((millis() - lastModeDebounceTime) > 50) {
        if (modeReading != modeButtonState) {
            modeButtonState = modeReading;
            
            if (modeButtonState == LOW) {
                // Кнопка нажата - показываем проценты
                buttonPressed = true;
                showPercentage = true;
                lcd.clear();
                updateDisplay();
            } else {
                // Кнопка отпущена - показываем напряжение
                buttonPressed = false;
                showPercentage = false;
                lcd.clear();
                updateDisplay();
            }
        }
    }
    lastModeButtonState = modeReading;
}

String getBatteryStatus(float voltage) {
    if (voltage <= 0.0) return "Нет данных";  // Добавлена проверка на 0
    if (voltage < 0.5) return "Нет данных";
    if (voltage < 2.3) return "Критический разряд";
    if (voltage < 3.2) return "Низкий заряд";
    if (voltage > 4.2) return "Перезаряд";
    return "Норма";
}

int calculatePercentage(float voltage) {
    if (voltage >= 4.2) return 100;
    if (voltage <= 3.0) return 0;
    // Более точная формула для Li-ion аккумуляторов
    float percentage = (voltage - 3.0) / (4.2 - 3.0) * 100;
    return constrain((int)percentage, 0, 100);
}


void playAlarmTone() {
    // for (int i = 0; i < 3; i++) {
    //     tone(buzzerPin1, buzzerTone, buzzerDuration);
    //     tone(buzzerPin2, buzzerTone, buzzerDuration);
    //     delay(buzzerDuration + 100);
    // }
}