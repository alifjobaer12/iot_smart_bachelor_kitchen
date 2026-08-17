#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h> 
#include <DHT.h>
#include <ESP32Servo.h>
#include <time.h>
#include "secrets.h"

// ==========================================
// PIN DEFINITIONS
// ==========================================
// ST7789 SPI Display
#define TFT_CS    5
#define TFT_DC    2
#define TFT_RST   15

// Dedicated Cooking Button Pin (Internal Pull-Up -> Connect between Pin 13 and GND)
#define BTN_COOK  13 

// Sensors
#define PIR_PIN   36
#define FLAME_PIN 39
#define MQ2_PIN   32
#define DHT_PIN   33
#define TRIG_TAP  25
#define ECHO_TAP  26
#define TRIG_BIN  27
#define ECHO_BIN  14

// Actuators
#define MOTOR_TAP_PIN   12 // Signal to a logic-level MOSFET motor driver
#define FAN_PIN         4  // Signal to a logic-level MOSFET fan driver
#define LED_PIN         16
#define BUZZER_PIN      17
#define SERVO_BIN_PIN   21
#define SERVO_EMERG_PIN 22

// ==========================================
// THRESHOLDS & TIMINGS
// ==========================================
const int GAS_THRESHOLD = 15000;       
const float TEMP_THRESHOLD = 32.0;    
const int DISTANCE_THRESHOLD = 15;    
const unsigned long LED_DELAY = 60000;
const unsigned long PAGE_AUTO_SWITCH_INTERVAL = 10000; // 8 seconds per page
const unsigned long WIFI_CONNECT_TIMEOUT = 15000;
const char* WIFI_SETUP_AP_NAME = "SmartKitchen-Setup";

// ==========================================
// OBJECTS & THREADING
// ==========================================
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
DHT dht(DHT_PIN, DHT11);
Servo servoBin;
Servo servoEmergency;

TaskHandle_t TaskUI;
SemaphoreHandle_t dataMutex;
WebServer wifiSetupServer(80);
DNSServer wifiSetupDns;
Preferences wifiPreferences;
bool wifiSetupPortalActive = false;
bool wifiSetupRoutesConfigured = false;
unsigned long wifiAttemptStarted = 0;
String activeWifiSsid;
String activeWifiPassword;

// ==========================================
// AUTH & SHARED VARIABLES
// ==========================================
String firebaseIdToken = "";
unsigned long tokenAuthTime = 0;

float sharedTemp = 0.0;
float sharedHum = 0.0;
int sharedGas = 0;
bool isEmergency = false;

int currentPage = 0; 
bool warningTriggered = false;

String bfastMenu = "-", lunchMenu = "-", dinnerMenu = "-";
int bfastCount = 0, lunchCount = 0, dinnerCount = 0, totalCount = 0;

// ==========================================
// HARDWARE INTERRUPT (ISR) ROUTINE
// ==========================================
volatile bool flagCookPressed = false;
volatile unsigned long lastCookIsrTime = 0;

