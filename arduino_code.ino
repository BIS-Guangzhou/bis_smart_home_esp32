#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>
#include <Wire.h>
#include <SPI.h>
#include <FastLED.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <HardwareSerial.h>
#include <ESP32Servo.h>

// WiFi配置
const char* ssid = "STEAM";
const char* password = "bissteam";
WebServer server(80);

// 设备控制状态
struct DeviceState {
  bool rgbOn = false;      // RGB灯状态
  int rgbColor = 0;        // RGB颜色 (0=white, 1=yellow, 2=rainbow)
  int rgbBrightness = 50;  // RGB亮度 (0-255)
  bool fanOn = false;
  bool lcdOn = true;
  bool doorOpen = false;
  bool windowOpen = false;
  bool lightOn = false;
} deviceState;

//定义舵机引脚
#define servo1_PIN 14
#define servo2_PIN 10
#define OUTPUT_PIN 3  // WS2812的数据线连接引脚
#define NUM_LEDS 4    // LED数量
CRGB leds[NUM_LEDS];  // 定义LED数组

// 新增7引脚RGB灯条定义
#define RGBpin 7              // WS2812B数据引脚
#define RGB_NUM_LEDS 7        // LED灯珠的数量
CRGB rgb_leds[RGB_NUM_LEDS];  // 定义RGB LED数组

//定义DHT11温湿度引脚
#define DHTPIN 4       // DHT11数据引脚连接到D3
#define DHTTYPE DHT11  // 使用DHT11传感器
//定义风扇引脚
#define FanPinA 2  // 风扇PWM控制引脚
#define FanPinB 1  // L9110 1B
// 定义LCD1602的I2C转接引脚
#define I2C_SDA 8  // IO7作为SDA
#define I2C_SCL 9  // IO6作为SCL
//定义RC522引脚
#define RST_PIN 18  // RST引脚
#define SS_PIN 5    // SDA引脚(CS)

#define Photosensitive_Pin 17          //光敏电阻检测引脚 Photoresistor detection pin
const unsigned int digitalInPin = 19;  //水位传感器检测引脚
// 蜂鸣器引脚定义
int buzzerPin = 46;       // 假设蜂鸣器连接在GPIO46上
const int buttonPin = 6;  // 新增按键引脚

HardwareSerial SerialASR(1);

// 初始化LCD对象时不需要指定引脚，需要在setup()中初始化Wire
LiquidCrystal_I2C lcd(0x27, 16, 2);

// 创建DHT对象
DHT dht(DHTPIN, DHTTYPE);

MFRC522 mfrc522(SS_PIN, RST_PIN);  // 创建MFRC522实例

Servo servo1;  // 舵机1对象（引脚14） 门
Servo servo2;  // 舵机2对象（引脚10） 窗

int sensorValue = 0;  //水位值
int sensorValue1;     //光敏值

unsigned long previousMillis = 0;
const long interval = 1000;                // 更新间隔(ms)
bool shouldUpdateDHT = false;              // 是否应该更新DHT数据
float lastTemperature = 0;                 // 供Web显示的最近温度
float lastHumidity = 0;                    // 供Web显示的最近湿度
unsigned long lastVoiceTime = 0;           // 最后语音指令时间
const unsigned long voiceTimeout = 10000;  // 语音模式超时10秒
bool voiceMode = false;                    // 当前是否为语音模式

// RGB颜色状态
int rgbState = 0;  // 0=关闭, 1=白色, 2=黄色, 3=彩色
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 200;  // 按键消抖时间

// 预设的RFID卡片UID，用于验证身份
String validUID = "FF0E6EE6";

enum WindowControlMode { AUTO,
                         MANUAL };
WindowControlMode windowControlMode = AUTO;
unsigned long lastManualControlTime = 0;
const unsigned long manualControlTimeout = 3000;  // 3秒后恢复自动控制

