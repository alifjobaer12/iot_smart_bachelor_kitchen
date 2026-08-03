#include <WiFi.h>
#include <WiFiClientSecure.h> // REQUIRED FOR HTTPS (VERCEL)
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
#define TFT_CS    5
#define TFT_DC    2
#define TFT_RST   15

#define BTN_PAGE  34 // REQUIRES PHYSICAL 10k PULL-DOWN RESISTOR TO GND
#define BTN_COOK  35 // REQUIRES PHYSICAL 10k PULL-DOWN RESISTOR TO GND

#define PIR_PIN   36
#define FLAME_PIN 39
#define MQ2_PIN   32
#define DHT_PIN   33
#define TRIG_TAP  25
#define ECHO_TAP  26
#define TRIG_BIN  27
#define ECHO_BIN  14

#define RELAY_TAP       12 
#define RELAY_FAN       4
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

// ==========================================
// OBJECTS & THREADING
// ==========================================
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
DHT dht(DHT_PIN, DHT11);
Servo servoBin;
Servo servoEmergency;

TaskHandle_t TaskUI;
SemaphoreHandle_t dataMutex;

// ==========================================
// SHARED VARIABLES (Protected by Mutex)
// ==========================================
float sharedTemp = 0.0;
float sharedHum = 0.0;
int sharedGas = 0;
bool isEmergency = false;

int currentPage = 0; 
bool warningTriggered = false;

String bfastMenu = "-", lunchMenu = "-", dinnerMenu = "-";
int bfastCount = 0, lunchCount = 0, dinnerCount = 0, totalCount = 0;

void setup() {
  Serial.begin(115200);
  Serial.println("\n--- ESP32 Smart Project Starting ---");

  dataMutex = xSemaphoreCreateMutex();

  Serial.println("Initializing Hardware (Core 1)...");
  initHardware();

  Serial.println("Starting UI and Network Task (Core 0)...");
  xTaskCreatePinnedToCore(TaskUI_Network, "TaskUI", 10000, NULL, 1, &TaskUI, 0); 
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
    Serial.printf("[Core 1] Temp: %.1fC | Hum: %.1f%% | Gas: %d | Fire: %d | Tap Dist: %dcm | Bin Dist: %dcm\n", 
                  t, h, g, fire, distTap, distBin);
    lastSerialPrint = currentMillis;
  }

  if (emergencyState != lastEmergencyState) {
    if (emergencyState) Serial.println("\n[Core 1] >>> EMERGENCY TRIGGERED! <<<");
    else Serial.println("\n[Core 1] >>> EMERGENCY CLEARED. <<<");
    lastEmergencyState = emergencyState;
  }

  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    if (!isnan(t)) sharedTemp = t;
    if (!isnan(h)) sharedHum = h;
    sharedGas = g;
    isEmergency = emergencyState;
    xSemaphoreGive(dataMutex);
  }

  // Actuators
  bool handDetected = (distTap < DISTANCE_THRESHOLD);
  digitalWrite(RELAY_TAP, (handDetected || fire) ? HIGH : LOW);
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

  digitalWrite(RELAY_FAN, (isHot || gasLeak) ? HIGH : LOW);
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
  
  Serial.print("[Core 0] Connecting to Wi-Fi...");
  WiFi.begin(ssid, password);

  // Try connecting for up to 10 seconds (20 * 500ms)
  int connTimeout = 0;
  while (WiFi.status() != WL_CONNECTED && connTimeout < 20) {
    Serial.print(".");
    vTaskDelay(500 / portTICK_PERIOD_MS);
    connTimeout++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[Core 0] Wi-Fi Connected! IP: " + WiFi.localIP().toString());
    configTime(21600, 0, "pool.ntp.org", "time.nist.gov"); 
    fetchMealsAPI(); 
  } else {
    Serial.println("\n[Core 0] Wi-Fi Connection Failed! Moving forward offline.");
  }
  
  tft.fillScreen(ST77XX_BLACK); // Clear startup text

  unsigned long lastApiTime = 0;
  
  // Software Debounce Trackers
  bool lastBtnPage = LOW;
  bool lastBtnCook = LOW;
  unsigned long debouncePageTimer = 0;
  unsigned long debounceCookTimer = 0;

  for(;;) { 
    // Check Wi-Fi background reconnection if disconnected
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid, password); // Non-blocking background attempt can go here or keep status check
    }

    bool btnPage = digitalRead(BTN_PAGE);
    bool btnCook = digitalRead(BTN_COOK);

    // Debounce Logic 
    if (btnPage == HIGH && lastBtnPage == LOW && (millis() - debouncePageTimer > 300)) {
      currentPage = (currentPage == 0) ? 1 : 0;
      Serial.printf("[Core 0] Page Button Pressed! Switched to Page %d\n", currentPage);
      tft.fillScreen(ST77XX_BLACK);
      debouncePageTimer = millis();
    }
    lastBtnPage = btnPage;

    if (btnCook == HIGH && lastBtnCook == LOW && (millis() - debounceCookTimer > 1000)) {
      Serial.println("[Core 0] Cook Button Pressed! Triggering POST API.");
      postCookingAPI();
      debounceCookTimer = millis();
    }
    lastBtnCook = btnCook;

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

    // Periodic API Posting & Fetching (Only if connected)
    if (WiFi.status() == WL_CONNECTED && (millis() - lastApiTime > 60000)) {
      postSensorsAPI(cT, cH, cG);
      fetchMealsAPI();
      lastApiTime = millis();
    }

    vTaskDelay(200 / portTICK_PERIOD_MS); 
  }
}