void IRAM_ATTR isrCookButton() {
  unsigned long now = millis();
  if (now - lastCookIsrTime > 1000) { // 1000ms debounce
    flagCookPressed = true;
    lastCookIsrTime = now;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- ESP32 Smart Project Starting ---");

  dataMutex = xSemaphoreCreateMutex();

  Serial.println("Initializing Hardware (Core 1)...");
  initHardware();

  Serial.println("Starting UI and Network Task (Core 0)...");
  xTaskCreatePinnedToCore(TaskUI_Network, "TaskUI", 20000, NULL, 1, &TaskUI, 0); 
}

void loop() {
  // ==========================================
  // CORE 1: HARDWARE LOGIC 
  // ==========================================
  unsigned long currentMillis = millis();
  static unsigned long lastMotionTime = 0;
  static unsigned long lastSerialPrint = 0;
  static bool lastEmergencyState = false;

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int g = analogRead(MQ2_PIN);
  bool fire = digitalRead(FLAME_PIN) == HIGH; 
  int distTap = getDistance(TRIG_TAP, ECHO_TAP);
  int distBin = getDistance(TRIG_BIN, ECHO_BIN);

  bool gasLeak = (g > GAS_THRESHOLD);
  bool isHot = (!isnan(t) && t >= TEMP_THRESHOLD);
  bool emergencyState = (gasLeak || fire);

  if (currentMillis - lastSerialPrint >= 2000) {
    Serial.printf("[Core 1] Temp: %.1fC | Hum: %.1f%% | Gas: %d | Fire: %d\n", t, h, g, fire);
    lastSerialPrint = currentMillis;
  }

  if (emergencyState != lastEmergencyState) {
    if (emergencyState) {
      Serial.println("\n[Core 1] >>> EMERGENCY TRIGGERED! <<<");
      postWarningFirebase(true, fire ? "Fire Detected" : "Gas Leak Detected");
    } else {
      Serial.println("\n[Core 1] >>> EMERGENCY CLEARED. <<<");
      postWarningFirebase(false, "Normal");
    }
    lastEmergencyState = emergencyState;
  }

  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    if (!isnan(t)) sharedTemp = t;
    if (!isnan(h)) sharedHum = h;
    sharedGas = g;
    isEmergency = emergencyState;
    xSemaphoreGive(dataMutex);
  }

  bool handDetected = (distTap < DISTANCE_THRESHOLD);
  digitalWrite(MOTOR_TAP_PIN, (handDetected || fire) ? HIGH : LOW);
  servoBin.write((distBin < DISTANCE_THRESHOLD) ? 90 : 0);

  if (digitalRead(PIR_PIN) == HIGH) {
    digitalWrite(LED_PIN, HIGH);
    lastMotionTime = currentMillis;
  } else if (currentMillis - lastMotionTime >= LED_DELAY) {
    digitalWrite(LED_PIN, LOW);
  }

  if (emergencyState) {
    digitalWrite(BUZZER_PIN, HIGH);
    servoEmergency.write(90); 
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    servoEmergency.write(0);
  }

  digitalWrite(FAN_PIN, (isHot || gasLeak) ? HIGH : LOW);
  delay(100); 
}

// ==========================================
// CORE 0: UI, DISPLAY & WIFI TASK
// ==========================================
void TaskUI_Network(void * pvParameters) {
  tft.init(240, 320); 
  tft.setRotation(1); 
  tft.fillScreen(ST77XX_BLACK);

  tft.setCursor(10, 10);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.print("Connecting to Wi-Fi...");
  
  loadWifiCredentials();
  beginWifiConnection();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - wifiAttemptStarted < WIFI_CONNECT_TIMEOUT) {
    vTaskDelay(250 / portTICK_PERIOD_MS);
  }

  if (WiFi.status() == WL_CONNECTED) {
    stopWifiSetupPortal();
    configTime(21600, 0, "pool.ntp.org", "time.nist.gov"); 
    if (loginFirebase()) {
      fetchMealsFirebase();
    }
  }
  
  tft.fillScreen(ST77XX_BLACK); 

  unsigned long lastApiTime = 0;
  unsigned long lastPageSwitchTime = millis();
  bool previouslyConnected = (WiFi.status() == WL_CONNECTED);

  if (WiFi.status() != WL_CONNECTED) startWifiSetupPortal();

  for(;;) { 
    if (wifiSetupPortalActive) {
      wifiSetupDns.processNextRequest();
      wifiSetupServer.handleClient();
    }

    if (WiFi.status() == WL_CONNECTED) {
      if (wifiSetupPortalActive) stopWifiSetupPortal();
    } else {
      if (previouslyConnected) {
        wifiAttemptStarted = millis();
        WiFi.begin(activeWifiSsid.c_str(), activeWifiPassword.c_str());
      }
      if (!wifiSetupPortalActive && millis() - wifiAttemptStarted >= WIFI_CONNECT_TIMEOUT) {
        startWifiSetupPortal();
      }
    }
    previouslyConnected = (WiFi.status() == WL_CONNECTED);

    if (WiFi.status() == WL_CONNECTED &&
        (firebaseIdToken == "" || (millis() - tokenAuthTime > 3000000))) {
      loginFirebase();
    }

    // 1. Automatic Page Switcher (Every 8s)
    if (millis() - lastPageSwitchTime >= PAGE_AUTO_SWITCH_INTERVAL) {
      currentPage = (currentPage == 0) ? 1 : 0;
      tft.fillScreen(ST77XX_BLACK);
      lastPageSwitchTime = millis();
    }

    // 2. Hardware Interrupt Check for Cook Button on Pin 13
    if (flagCookPressed) {
      flagCookPressed = false;
      if (digitalRead(BTN_COOK) == LOW) { // Pin verified grounded
        Serial.println("[Core 0] Cook Button Pressed! Sending to Firebase...");
        postCookingFirebase();
      }
    }

    // 3. Thread-safe copy of sensor data
    float cT = 0, cH = 0;
    int cG = 0;
    bool emerg = false;
    if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
      cT = sharedTemp;
      cH = sharedHum;
      cG = sharedGas;
      emerg = isEmergency;
      xSemaphoreGive(dataMutex);
    }

    // 4. UI Drawing
    if (emerg) {
      if (!warningTriggered) {
        tft.fillScreen(ST77XX_RED);
        warningTriggered = true;
      }
      drawWarningPage();
    } else {
      if (warningTriggered) {
        tft.fillScreen(ST77XX_BLACK); 
        warningTriggered = false;
      }
      if (currentPage == 0) drawMealPage();
      else drawSensorPage(cT, cH, cG);
    }

    // 5. Periodic Sync every 60 seconds
    if (WiFi.status() == WL_CONNECTED && (millis() - lastApiTime > 6000)) {
      postSensorsFirebase(cT, cH, cG);
      fetchMealsFirebase();
      lastApiTime = millis();
    }

    vTaskDelay(20 / portTICK_PERIOD_MS); 
  }
}