void setup() {
  Serial.begin(115200);
  SPI.begin();         // 初始化SPI总线
  mfrc522.PCD_Init();  // 初始化MFRC522读卡器
  // 初始化舵机
  servo1.attach(servo1_PIN);                  // 舵机1接引脚10
  servo1.write(0);                            // 初始角度0度
  servo2.attach(servo2_PIN);                  // 舵机2接引脚14
  servo2.write(0);                            // 初始角度0度
  SerialASR.begin(9600, SERIAL_8N1, 20, 21);  // 确认模块实际波特率

  // 初始化3引脚FastLED
  FastLED.addLeds<WS2812, OUTPUT_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(deviceState.rgbBrightness);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();

  // 初始化7引脚RGB灯条
  FastLED.addLeds<WS2812B, RGBpin, GRB>(rgb_leds, RGB_NUM_LEDS);
  FastLED.setBrightness(deviceState.rgbBrightness);
  fill_solid(rgb_leds, RGB_NUM_LEDS, CRGB::Black);
  FastLED.show();

  pinMode(Photosensitive_Pin, INPUT);  //设置为输入引脚 Set to the input pin
  pinMode(digitalInPin, INPUT);
  digitalWrite(OUTPUT_PIN, LOW);
  pinMode(FanPinA, OUTPUT);
  pinMode(FanPinB, OUTPUT);
  digitalWrite(FanPinB, LOW);  // 设置电机方向
  Wire.begin(I2C_SDA, I2C_SCL);
  lcd.init();
  dht.begin();
  pinMode(buttonPin, INPUT_PULLUP);  // 初始化按键引脚
  pinMode(buzzerPin, OUTPUT);        // 设置蜂鸣器引脚为输出模式
  digitalWrite(buzzerPin, LOW);      // 初始关闭蜂鸣器
  Serial.println("RFID门禁系统已启动");
  Serial.print("等待验证卡片...");
  Serial.println("System Ready. Waiting for ASR commands...");

  // 连接WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // 设置Web服务器路由
  server.on("/", handleRoot);
  server.on("/control", handleControl);
  server.on("/status", handleStatus);
  server.begin();
}

void setRGBColor(int color) {
  // 设置3号引脚RGB灯
  FastLED.setBrightness(deviceState.rgbBrightness);
  switch (color) {
    case 0:  // 关闭
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      break;
    case 1:  // 白色
      fill_solid(leds, NUM_LEDS, CRGB(255, 255, 255));
      break;
    case 2:  // 黄色
      fill_solid(leds, NUM_LEDS, CRGB(255, 255, 0));
      break;
    case 3:  // 彩虹色
      static uint8_t hue3 = 0;
      for (int i = 0; i < NUM_LEDS; i++) {
        leds[i] = CHSV(hue3 + i * (255 / NUM_LEDS), 255, 255);
      }
      hue3++;
      delay(50);
      break;
  }
  FastLED.show();

  // 设置7号引脚RGB灯
  FastLED.setBrightness(deviceState.rgbBrightness);
  switch (color) {
    case 0:  // 关闭
      fill_solid(rgb_leds, RGB_NUM_LEDS, CRGB::Black);
      Serial.println("7-pin RGB: OFF");
      break;
    case 1:  // 白色
      fill_solid(rgb_leds, RGB_NUM_LEDS, CRGB(255, 255, 255));
      Serial.println("7-pin RGB: White");
      break;
    case 2:  // 黄色
      fill_solid(rgb_leds, RGB_NUM_LEDS, CRGB(255, 255, 0));
      Serial.println("7-pin RGB: Yellow");
      break;
    case 3:  // 彩虹色
      static uint8_t hue7 = 0;
      for (int i = 0; i < RGB_NUM_LEDS; i++) {
        rgb_leds[i] = CHSV(hue7 + i * (255 / RGB_NUM_LEDS), 255, 255);
      }
      hue7++;
      delay(50);
      Serial.println("7-pin RGB: Rainbow");
      break;
  }
  FastLED.show();
}

void setRGB7Color(int color) {
  // 单独设置7号引脚RGB灯
  FastLED.setBrightness(deviceState.rgbBrightness);
  switch (color) {
    case 0:  // 关闭
      fill_solid(rgb_leds, RGB_NUM_LEDS, CRGB::Black);
      Serial.println("7-pin RGB: OFF");
      break;
    case 1:  // 白色
      fill_solid(rgb_leds, RGB_NUM_LEDS, CRGB(255, 255, 255));
      Serial.println("7-pin RGB: White");
      break;
    case 2:  // 黄色
      fill_solid(rgb_leds, RGB_NUM_LEDS, CRGB(255, 255, 0));
      Serial.println("7-pin RGB: Yellow");
      break;
    case 3:  // 彩虹色
      static uint8_t hue7 = 0;
      for (int i = 0; i < RGB_NUM_LEDS; i++) {
        rgb_leds[i] = CHSV(hue7 + i * (255 / RGB_NUM_LEDS), 255, 255);
      }
      hue7++;
      delay(50);
      Serial.println("7-pin RGB: Rainbow");
      break;
  }
  FastLED.show();
}