// ==========================================
// UI DRAWING FUNCTIONS
// ==========================================
void drawWifiStatusIcon() {
  // Top right corner indicator area (around X: 270 to 310, Y: 2)
  tft.fillRect(260, 2, 55, 18, ST77XX_BLACK); 
  
  if (WiFi.status() == WL_CONNECTED) {
    tft.setTextColor(ST77XX_GREEN);
    tft.setTextSize(1);
    tft.setCursor(265, 5);
    tft.print("WIFI OK");
  } else {
    tft.setTextColor(ST77XX_RED);
    tft.setTextSize(1);
    tft.setCursor(260, 5);
    tft.print("NO WIFI");
  }
}

void drawTimeDate() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeStr[10], dateStr[15];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
    strftime(dateStr, sizeof(dateStr), "%d/%m/%y", &timeinfo);

    tft.fillRect(0, 0, 255, 20, ST77XX_BLACK); // Clear top bar text area (leaving space for wifi icon)
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
  
  // Always update Wi-Fi status symbol on the top bar
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
// API FUNCTIONS (UPDATED FOR HTTPS)
// ==========================================
void fetchMealsAPI() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure(); // Bypass SSL verification for ESP32
    HTTPClient http;
    
    http.begin(client, apiGetMeals);
    int httpCode = http.GET();
    
    if (httpCode > 0) {
      String payload = http.getString();
      Serial.printf("[API] GET Success (Code: %d)\n", httpCode);
      
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, payload);
      
      bfastCount = doc["bfast_count"] | 0;
      lunchCount = doc["lunch_count"] | 0;
      dinnerCount = doc["dinner_count"] | 0;
      totalCount = bfastCount + lunchCount + dinnerCount;
      bfastMenu = doc["bfast_menu"] | "-";
      lunchMenu = doc["lunch_menu"] | "-";
      dinnerMenu = doc["dinner_menu"] | "-";
    } else {
      Serial.printf("[API] GET Failed, Error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
  }
}

void postSensorsAPI(float t, float h, int g) {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure(); 
    HTTPClient http;
    
    http.begin(client, apiPostSensors);
    http.addHeader("Content-Type", "application/json");
    
    String jsonBody = "{\"temp\":" + String(t) + ",\"humidity\":" + String(h) + ",\"gas\":" + String(g) + "}";
    int httpResponseCode = http.POST(jsonBody);
    Serial.printf("[API] POST Sensors Response Code: %d\n", httpResponseCode);
    
    http.end();
  }
}

void postCookingAPI() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    
    http.begin(client, apiPostCooking);
    http.addHeader("Content-Type", "application/json");
    
    int httpResponseCode = http.POST("{\"status\":\"cooking_done\"}");
    Serial.printf("[API] POST Cooking Response Code: %d\n", httpResponseCode);
    
    http.end();
    
    // Quick UI feedback
    tft.fillRect(0, 220, 320, 20, ST77XX_BLACK);
    tft.setCursor(50, 220);
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_GREEN);
    tft.print("Cooking Done Message Sent!");
    delay(1000); 
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

  pinMode(BTN_PAGE, INPUT);
  pinMode(BTN_COOK, INPUT);

  pinMode(RELAY_TAP, OUTPUT);
  pinMode(RELAY_FAN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Allocate hardware timers for ESP32 servos
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  
  servoBin.setPeriodHertz(50); 
  servoBin.attach(SERVO_BIN_PIN, 500, 2400); 
  
  servoEmergency.setPeriodHertz(50);
  servoEmergency.attach(SERVO_EMERG_PIN, 500, 2400);
  
  digitalWrite(RELAY_TAP, LOW);
  digitalWrite(RELAY_FAN, LOW);
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