// ==========================================
// TEMPORARY WI-FI SETUP PORTAL
// ==========================================
void loadWifiCredentials() {
  wifiPreferences.begin("wifi", true);
  activeWifiSsid = wifiPreferences.getString("ssid", ssid);
  activeWifiPassword = wifiPreferences.getString("password", password);
  wifiPreferences.end();
}

void beginWifiConnection() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(activeWifiSsid.c_str(), activeWifiPassword.c_str());
  wifiAttemptStarted = millis();
  Serial.printf("[WiFi] Connecting to %s...\n", activeWifiSsid.c_str());
}

String wifiSetupPage(const String& message = "") {
  String page = F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
                  "<title>Smart Kitchen Wi-Fi</title><style>body{font-family:sans-serif;max-width:420px;"
                  "margin:40px auto;padding:20px;background:#f4f6f8}main{background:white;padding:24px;"
                  "border-radius:12px;box-shadow:0 2px 12px #bbb}input,button{box-sizing:border-box;width:100%;"
                  "padding:12px;margin:8px 0}button{background:#087f5b;color:white;border:0;border-radius:6px}"
                  "</style></head><body><main><h2>Smart Kitchen Wi-Fi Setup</h2>");
  if (message.length()) page += "<p>" + message + "</p>";
  page += F("<form method='post' action='/save'><label>Wi-Fi name (SSID)</label>"
            "<input name='ssid' maxlength='32' required><label>Password</label>"
            "<input name='password' type='password' maxlength='63'>"
            "<button type='submit'>Save and connect</button></form>"
            "<p>This page closes automatically after Wi-Fi connects.</p></main></body></html>");
  return page;
}

void startWifiSetupPortal() {
  if (wifiSetupPortalActive) return;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(WIFI_SETUP_AP_NAME);
  wifiSetupDns.start(53, "*", WiFi.softAPIP());

  if (!wifiSetupRoutesConfigured) {
    wifiSetupServer.on("/", HTTP_GET, []() {
      wifiSetupServer.send(200, "text/html", wifiSetupPage());
    });
    wifiSetupServer.on("/save", HTTP_POST, []() {
      String newSsid = wifiSetupServer.arg("ssid");
      String newPassword = wifiSetupServer.arg("password");
      newSsid.trim();
      if (newSsid.length() == 0) {
        wifiSetupServer.send(400, "text/html", wifiSetupPage("Wi-Fi name is required."));
        return;
      }

      wifiPreferences.begin("wifi", false);
      wifiPreferences.putString("ssid", newSsid);
      wifiPreferences.putString("password", newPassword);
      wifiPreferences.end();
      activeWifiSsid = newSsid;
      activeWifiPassword = newPassword;

      wifiSetupServer.send(200, "text/html", wifiSetupPage("Saved. Connecting now..."));
      delay(150);
      stopWifiSetupPortal();
      beginWifiConnection();
    });
    wifiSetupServer.onNotFound([]() {
      wifiSetupServer.sendHeader("Location", "http://192.168.4.1/", true);
      wifiSetupServer.send(302, "text/plain", "");
    });
    wifiSetupRoutesConfigured = true;
  }
  wifiSetupServer.begin();
  wifiSetupPortalActive = true;
  Serial.println("[WiFi] Setup portal: connect to SmartKitchen-Setup and open http://192.168.4.1");
}