void checkButton() {
  // 读取按键状态（带消抖）
  if (digitalRead(buttonPin) == LOW) {
    if (millis() - lastButtonPress > debounceDelay) {
      lastButtonPress = millis();

      rgbState++;          // 切换到下一个颜色
      if (rgbState > 2) {  // 现在只有3种状态：0=关闭,1=白色,2=黄色,3=彩色
        rgbState = 0;
      }
      setRGBColor(rgbState + 1);  // +1 因为网页控制使用1-3
    }
    delay(100);
  }
}

void readWeather_Sensor() {
  sensorValue1 = analogRead(Photosensitive_Pin);
  sensorValue = digitalRead(digitalInPin);
  Serial.print("Water = ");
  Serial.println(sensorValue);
  Serial.print("Light = ");
  Serial.println(sensorValue1);
  delay(500);

  // 检查是否需要恢复自动控制
  if (windowControlMode == MANUAL && millis() - lastManualControlTime > manualControlTimeout) {
    windowControlMode = AUTO;
    Serial.println("Window control returned to AUTO mode");
  }

  // 只有在自动模式下才响应传感器
  if (windowControlMode == AUTO) {
    // 添加光敏控制RGB灯的逻辑
    if (sensorValue1 > 2500) {
      if (!deviceState.rgbOn) {  // 只有当RGB灯未被手动开启时才自动控制
        fill_solid(leds, NUM_LEDS, CRGB::White);
        FastLED.show();
        Serial.println("Light intensity > 2500, turning on white LED");
      }
    } else {
      // 只有当不是语音控制且未被手动开启时才关闭LED
      if (!voiceMode && !deviceState.rgbOn) {
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
      }
    }

    if (sensorValue1 <= 2000 && sensorValue == 0) {
      servo2.write(100);
      deviceState.windowOpen = true;
    } else {
      servo2.write(0);
      deviceState.windowOpen = false;
    }
  }
}

void RC522() {
  // 检查是否有新的RFID卡片靠近
  if (mfrc522.PICC_IsNewCardPresent()) {
    // 尝试读取卡片
    if (mfrc522.PICC_ReadCardSerial()) {
      // 获取卡片的UID
      String cardUID = "";
      for (byte i = 0; i < mfrc522.uid.size; i++) {
        // 补零以确保两位十六进制表示
        if (mfrc522.uid.uidByte[i] < 0x10) {
          cardUID += "0";
        }
        cardUID += String(mfrc522.uid.uidByte[i], HEX);
      }
      cardUID.toUpperCase();

      // 在串口监视器上打印卡片的UID
      Serial.println("\n检测到卡片,UID: " + cardUID);

      // 验证卡片UID
      if (cardUID.equals(validUID)) {
        // UID匹配，解锁门禁
        Serial.println("验证成功，门已解锁");

        // 控制舵机旋转到开门位置（100°）
        servo1.write(100);
        digitalWrite(buzzerPin, HIGH);
        delay(3000);  // 解锁后保持3秒

        // 重新锁定门禁
        servo1.write(0);  // 舵机转到关门位置
        digitalWrite(buzzerPin, LOW);
        Serial.println("门已重新锁定");
      } else {
        // UID不匹配，拒绝访问
        Serial.println("验证失败，拒绝访问");
      }

      // 停止读取当前卡片
      mfrc522.PICC_HaltA();
      // 停止加密
      mfrc522.PCD_StopCrypto1();
    }
  }
}

void readDHT11_Sensor(bool forceUpdate = false) {
  static unsigned long lastUpdateTime = 0;
  const long updateInterval = 2000;  // 2秒更新一次

  // 检查是否需要更新数据
  if (forceUpdate || millis() - lastUpdateTime >= updateInterval) {
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();

    // 数据有效性检查 - 始终更新Web显示的全局变量
    if (!isnan(humidity) && !isnan(temperature)) {
      lastHumidity = humidity;
      lastTemperature = temperature;

      // 仅当LCD开启时更新屏幕
      if (deviceState.lcdOn) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("H:");
        lcd.print(humidity, 1);
        lcd.print("%");

        lcd.setCursor(0, 1);
        lcd.print("T:");
        lcd.print(temperature, 1);
        lcd.print("C");
      }

      lastUpdateTime = millis();

      Serial.print("DHT Updated - H:");
      Serial.print(humidity);
      Serial.print("% T:");
      Serial.print(temperature);
      Serial.println("C");
    }
  }
}

void handleVoiceCommand() {
  if (SerialASR.available() > 0) {
    // 原始数据十六进制打印（关键调试步骤）
    voiceMode = true;          // 进入语音模式
    lastVoiceTime = millis();  // 更新最后指令时间
    Serial.print("Raw HEX: ");
    while (SerialASR.available()) {
      byte rawData = SerialASR.read();
      Serial.print(rawData);
      Serial.print(" ");

      // 直接处理原始字节
      if (rawData == 10 || rawData == 'A') {  // 'A'的ASCII码是0x41
        fill_solid(leds, NUM_LEDS, CRGB::White);
        FastLED.show();
        Serial.println("\nLight ON (A detected)");
      }
      if (rawData == 11 || rawData == 'B') {  // 'B'的ASCII码是0x42
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
        Serial.println("\nLight OFF (B detected)");
      }
      if (rawData == 12 || rawData == 'C') {
        servo1.write(100);
        Serial.println("Servo1: ON");
      }
      if (rawData == 13 || rawData == 'D') {
        servo1.write(0);
        Serial.println("Servo1: OFF");
      }
      if (rawData == 14 || rawData == 'E') {
        servo2.write(100);
        Serial.println("Servo2: ON");
      }
      if (rawData == 15 || rawData == 'F') {
        servo2.write(0);
        Serial.println("Servo2: OFF");
      }
      if (rawData == 1) {
        analogWrite(FanPinA, 40);
        digitalWrite(FanPinB, 0);
        Serial.println("Fans: ON");
      }
      if (rawData == 2) {
        analogWrite(FanPinA, 0);
        digitalWrite(FanPinB, 0);
        Serial.println("Fans: OFF");
      }
      if (rawData == 3) {
        lcd.backlight();         // 开启背光
        shouldUpdateDHT = true;  // 允许更新DHT数据
        readDHT11_Sensor(true);  // 强制立即更新
        Serial.println("LCD1602: ON");
      }
      if (rawData == 4) {
        lcd.clear();        // 清空屏幕内容
        lcd.noBacklight();  // 关闭背光（节能）
        Serial.println("LC1602: OFF");
      }
      // 修改：语音指令5、6只控制7号引脚的RGB灯
      if (rawData == 5) {
        deviceState.rgbOn = true;
        deviceState.rgbColor = 0;  // 白色
        setRGB7Color(1);           // 只控制7号引脚
      }
      if (rawData == 6) {
        deviceState.rgbOn = false;
        setRGB7Color(0);  // 只控制7号引脚
      }
    }
  }

  if (voiceMode && (millis() - lastVoiceTime > voiceTimeout)) {
    voiceMode = false;
    Serial.println("Return to Normal Mode");
  }
}