void stopWifiSetupPortal() {
  if (!wifiSetupPortalActive) return;
  wifiSetupDns.stop();
  wifiSetupServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  wifiSetupPortalActive = false;
  Serial.println("[WiFi] Setup portal stopped.");
}

// ==========================================
// UI DRAWING FUNCTIONS
// ==========================================
void drawWifiStatusIcon() {
  tft.fillRect(260, 2, 55, 18, ST77XX_BLACK); 
  if (WiFi.status() == WL_CONNECTED && firebaseIdToken != "") {
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(1);
    tft.setCursor(265, 5);
    tft.print("WIFI OK");
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.setTextSize(1);
    tft.setCursor(260, 5);
    tft.print("NO AUTH");
  }
}

void drawTimeDate() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[10], dateStr[15];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    strftime(dateStr, sizeof(dateStr), "%d/%m/%y", &timeinfo);

    tft.fillRect(0, 0, 255, 20, ST77XX_BLACK); 
    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(2);
    tft.setCursor(10, 5); tft.print(dateStr);
    tft.setCursor(150, 5); tft.print(timeStr);
  } else {
    tft.fillRect(0, 0, 255, 20, ST77XX_BLACK);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(1);
    tft.setCursor(10, 5); tft.print("Time Syncing...");
  }
  drawWifiStatusIcon();
}

void drawMealPage() {
  drawTimeDate();
  tft.setTextSize(2);
  
  tft.setTextColor(ST77XX_RED); tft.setCursor(10, 40); tft.print("B.Fast");
  tft.setTextColor(ST77XX_GREEN); tft.setCursor(110, 40); tft.print("Lunch");
  tft.setTextColor(ST77XX_ORANGE); tft.setCursor(210, 40); tft.print("Dinner");

  tft.fillRect(0, 70, 320, 20, ST77XX_BLACK);
  tft.setTextColor(ST77XX_RED); tft.setCursor(10, 70); tft.print(bfastCount);
  tft.setTextColor(ST77XX_GREEN); tft.setCursor(110, 70); tft.print(lunchCount);
  tft.setTextColor(ST77XX_ORANGE); tft.setCursor(210, 70); tft.print(dinnerCount);

  tft.fillRect(0, 100, 320, 40, ST77XX_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_RED); tft.setCursor(10, 100); tft.print(bfastMenu);
  tft.setTextColor(ST77XX_GREEN); tft.setCursor(110, 100); tft.print(lunchMenu);
  tft.setTextColor(ST77XX_ORANGE); tft.setCursor(210, 100); tft.print(dinnerMenu);

  tft.fillRect(0, 180, 320, 30, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(70, 180);
  tft.print("Total Meals: "); tft.print(totalCount);
}

void drawSensorPage(float t, float h, int g) {
  drawTimeDate();
  tft.setTextSize(2);
  
  tft.setTextColor(ST77XX_RED); tft.setCursor(10, 60); tft.print("Temp");
  tft.setTextColor(ST77XX_BLUE); tft.setCursor(110, 60); tft.print("Humid");
  tft.setTextColor(ST77XX_YELLOW); tft.setCursor(210, 60); tft.print("Gas");

  tft.fillRect(0, 90, 320, 20, ST77XX_BLACK);
  
  tft.setTextColor(ST77XX_RED); tft.setCursor(10, 90); tft.print(t, 1); tft.print("C");
  tft.setTextColor(ST77XX_BLUE); tft.setCursor(110, 90); tft.print(h, 1); tft.print("%");
  tft.setTextColor(ST77XX_YELLOW); tft.setCursor(210, 90); tft.print(g);

  tft.setTextColor(ST77XX_MAGENTA);
  tft.setCursor(40, 200);
  tft.print("Sponsor: Team Alpha");
}

void drawWarningPage() {
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(3);
  tft.setCursor(30, 100);
  tft.print("WARNING!");
  tft.setCursor(10, 140);
  tft.print("FIRE/GAS DETECTED");
}

// ==========================================
// FIREBASE AUTH & REST API FUNCTIONS
// ==========================================
bool loginFirebase() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    String authUrl = "https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=" + String(firebaseApiKey);
    http.begin(client, authUrl);
    http.addHeader("Content-Type", "application/json");

    String jsonPayload = "{\"email\":\"" + String(firebaseUserEmail) + "\",\"password\":\"" + String(firebaseUserPassword) + "\",\"returnSecureToken\":true}";
    int httpCode = http.POST(jsonPayload);

    if (httpCode == 200) {
      String response = http.getString();
      DynamicJsonDocument doc(2048);
      DeserializationError error = deserializeJson(doc, response);

      if (!error && doc.containsKey("idToken")) {
        firebaseIdToken = doc["idToken"].as<String>();
        tokenAuthTime = millis();
        Serial.println("[Firebase] Auth Successful!");
        http.end();
        return true;
      }
    } else {
      Serial.printf("[Firebase] Auth Failed! Code: %d\n", httpCode);
    }
    http.end();
  }
  return false;
}