void handleRoot() {
  String html = R"=====(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>BIS School Smart Control</title>
  <style>
    @import url('https://fonts.googleapis.com/css2?family=Outfit:wght@400;600;700&display=swap');
    * { box-sizing: border-box; }
    body {
      font-family: 'Outfit', sans-serif;
      margin: 0;
      padding: 20px;
      min-height: 100vh;
      background: linear-gradient(135deg, #0f172a 0%, #1e293b 50%, #0f172a 100%);
      color: #e2e8f0;
      display: flex;
      flex-direction: column;
      align-items: center;
    }
    .container { max-width: 420px; width: 100%; }
    
    .header {
      text-align: center;
      margin-bottom: 24px;
    }
    .header h1 {
      font-size: 1.5rem;
      font-weight: 700;
      margin: 0;
      background: linear-gradient(90deg, #38bdf8, #818cf8);
      -webkit-background-clip: text;
      -webkit-text-fill-color: transparent;
      background-clip: text;
    }
    .header p { font-size: 0.85rem; color: #94a3b8; margin: 4px 0 0; }
    
    .sensor-card {
      background: linear-gradient(145deg, #1e293b 0%, #334155 100%);
      border-radius: 16px;
      padding: 24px;
      margin-bottom: 20px;
      border: 1px solid rgba(56, 189, 248, 0.2);
      box-shadow: 0 4px 24px rgba(0,0,0,0.3);
    }
    .sensor-card h3 {
      font-size: 0.75rem;
      text-transform: uppercase;
      letter-spacing: 1.5px;
      color: #94a3b8;
      margin: 0 0 16px;
    }
    .sensor-row {
      display: flex;
      gap: 16px;
      justify-content: space-around;
    }
    .sensor-item {
      flex: 1;
      text-align: center;
      padding: 16px;
      background: rgba(15, 23, 42, 0.6);
      border-radius: 12px;
      border: 1px solid rgba(255,255,255,0.06);
    }
    .sensor-value {
      font-size: 2rem;
      font-weight: 700;
      color: #38bdf8;
      display: block;
      line-height: 1.2;
    }
    .sensor-unit { font-size: 0.8rem; color: #64748b; }
    .sensor-label { font-size: 0.7rem; color: #94a3b8; margin-top: 4px; }
    
    .control-card {
      background: linear-gradient(145deg, #1e293b 0%, #334155 100%);
      border-radius: 16px;
      padding: 20px;
      margin-bottom: 16px;
      border: 1px solid rgba(255,255,255,0.06);
      box-shadow: 0 4px 24px rgba(0,0,0,0.3);
    }
    .control-card h2 {
      font-size: 0.9rem;
      font-weight: 600;
      margin: 0 0 12px;
      color: #f1f5f9;
    }
    .btn-row { display: flex; flex-wrap: wrap; gap: 8px; margin-bottom: 12px; }
    .btn-row:last-child { margin-bottom: 0; }
    .btn {
      padding: 10px 18px;
      border: none;
      border-radius: 10px;
      font-family: inherit;
      font-weight: 600;
      font-size: 0.85rem;
      cursor: pointer;
      transition: transform 0.15s, box-shadow 0.15s;
    }
    .btn:active { transform: scale(0.97); }
    .btn-on { background: linear-gradient(135deg, #22c55e, #16a34a); color: white; }
    .btn-on:hover { box-shadow: 0 0 20px rgba(34, 197, 94, 0.4); }
    .btn-off { background: linear-gradient(135deg, #ef4444, #dc2626); color: white; }
    .btn-off:hover { box-shadow: 0 0 20px rgba(239, 68, 68, 0.4); }
    .rgb-btn {
      width: 48px;
      height: 48px;
      padding: 0;
      border-radius: 50%;
      border: 2px solid rgba(255,255,255,0.3);
    }
    .rgb-btn:hover { transform: scale(1.05); }
    .slider-container { margin: 12px 0; }
    .slider {
      width: 100%;
      height: 8px;
      -webkit-appearance: none;
      background: #334155;
      border-radius: 4px;
      outline: none;
    }
    .slider::-webkit-slider-thumb {
      -webkit-appearance: none;
      width: 20px;
      height: 20px;
      background: #38bdf8;
      border-radius: 50%;
      cursor: pointer;
      box-shadow: 0 0 10px rgba(56, 189, 248, 0.5);
    }
    .brightness-label { font-size: 0.8rem; color: #94a3b8; }
    
    .status-card {
      background: linear-gradient(145deg, #1e293b 0%, #334155 100%);
      border-radius: 16px;
      padding: 20px;
      border: 1px solid rgba(255,255,255,0.06);
      font-size: 0.85rem;
    }
    .status-card h3 { font-size: 0.9rem; margin: 0 0 12px; color: #f1f5f9; }
    .status-grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 8px 16px;
    }
    .status-item { color: #94a3b8; }
    .status-item span { color: #e2e8f0; font-weight: 600; }
  </style>
</head>
<body>
  <div class="container">
    <div class="header">
      <h1>BIS School Smart Control</h1>
      <p>WiFi Connected Device Control</p>
    </div>
    
    <div class="sensor-card">
      <h3>Environment</h3>
      <div class="sensor-row">
        <div class="sensor-item">
          <span class="sensor-value" id="tempDisplay">--</span>
          <span class="sensor-unit">°C</span>
          <div class="sensor-label">Temperature</div>
        </div>
        <div class="sensor-item">
          <span class="sensor-value" id="humidDisplay">--</span>
          <span class="sensor-unit">%</span>
          <div class="sensor-label">Humidity</div>
        </div>
      </div>
    </div>
    
    <div class="control-card">
      <h2>RGB LED</h2>
      <div class="btn-row">
        <button class="btn btn-on" onclick="control('rgb', 'on')">On</button>
        <button class="btn btn-off" onclick="control('rgb', 'off')">Off</button>
      </div>
      <div class="btn-row">
        <button class="rgb-btn" style="background:#fff;" onclick="control('rgb_color', 'white')"></button>
        <button class="rgb-btn" style="background:#facc15;" onclick="control('rgb_color', 'yellow')"></button>
        <button class="rgb-btn" style="background:linear-gradient(135deg,#f43f5e,#f97316,#eab308,#22c55e,#3b82f6,#8b5cf6);" onclick="control('rgb_color', 'rainbow')"></button>
      </div>
      <div class="slider-container">
        <span class="brightness-label">Brightness: <strong id="rgbBrightnessValue">50</strong></span>
        <input type="range" id="rgbBrightness" class="slider" min="0" max="100" value="50" onchange="updateBrightness('rgb', this.value)">
      </div>
    </div>
    
    <div class="control-card">
      <h2>Devices</h2>
      <div class="btn-row">
        <button class="btn btn-on" onclick="control('fan', 'on')">Fan On</button>
        <button class="btn btn-off" onclick="control('fan', 'off')">Fan Off</button>
        <button class="btn btn-on" onclick="control('lcd', 'on')">LCD On</button>
        <button class="btn btn-off" onclick="control('lcd', 'off')">LCD Off</button>
      </div>
    </div>
    
    <div class="control-card">
      <h2>Door & Window</h2>
      <div class="btn-row">
        <button class="btn btn-on" onclick="control('door', 'open')">Open Door</button>
        <button class="btn btn-off" onclick="control('door', 'close')">Close Door</button>
        <button class="btn btn-on" onclick="control('window', 'open')">Open Window</button>
        <button class="btn btn-off" onclick="control('window', 'close')">Close Window</button>
      </div>
    </div>
    
    <div class="status-card">
      <h3>Status</h3>
      <div class="status-grid" id="status">
        <div class="status-item">RGB: <span id="stRgb">--</span></div>
        <div class="status-item">Fan: <span id="stFan">--</span></div>
        <div class="status-item">LCD: <span id="stLcd">--</span></div>
        <div class="status-item">Door: <span id="stDoor">--</span></div>
        <div class="status-item">Window: <span id="stWindow">--</span></div>
        <div class="status-item">Mode: <span id="stMode">--</span></div>
      </div>
    </div>
  </div>
  
  <script>
    function control(device, action) {
      fetch('/control?device=' + device + '&action=' + action)
        .then(r => r.json())
        .then(updateStatus);
    }
    function updateBrightness(device, value) {
      document.getElementById('rgbBrightnessValue').textContent = value;
      fetch('/control?device=rgb_brightness&action=' + value)
        .then(r => r.json())
        .then(updateStatus);
    }
    function updateStatus(data) {
      document.getElementById('tempDisplay').textContent = (data.temperature != null ? data.temperature : '--');
      document.getElementById('humidDisplay').textContent = (data.humidity != null ? data.humidity : '--');
      document.getElementById('stRgb').textContent = data.rgbOn ? 'ON (' + data.rgbColor + ')' : 'OFF';
      document.getElementById('stFan').textContent = data.fan ? 'ON' : 'OFF';
      document.getElementById('stLcd').textContent = data.lcd ? 'ON' : 'OFF';
      document.getElementById('stDoor').textContent = data.door ? 'OPEN' : 'CLOSED';
      document.getElementById('stWindow').textContent = data.window ? 'OPEN' : 'CLOSED';
      document.getElementById('stMode').textContent = data.windowControlMode || '--';
      var sb = document.getElementById('rgbBrightness');
      if (sb) { sb.value = data.rgbBrightness; }
      document.getElementById('rgbBrightnessValue').textContent = data.rgbBrightness;
    }
    fetch('/status').then(r => r.json()).then(updateStatus);
    setInterval(function(){ fetch('/status').then(r => r.json()).then(updateStatus); }, 3000);
  </script>
</body>
</html>
)=====";

  server.send(200, "text/html", html);
}

void handleControl() {
  String device = server.arg("device");
  String action = server.arg("action");

  if (device == "rgb") {
    if (action == "on") {
      deviceState.rgbOn = true;
      setRGBColor(deviceState.rgbColor + 1);
    } else {
      deviceState.rgbOn = false;
      setRGBColor(0);
    }
  } else if (device == "rgb_color") {
    deviceState.rgbOn = true;
    if (action == "white") deviceState.rgbColor = 0;
    else if (action == "yellow") deviceState.rgbColor = 1;
    else if (action == "rainbow") deviceState.rgbColor = 2;
    setRGBColor(deviceState.rgbColor + 1);
  } else if (device == "rgb_brightness") {
    deviceState.rgbBrightness = action.toInt();
    if (deviceState.rgbOn) {
      setRGBColor(deviceState.rgbColor + 1);
    }
  } else if (device == "fan") {
    if (action == "on") {
      analogWrite(FanPinA, 40);
      deviceState.fanOn = true;
    } else {
      analogWrite(FanPinA, 0);
      deviceState.fanOn = false;
    }
  } else if (device == "lcd") {
    if (action == "on") {
      lcd.backlight();
      deviceState.lcdOn = true;
      shouldUpdateDHT = true;
      readDHT11_Sensor(true);
    } else {
      lcd.noBacklight();
      deviceState.lcdOn = false;
    }
  } else if (device == "door") {
    if (action == "open") {
      servo1.write(100);
      deviceState.doorOpen = true;
    } else {
      servo1.write(0);
      deviceState.doorOpen = false;
    }
  } else if (device == "window") {
    windowControlMode = MANUAL;  // 设置为手动模式
    lastManualControlTime = millis();

    if (action == "open") {
      servo2.write(100);
      deviceState.windowOpen = true;
    } else {
      servo2.write(0);
      deviceState.windowOpen = false;
    }
  }

  // 返回当前状态
  handleStatus();
}

void handleStatus() {
  String json = "{";
  json += "\"rgbOn\":" + String(deviceState.rgbOn ? "true" : "false") + ",";
  json += "\"rgbColor\":\"";
  switch (deviceState.rgbColor) {
    case 0: json += "white"; break;
    case 1: json += "yellow"; break;
    case 2: json += "rainbow"; break;
  }
  json += "\",";
  json += "\"rgbBrightness\":" + String(deviceState.rgbBrightness) + ",";
  json += "\"fan\":" + String(deviceState.fanOn ? "true" : "false") + ",";
  json += "\"lcd\":" + String(deviceState.lcdOn ? "true" : "false") + ",";
  json += "\"door\":" + String(deviceState.doorOpen ? "true" : "false") + ",";
  json += "\"window\":" + String(deviceState.windowOpen ? "true" : "false") + ",";
  json += "\"windowControlMode\":\"" + String(windowControlMode == AUTO ? "auto" : "manual") + "\",";
  json += "\"temperature\":" + String(lastTemperature, 1) + ",";
  json += "\"humidity\":" + String(lastHumidity, 1);
  json += "}";

  server.send(200, "application/json", json);
}

void loop() {
  server.handleClient();  // 处理Web请求

  if (shouldUpdateDHT) {
    readDHT11_Sensor();  // 常规更新
  }

  checkButton();  // 检查按键状态

  readDHT11_Sensor();  // 定期读取温湿度供Web显示

  if (voiceMode) {
    handleVoiceCommand();  // 语音模式优先
  } else {
    RC522();
    readWeather_Sensor();  // 常规天气控制
    handleVoiceCommand();  // 仍检查语音指令
  }

  // 彩虹效果处理
  if (deviceState.rgbOn && deviceState.rgbColor == 2) {
    setRGBColor(3);  // 调用彩虹效果
  }
}