void fetchMealsFirebase() {
  if (WiFi.status() == WL_CONNECTED && firebaseIdToken != "") {
    WiFiClientSecure client;
    client.setInsecure(); 
    HTTPClient http;
    
    String url = String(firebaseHost) + "/meal.json?auth=" + firebaseIdToken;
    http.begin(client, url);
    int httpCode = http.GET();
    
    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
        bfastCount = doc["bfast_count"] | 0;
        lunchCount = doc["lunch_count"] | 0;
        dinnerCount = doc["dinner_count"] | 0;
        totalCount = bfastCount + lunchCount + dinnerCount;
        bfastMenu = doc["bfast_menu"] | "-";
        lunchMenu = doc["lunch_menu"] | "-";
        dinnerMenu = doc["dinner_menu"] | "-";
      }
    }
    http.end();
  }
}

void postSensorsFirebase(float t, float h, int g) {
  if (WiFi.status() == WL_CONNECTED && firebaseIdToken != "") {
    WiFiClientSecure client;
    client.setInsecure(); 
    HTTPClient http;
    
    String url = String(firebaseHost) + "/sensors.json?auth=" + firebaseIdToken;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    String jsonBody = "{\"temp\":" + String(t) + ",\"humidity\":" + String(h) + ",\"gas\":" + String(g) + "}";
    http.PUT(jsonBody);
    http.end();
  }
}

void postCookingFirebase() {
  if (WiFi.status() == WL_CONNECTED && firebaseIdToken != "") {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    
    String url = String(firebaseHost) + "/kitchen.json?auth=" + firebaseIdToken;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    http.PUT("{\"status\":\"cooking_done\"}");
    http.end();
    
    tft.fillRect(0, 220, 320, 20, ST77XX_BLACK);
    tft.setCursor(50, 220);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_GREEN);
    tft.print("Cooking Done Message Sent!");
  }
}

void postWarningFirebase(bool state, String message) {
  if (WiFi.status() == WL_CONNECTED && firebaseIdToken != "") {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    
    String url = String(firebaseHost) + "/warning.json?auth=" + firebaseIdToken;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    String jsonBody = "{\"active\":" + String(state ? "true" : "false") + ",\"message\":\"" + message + "\"}";
    http.PUT(jsonBody);
    http.end();
  }
}

// ==========================================
// HARDWARE INITIALIZATION & HELPERS
// ==========================================
void initHardware() {
  dht.begin();
  
  pinMode(TRIG_TAP, OUTPUT); pinMode(ECHO_TAP, INPUT);
  pinMode(TRIG_BIN, OUTPUT); pinMode(ECHO_BIN, INPUT);
  
  pinMode(PIR_PIN, INPUT);
  pinMode(MQ2_PIN, INPUT);
  pinMode(FLAME_PIN, INPUT); 

  // Cook Button with Internal Pull-Up on Pin 13 (Active LOW)
  pinMode(BTN_COOK, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_COOK), isrCookButton, FALLING);

  pinMode(MOTOR_TAP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  servoBin.setPeriodHertz(50); 
  servoBin.attach(SERVO_BIN_PIN, 500, 2400); 
  
  servoEmergency.setPeriodHertz(50);
  servoEmergency.attach(SERVO_EMERG_PIN, 500, 2400);
  
  digitalWrite(MOTOR_TAP_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  
  servoBin.write(0);        
  servoEmergency.write(0);  
}

int getDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  long duration = pulseIn(echoPin, HIGH, 30000); 
  if (duration == 0) return 999; 
  return duration * 0.034 / 2;
}
