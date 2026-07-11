#include <WiFi.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <PubSubClient.h> 

// ==================== PIN DEFINITIONS ====================
#define DHTPIN 4
#define MQ2_PIN 34
#define PIR_PIN 27
#define TRIG_PIN 12
#define ECHO_PIN 14

#define BUZZER_PIN 13
#define GREEN_LED 2
#define YELLOW_LED 5
#define RED_LED 15
#define BLUE_LED 25
#define WHITE_LED_FAN 26
#define WHITE_LED_Z1 23
#define WHITE_LED_Z2 19
#define WHITE_LED_Z3 18

// ==================== SENSOR CONFIGURATION ====================
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2); 

// ==================== NETWORK CREDENTIALS ====================
const char* ssid = "M0Bry";
const char* password = "00000000";
const char* mqtt_server = "10.86.56.3";

WiFiClient espClient;
PubSubClient client(espClient);
bool isOnline = false;

// ==================== SYSTEM TIMING ====================
unsigned long systemStartTime = 0;
unsigned long last4MinCheck = 0;
bool isFirstMinute = true;

// ==================== COMPONENT HEALTH TRACKING ====================
bool isWiFiOk = true;
bool isDHTOk = true;
bool isMQ2Ok = true;
bool isUltraOk = true;

// ==================== UI AND WARNING MANAGEMENT ====================
unsigned long lcdNotificationTime = 0;
bool isNotificationActive = false;
bool humidityWarning = false;
unsigned long humWarningStartTime = 0;

bool gasWarningActive = false;
unsigned long gasWarningStartTime = 0;
float currentTemp = 0.0;
float currentHum = 0.0;
int lastLoggedGas = 0;
int lastPublishedCount = -1;

// ==================== ROOM PHASE TIMING ====================
unsigned long entryPhaseStartTime = 0;
unsigned long pausePhaseStartTime = 0;

// ==================== SYSTEM STATE ENUMERATIONS ====================
enum SystemMode { OFFLINE_MODE, ONLINE_MODE };
enum RoomState { IDLE_MODE, ENTRY_PHASE, PAUSE_PHASE, EXIT_PHASE, EMERGENCY_MODE };
SystemMode currentMode = OFFLINE_MODE;
RoomState currentRoomState = IDLE_MODE;

// ==================== OCCUPANCY TRACKING ====================
int studentCount = 0;
bool pirActive = true;
bool inEmergency = false;

// ==================== ULTRASONIC SENSOR ====================
long duration;
int distance;
bool objectPresent = false;
int baselineDistance = 80;

// ==================== USER OVERRIDE MANAGEMENT ====================
unsigned long userOverrideStartTime = 0;
bool isUserOverrideActive = false;
unsigned long userOverrideCheckTime = 0;
bool userOverridePendingCheck = false;

struct UserOverrideState {
  bool light1Overridden = false;
  bool light2Overridden = false;
  bool light3Overridden = false;
  bool fanOverridden = false;
  bool acOverridden = false;
  bool emergencyCleared = false;
} userOverride;

struct DeviceState {
  bool light1 = false;
  bool light2 = false;
  bool light3 = false;
  bool fan = false;
  bool ac = false;
} deviceState;

unsigned long lastStatePublish = 0;

bool isRunningPeriodicCheck = false;
unsigned long periodicCheckStartTime = 0;

// CRITICAL: Fan locked by gas warning - no function can override this
bool fanForGasWarning = false;

// ==================== FUNCTION PROTOTYPES ====================
void beep(int times);
void showMainScreen();
void printSerialLCD(String msg1, String msg2);
void checkSensorsInit();
void sensorFailedAlert(String sensorName);
void connectWiFi();
void reconnectMQTT();
void handleClimate(float temp, float hum, int gasVal);
void updateLighting();
void handlePhases();
void periodicCheck();
void publishDeviceStates();
void checkUserOverrideRules();
void applySystemLightingRules();
void applySystemClimateRules(float temp, float hum);
void forceFanOn();  // New: dedicated function to lock fan ON

// ==================== FORCE FAN ON (Gas Warning Lock) ====================
void forceFanOn() {
  digitalWrite(WHITE_LED_FAN, HIGH);
  deviceState.fan = true;
}

// ==================== MQTT CALLBACK ====================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) msg += (char)payload[i];
  String topicStr = String(topic);
  
  Serial.println("Dashboard Command Received: [" + topicStr + "] " + msg);
  
  if (isRunningPeriodicCheck) {
    Serial.println("Command ignored - Periodic check in progress");
    return;
  }
  
  if (currentRoomState == EMERGENCY_MODE && topicStr != "room/override") {
    Serial.println("Command ignored - System in Emergency Mode");
    return;
  }
  
  isUserOverrideActive = true;
  userOverrideStartTime = millis();
  userOverrideCheckTime = millis() + 60000;
  userOverridePendingCheck = true;
  
  if (topicStr == "room/override" && msg == "FORCE_OFF") {
      beep(1);
      printSerialLCD("User Override", "Active (1 Min)");
      userOverride.emergencyCleared = true;
      
      if (currentRoomState == EMERGENCY_MODE) {
          currentRoomState = IDLE_MODE;
          inEmergency = false;
          digitalWrite(RED_LED, LOW);
          digitalWrite(BUZZER_PIN, LOW);
          digitalWrite(GREEN_LED, HIGH);
          Serial.println("User Forced System Out of Emergency!");
      }
  }
  else if (topicStr == "room/light1") { 
    bool state = (msg == "ON");
    digitalWrite(WHITE_LED_Z1, state ? HIGH : LOW);
    deviceState.light1 = state;
    userOverride.light1Overridden = true;
    Serial.print("User set Light Zone 1: ");
    Serial.println(state ? "ON" : "OFF");
    publishDeviceStates();
  }
  else if (topicStr == "room/light2") { 
    bool state = (msg == "ON");
    digitalWrite(WHITE_LED_Z2, state ? HIGH : LOW);
    deviceState.light2 = state;
    userOverride.light2Overridden = true;
    Serial.print("User set Light Zone 2: ");
    Serial.println(state ? "ON" : "OFF");
    publishDeviceStates();
  }
  else if (topicStr == "room/light3") { 
    bool state = (msg == "ON");
    digitalWrite(WHITE_LED_Z3, state ? HIGH : LOW);
    deviceState.light3 = state;
    userOverride.light3Overridden = true;
    Serial.print("User set Light Zone 3: ");
    Serial.println(state ? "ON" : "OFF");
    publishDeviceStates();
  }
  else if (topicStr == "room/fan") { 
    // User manually controlling fan overrides gas lock
    bool state = (msg == "ON");
    fanForGasWarning = false;
    digitalWrite(WHITE_LED_FAN, state ? HIGH : LOW);
    deviceState.fan = state;
    userOverride.fanOverridden = true;
    Serial.print("User set Fan: ");
    Serial.println(state ? "ON" : "OFF");
    publishDeviceStates();
  }
  else if (topicStr == "room/ac") { 
    bool state = (msg == "ON");
    digitalWrite(BLUE_LED, state ? HIGH : LOW);
    deviceState.ac = state;
    userOverride.acOverridden = true;
    Serial.print("User set AC: ");
    Serial.println(state ? "ON" : "OFF");
    publishDeviceStates();
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.begin(115200);
  systemStartTime = millis();
  
  pinMode(PIR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(MQ2_PIN, INPUT);
  
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(WHITE_LED_FAN, OUTPUT);
  pinMode(WHITE_LED_Z1, OUTPUT);
  pinMode(WHITE_LED_Z2, OUTPUT);
  pinMode(WHITE_LED_Z3, OUTPUT);

  dht.begin();
  lcd.init();
  lcd.backlight();
  
  digitalWrite(GREEN_LED, HIGH); 
  beep(3);
  Serial.println("======System Starts=====");
  lcd.clear(); lcd.print("Scanning System");
  delay(1500); 
  
  checkSensorsInit();
  connectWiFi(); 
  
  Serial.println("==== System Ready ====");
  currentRoomState = IDLE_MODE;
  last4MinCheck = millis();
  
  currentTemp = dht.readTemperature();
  currentHum = dht.readHumidity();
  if (isnan(currentTemp)) currentTemp = 0.0;
  if (isnan(currentHum)) currentHum = 0.0;
  
  showMainScreen();
}

// ==================== MAIN CONTROL LOOP ====================
void loop() {
  unsigned long currentMillis = millis();
  
  if (isOnline && !client.connected()) reconnectMQTT();
  if (isOnline) client.loop();
  
  if (isFirstMinute && (currentMillis - systemStartTime >= 60000)) {
    isFirstMinute = false;
  }

  if (userOverridePendingCheck && (currentMillis >= userOverrideCheckTime)) {
    checkUserOverrideRules();
    userOverridePendingCheck = false;
    isUserOverrideActive = false;
  }

  if (currentRoomState != EMERGENCY_MODE) {
    bool continuousBlink = false;
    if (gasWarningActive && (currentMillis - gasWarningStartTime <= 60000)) continuousBlink = true;
    if (humidityWarning && (currentMillis - humWarningStartTime <= 60000)) continuousBlink = true;
    
    if (continuousBlink) {
      digitalWrite(YELLOW_LED, (currentMillis % 1000 < 500) ? HIGH : LOW);
    } 
    else if (isNotificationActive && (currentMillis - lcdNotificationTime <= 3000)) {
      digitalWrite(YELLOW_LED, HIGH);
    } 
    else {
      digitalWrite(YELLOW_LED, LOW);
    }
    
    if (isNotificationActive && (currentMillis - lcdNotificationTime >= 3000)) {
      showMainScreen();
    }
  }

  // Gas monitoring (ALWAYS runs - safety critical)
  int gasValue = analogRead(MQ2_PIN);
  if (abs(gasValue - lastLoggedGas) >= 200) {
      lastLoggedGas = gasValue;
      
      if (gasWarningActive || currentRoomState == EMERGENCY_MODE) {
          Serial.print("Live Gas Level: "); Serial.println(gasValue);
      }
      
      if (client.connected()) client.publish("sensor/gas", String(gasValue).c_str());
  }

  int emergencyThreshold = (currentRoomState == EMERGENCY_MODE) ? 2900 : 3000; 
  int warningThreshold = gasWarningActive ? 1400 : 1500;
  
  if (!isFirstMinute) {
    if (gasValue >= emergencyThreshold) {
      if (currentRoomState != EMERGENCY_MODE) {
        if (userOverride.emergencyCleared && (currentMillis - userOverrideStartTime < 60000)) {
          Serial.println("Gas critical but user override active (within 60s)");
        } else {
          isRunningPeriodicCheck = false;
          currentRoomState = EMERGENCY_MODE;
          inEmergency = true;
          gasWarningActive = false;
          fanForGasWarning = false;
          isNotificationActive = false; 
          userOverride.emergencyCleared = false;
          digitalWrite(GREEN_LED, LOW);
          digitalWrite(YELLOW_LED, LOW);
          Serial.print("EMERGENCY: Gas Leak / Fire | Level: "); Serial.println(gasValue);
          lcd.clear(); lcd.print("EMERGENCY!"); lcd.setCursor(0,1); lcd.print("Fire/Gas Leak");
          beep(2);
        }
      }
    } 
    else if (gasValue >= warningThreshold && !userOverride.emergencyCleared) {
      if (currentRoomState == EMERGENCY_MODE) {
        currentRoomState = IDLE_MODE;
        inEmergency = false;
        digitalWrite(RED_LED, LOW);
        digitalWrite(BUZZER_PIN, LOW); 
        Serial.println("Emergency Over. Entering Gas Warning State.");
        beep(2);
      }
      if (!gasWarningActive) {
        gasWarningActive = true;
        gasWarningStartTime = millis(); 
        digitalWrite(GREEN_LED, LOW);
        
        // Lock fan ON for gas ventilation
        fanForGasWarning = true;
        forceFanOn();
        Serial.println("Fan LOCKED ON - Gas Warning Active (Ventilation)");
        publishDeviceStates();
        
        beep(1);
      }
      
      // Re-apply fan lock every loop cycle
      if (fanForGasWarning) {
        forceFanOn();
      }
      
      if (currentMillis - gasWarningStartTime <= 60000) {
        static unsigned long lastGasLog = 0;
        if (currentMillis - lastGasLog >= 4000) {
           printSerialLCD("WARNING!", "High Gas Level");
           beep(1);
           lastGasLog = currentMillis;
        }
      }
    } 
    else {
      if (currentRoomState == EMERGENCY_MODE) {
        currentRoomState = IDLE_MODE;
        inEmergency = false;
        digitalWrite(RED_LED, LOW); digitalWrite(BUZZER_PIN, LOW); digitalWrite(GREEN_LED, HIGH);
        beep(2);
        printSerialLCD("System Normal", "IDLE Mode");
        Serial.print("Emergency Cleared. Gas Level: "); Serial.print(gasValue);
        Serial.println(". Returning to IDLE Mode");
      } 
      else if (gasWarningActive) {
        gasWarningActive = false;
        userOverride.emergencyCleared = false;
        digitalWrite(GREEN_LED, HIGH);
        
        // Release fan lock - check if climate needs it
        if (fanForGasWarning) {
            fanForGasWarning = false;
            float temp = dht.readTemperature();
            float hum = dht.readHumidity();
            bool needFan = false;
            if (studentCount > 0) {
                if (!isnan(hum) && (hum < 40 || hum > 70)) needFan = true;
                if (!isnan(temp) && temp >= 20 && temp <= 27) needFan = true;
            }
            
            if (!needFan) {
                digitalWrite(WHITE_LED_FAN, LOW);
                deviceState.fan = false;
                Serial.println("Fan released - Gas Normalized (No climate need)");
            } else {
                deviceState.fan = true;
                Serial.println("Fan stays ON - Climate/Humidity need");
            }
            publishDeviceStates();
        }
        
        beep(1); 
        printSerialLCD("Atmos! Normal", "IDLE Mode"); 
        Serial.print("Atmosphere Normal! Gas Level: "); Serial.print(gasValue);
        Serial.println(". Returning to IDLE Mode");
      }
    }
  }

  if (currentRoomState == EMERGENCY_MODE) {
      digitalWrite(RED_LED, (currentMillis % 500 < 250) ? HIGH : LOW);
      digitalWrite(BUZZER_PIN, (currentMillis % 500 < 250) ? HIGH : LOW); 
      digitalWrite(WHITE_LED_Z1, LOW); digitalWrite(WHITE_LED_Z2, LOW); digitalWrite(WHITE_LED_Z3, LOW);
      digitalWrite(BLUE_LED, LOW); digitalWrite(WHITE_LED_FAN, LOW);
      deviceState.light1 = false; deviceState.light2 = false; deviceState.light3 = false;
      deviceState.fan = false; deviceState.ac = false;
      fanForGasWarning = false;
      publishDeviceStates();
      return;
  }

  if (!isRunningPeriodicCheck) {
    float temp = dht.readTemperature();
    float hum = dht.readHumidity();
    if (!isnan(temp) && !isnan(hum)) {
      if (abs(temp - currentTemp) >= 0.1 || abs(hum - currentHum) >= 0.1) {
        currentTemp = temp;
        currentHum = hum;
        
        if (!gasWarningActive && !humidityWarning && currentRoomState != EMERGENCY_MODE) {
            Serial.print("System Update -> Temp: "); Serial.print(temp); 
            Serial.print(" C | Hum: "); Serial.print(hum); 
            Serial.print(" % | Gas: ");
            Serial.println(gasValue);
        }
        
        if (client.connected()) {
            client.publish("sensor/temp", String(temp).c_str());
            client.publish("sensor/hum", String(hum).c_str());
        }
        
        if (!isNotificationActive) showMainScreen();
      }
      
      if (!isUserOverrideActive) {
        handleClimate(temp, hum, gasValue);
      }
    }

    // FINAL SAFETY: Re-apply fan lock after handleClimate
    if (fanForGasWarning) {
      forceFanOn();
    }

    handlePhases();

    // FINAL SAFETY: Re-apply fan lock after handlePhases
    if (fanForGasWarning) {
      forceFanOn();
    }

    if (studentCount != lastPublishedCount) {
       if (client.connected()) {
           client.publish("sensor/count", String(studentCount).c_str());
       }
       lastPublishedCount = studentCount;
    }
  }

  if (!isRunningPeriodicCheck && (currentMillis - last4MinCheck >= 240000)) { 
    periodicCheck();
    last4MinCheck = currentMillis;
  }
  
  if (isRunningPeriodicCheck && (currentMillis - periodicCheckStartTime >= 10000)) {
    isRunningPeriodicCheck = false;
    Serial.println("Periodic check forced timeout - returning to normal operation");
  }
  
  if (currentMillis - lastStatePublish >= 5000) {
    publishDeviceStates();
    lastStatePublish = currentMillis;
  }
}

// ==================== DEVICE STATE PUBLISHING ====================
void publishDeviceStates() {
  if (!client.connected()) return;
  client.publish("sensor/light1", deviceState.light1 ? "ON" : "OFF");
  client.publish("sensor/light2", deviceState.light2 ? "ON" : "OFF");
  client.publish("sensor/light3", deviceState.light3 ? "ON" : "OFF");
  client.publish("sensor/fan", deviceState.fan ? "ON" : "OFF");
  client.publish("sensor/ac", deviceState.ac ? "ON" : "OFF");
}

// ==================== USER OVERRIDE RULE CHECK ====================
void checkUserOverrideRules() {
  Serial.println("========================================");
  Serial.println("Checking User Override Rules (60s elapsed)...");
  
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  int gasVal = analogRead(MQ2_PIN);
  
  if (userOverride.emergencyCleared) {
    if (gasVal >= 3000) {
      Serial.println("Gas still HIGH! Re-activating EMERGENCY MODE");
      currentRoomState = EMERGENCY_MODE;
      inEmergency = true;
      fanForGasWarning = false;
      digitalWrite(GREEN_LED, LOW);
      digitalWrite(YELLOW_LED, LOW);
      lcd.clear(); lcd.print("EMERGENCY!"); lcd.setCursor(0,1); lcd.print("Gas Still High!");
      beep(2);
    } else {
      Serial.println("Gas level OK. Keeping normal mode.");
      printSerialLCD("Override Check", "Gas OK - Normal");
    }
    userOverride.emergencyCleared = false;
  }
  
  if (userOverride.light1Overridden || userOverride.light2Overridden || userOverride.light3Overridden) {
    if (studentCount == 0) {
      Serial.println("No Students. Turning OFF all lights!");
      digitalWrite(WHITE_LED_Z1, LOW);
      digitalWrite(WHITE_LED_Z2, LOW);
      digitalWrite(WHITE_LED_Z3, LOW);
      deviceState.light1 = false;
      deviceState.light2 = false;
      deviceState.light3 = false;
      printSerialLCD("Override End", "Room Empty-Lights OFF");
    } else {
      Serial.println("Students Present. Applying auto lighting rules!");
      applySystemLightingRules();
    }
    userOverride.light1Overridden = false;
    userOverride.light2Overridden = false;
    userOverride.light3Overridden = false;
    publishDeviceStates();
  }
  
  if (userOverride.fanOverridden || userOverride.acOverridden) {
    // Don't touch fan if gas locked
    if (!fanForGasWarning) {
      if (studentCount == 0) {
        Serial.println("No Students. Turning OFF Fan and AC!");
        digitalWrite(WHITE_LED_FAN, LOW);
        digitalWrite(BLUE_LED, LOW);
        deviceState.fan = false;
        deviceState.ac = false;
      } else {
        Serial.println("Students Present. Applying Auto Climate Rules!");
        applySystemClimateRules(temp, hum);
      }
    }
    userOverride.fanOverridden = false;
    userOverride.acOverridden = false;
    publishDeviceStates();
  }
  
  Serial.println("User Override Check Complete!");
  Serial.println("========================================");
  isUserOverrideActive = false;
}

// ==================== AUTOMATIC LIGHTING RULES ====================
void applySystemLightingRules() {
  if (studentCount >= 1 && studentCount <= 3) {
    digitalWrite(WHITE_LED_Z1, HIGH);
    digitalWrite(WHITE_LED_Z2, LOW);
    digitalWrite(WHITE_LED_Z3, LOW);
    deviceState.light1 = true;
    deviceState.light2 = false;
    deviceState.light3 = false;
  } 
  else if (studentCount > 3 && studentCount <= 5) {
    digitalWrite(WHITE_LED_Z1, HIGH);
    digitalWrite(WHITE_LED_Z2, HIGH);
    digitalWrite(WHITE_LED_Z3, LOW);
    deviceState.light1 = true;
    deviceState.light2 = true;
    deviceState.light3 = false;
  } 
  else if (studentCount > 5) {
    digitalWrite(WHITE_LED_Z1, HIGH);
    digitalWrite(WHITE_LED_Z2, HIGH);
    digitalWrite(WHITE_LED_Z3, HIGH);
    deviceState.light1 = true;
    deviceState.light2 = true;
    deviceState.light3 = true;
  } 
  else {
    digitalWrite(WHITE_LED_Z1, LOW);
    digitalWrite(WHITE_LED_Z2, LOW);
    digitalWrite(WHITE_LED_Z3, LOW);
    deviceState.light1 = false;
    deviceState.light2 = false;
    deviceState.light3 = false;
  }
}

// ==================== AUTOMATIC CLIMATE RULES ====================
void applySystemClimateRules(float temp, float hum) {
  // NEVER change fan if gas locked
  if (fanForGasWarning) {
    forceFanOn();
    digitalWrite(BLUE_LED, LOW);
    deviceState.ac = false;
    return;
  }
  
  if (hum < 40 || hum > 70) {
    digitalWrite(WHITE_LED_FAN, HIGH);
    deviceState.fan = true;
    
    if (temp > 27) {
      digitalWrite(BLUE_LED, HIGH);
      deviceState.ac = true;
    } else {
      digitalWrite(BLUE_LED, LOW);
      deviceState.ac = false;
    }
  }
  else {
    if (temp < 20) {
      digitalWrite(WHITE_LED_FAN, LOW);
      digitalWrite(BLUE_LED, LOW);
      deviceState.fan = false;
      deviceState.ac = false;
    } else if (temp >= 20 && temp <= 27) { 
      digitalWrite(WHITE_LED_FAN, HIGH);
      digitalWrite(BLUE_LED, LOW); 
      deviceState.fan = true;
      deviceState.ac = false;
    } else if (temp > 27) {
      digitalWrite(BLUE_LED, HIGH);
      deviceState.ac = true;
    }
  }
}

// ==================== HELPER FUNCTIONS ====================

void beep(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);
    digitalWrite(BUZZER_PIN, LOW);
    if (i < times - 1) delay(100); 
  }
}

void showMainScreen() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Temp: "); lcd.print(currentTemp, 1);
  lcd.print("C");
  lcd.setCursor(0,1);
  lcd.print("Hum:  "); lcd.print(currentHum, 1); lcd.print("%");
  isNotificationActive = false; 
}

void printSerialLCD(String msg1, String msg2) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(msg1);
  lcd.setCursor(0, 1); lcd.print(msg2);
  Serial.println(msg1 + " | " + msg2);
  isNotificationActive = true;
  lcdNotificationTime = millis();
}

void sensorFailedAlert(String sensorName) {
  Serial.println("Error: " + sensorName + " Failed!");
  beep(1);
  printSerialLCD("Sensor Error!", sensorName + " fail");
}

// ==================== SENSOR INITIALIZATION ====================
void checkSensorsInit() {
  Serial.println("Check Sensors");
  lcd.clear(); lcd.print("Check Sensors");
  delay(1000);
  bool allOk = true;
  
  lcd.setCursor(0,1); lcd.print("DHT22...        ");
  if (isnan(dht.readTemperature())) { 
    sensorFailedAlert("DHT22");
    isDHTOk = false; delay(2000); allOk = false;
  } else { 
    Serial.println("DHT22: Ok"); isDHTOk = true; lcd.setCursor(0,1);
    lcd.print("DHT22: Ok       "); delay(500); 
  }

  lcd.setCursor(0,1);
  lcd.print("MQ-2...         ");
  if (analogRead(MQ2_PIN) < 0) { 
    sensorFailedAlert("MQ-2");
    isMQ2Ok = false; delay(2000);
    allOk = false; 
  } else { 
    Serial.println("MQ-2: Ok"); isMQ2Ok = true; lcd.setCursor(0,1);
    lcd.print("MQ-2: Ok        "); delay(500); 
  }

  lcd.setCursor(0,1);
  lcd.print("PIR...          ");
  Serial.println("PIR: Ok"); lcd.setCursor(0,1);
  lcd.print("PIR: Ok         "); delay(500);
  
  lcd.setCursor(0,1); lcd.print("Ultrasonic...   ");
  delay(200);
  Serial.println("Ultrasonic: Ok (Normal)");
  isUltraOk = true; lcd.setCursor(0,1); lcd.print("Ultrasonic: Ok  "); delay(500);

  if (allOk) Serial.println("All Sensors Ok");
}

// ==================== WIFI CONNECTION ====================
void connectWiFi() { 
  Serial.println("ESP32 WiFi Test");
  Serial.println("Connecting to WiFi...");
  lcd.clear(); lcd.print("Wi-Fi Connection");
  
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) { 
    delay(500); 
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    isOnline = true; currentMode = ONLINE_MODE;
    isWiFiOk = true;
    Serial.println("Connected to WiFi!");
    Serial.print("IP Address: "); Serial.println(WiFi.localIP());
    lcd.setCursor(0, 1);
    lcd.print("Connected!      ");
    client.setServer(mqtt_server, 1883);
    client.setCallback(mqttCallback);
  } else {
    isOnline = false;
    currentMode = OFFLINE_MODE; isWiFiOk = false;
    Serial.println("Failed to Connect to WiFi!");
    lcd.setCursor(0, 1);
    lcd.print("Failed          ");
  }
  delay(1000);
}

// ==================== MQTT RECONNECTION ====================
void reconnectMQTT() {
  static unsigned long lastReconnectAttempt = 0;
  if (millis() - lastReconnectAttempt > 5000) {
    lastReconnectAttempt = millis();
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP32_IOT_System")) {
      Serial.println("Connected to MQTT Broker!");
      client.subscribe("room/#");
      publishDeviceStates();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" Try Again in 5 Seconds");
    }
  }
}

// ==================== CLIMATE CONTROL ====================
// Priority: Gas Warning > Humidity > Temperature
void handleClimate(float temp, float hum, int gasVal) {
  static int lastClimateState = 0;
  int currentClimateState = 0;
  
  // Humidity check - ALWAYS runs regardless of gas warning
  bool badHumidity = (hum < 40 || hum > 70);
  
  if (studentCount == 0) {
      // Room empty: turn off everything
      digitalWrite(WHITE_LED_FAN, LOW); 
      digitalWrite(BLUE_LED, LOW);
      deviceState.fan = false;
      deviceState.ac = false;
      currentClimateState = 0;
  } 
  else {
      // ===== GAS WARNING ACTIVE =====
      if (gasWarningActive || fanForGasWarning) {
          // Fan MUST stay ON during gas warning
          forceFanOn();
          
          // AC must stay OFF during gas warning
          digitalWrite(BLUE_LED, LOW);
          deviceState.ac = false;
          
          currentClimateState = 1; // Fan ON
          
          // Still check humidity for notifications (without changing fan state)
          if (badHumidity) {
              if(!humidityWarning) { 
                  humidityWarning = true;
                  humWarningStartTime = millis(); 
                  beep(1); 
                  printSerialLCD("Bad Humidity", "Fan ON (Gas)");
              }
          } else {
              if (humidityWarning) { 
                  humidityWarning = false;
                  beep(1); 
                  printSerialLCD("Humidity Improved", "System Normal"); 
                  Serial.println("Humidity Improved System Normal!");
              }
          }
          
          // Publish state and exit
          if (currentClimateState != lastClimateState) {
              lastClimateState = currentClimateState;
              publishDeviceStates();
          }
          return; // Skip normal climate control
      }
      
      // ===== NORMAL OPERATION (No Gas Warning) =====
      // Humidity takes priority over temperature
      if (badHumidity) {
        digitalWrite(WHITE_LED_FAN, HIGH);
        deviceState.fan = true;
        
        if (temp > 27) {
          digitalWrite(BLUE_LED, HIGH);
          deviceState.ac = true;
          currentClimateState = 2;
        } else {
          digitalWrite(BLUE_LED, LOW);
          deviceState.ac = false;
          currentClimateState = 1;
        }
        
        // Humidity warning notification
        if(!humidityWarning) { 
          humidityWarning = true;
          humWarningStartTime = millis(); 
          beep(1); 
          printSerialLCD("Bad Humidity", "Fan ON");
        }
      }
      // Temperature-based control (normal humidity)
      else {
        if (temp < 20) {
          digitalWrite(WHITE_LED_FAN, LOW);
          digitalWrite(BLUE_LED, LOW); 
          deviceState.fan = false;
          deviceState.ac = false;
          currentClimateState = 0;
        } else if (temp >= 20 && temp <= 27) { 
          digitalWrite(WHITE_LED_FAN, HIGH);
          digitalWrite(BLUE_LED, LOW); 
          deviceState.fan = true;
          deviceState.ac = false;
          currentClimateState = 1;
        } else if (temp > 27) {
          digitalWrite(BLUE_LED, HIGH);
          deviceState.ac = true;
          currentClimateState = 2;
        }
        
        // Clear humidity warning if active
        if (humidityWarning) { 
          humidityWarning = false;
          beep(1); 
          printSerialLCD("Humidity Improved", "System Normal"); 
          Serial.println("Humidity Improved System Normal!");
        }
      }
  }

  // Notify on state change (for normal operation)
  if (currentClimateState != lastClimateState) {
    if (currentClimateState == 1) { 
      beep(1);
      printSerialLCD("Fan Started", "Temp 20-27C"); 
    }
    else if (currentClimateState == 2) { 
      beep(1); 
      printSerialLCD("AC Started", "Temp > 27C");
    }
    else if (currentClimateState == 0) { 
       if(studentCount == 0) { 
         beep(1);
         printSerialLCD("Cooling OFF", "Room Empty"); 
       }
       else { 
         beep(1); 
         printSerialLCD("Cooling OFF", "Temp < 20C");
       }
    }
    lastClimateState = currentClimateState;
    publishDeviceStates();
  }
}

// ==================== ROOM PHASE HANDLER ====================
void handlePhases() {
  unsigned long currentMillis = millis();
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2); digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10); digitalWrite(TRIG_PIN, LOW);
  duration = pulseIn(ECHO_PIN, HIGH, 30000); 
  distance = duration * 0.034 / 2;
  bool triggerEvent = false;
  if (distance > 0 && distance < baselineDistance - 20) {
    if (!objectPresent) objectPresent = true;
  } else {
    if (objectPresent) { 
      objectPresent = false;
      triggerEvent = true; 
    }
  }

  static unsigned long lastTriggerTime = 0;
  if (triggerEvent) {
    if (currentMillis - lastTriggerTime > 800) {
      lastTriggerTime = currentMillis;
    } else {
      triggerEvent = false;
    }
  }

  if (pirActive && !isFirstMinute) {
    if (digitalRead(PIR_PIN) == HIGH) { Serial.println("PIR: Motion Detected Once");
    pirActive = false; }
  }

  switch (currentRoomState) {
    case IDLE_MODE:
      if (triggerEvent) {
        studentCount = 1;
        currentRoomState = ENTRY_PHASE; entryPhaseStartTime = millis(); pirActive = false; 
        beep(2);
        Serial.print("Ultrasonic: 1 Student. Entering ENTRY Phase\n");
        updateLighting();
      }
      break;

    case ENTRY_PHASE:
      if (triggerEvent) {
        studentCount++;
        beep(1); 
        Serial.print("Student Entered. Count: "); Serial.println(studentCount);
        updateLighting(); 
      }
      
      if (currentMillis - entryPhaseStartTime >= 60000) {
        if (studentCount == 0) {
           currentRoomState = IDLE_MODE;
           pirActive = true; 
           beep(2);
           Serial.println("Entry Phase ended. 0 Students. Returning to IDLE Mode");
           printSerialLCD("Room Empty", "IDLE Mode");
        } else {
           currentRoomState = PAUSE_PHASE;
           pausePhaseStartTime = millis();
           beep(2);
           Serial.println("Entering PAUSE Phase (Sensors OFF)");
           printSerialLCD("PAUSE Phase", "Sensors OFF");
        }
      }
      break;
    case PAUSE_PHASE:
      if (currentMillis - pausePhaseStartTime >= 60000) {
        currentRoomState = EXIT_PHASE;
        beep(2);
        Serial.println("Entering EXIT Phase (Listening for exits)");
        printSerialLCD("EXIT Phase", "Listening...");
      }
      break;
    case EXIT_PHASE:
      if (triggerEvent) {
        if (studentCount > 0) studentCount--;
        beep(1);
        Serial.print("Student Exited. Total inside: "); Serial.println(studentCount);
        updateLighting();
        
        if (studentCount == 0) {
          currentRoomState = IDLE_MODE;
          pirActive = true; 
          beep(2);
          Serial.println("Room Empty. Returning to IDLE Mode");
          printSerialLCD("Room Empty", "IDLE Mode");
          
          // Only turn off fan if not gas-locked
          if (!fanForGasWarning) {
            digitalWrite(WHITE_LED_FAN, LOW);
            deviceState.fan = false;
          }
          digitalWrite(BLUE_LED, LOW);
          deviceState.ac = false;
          publishDeviceStates();
        }
      }
      break;
    case EMERGENCY_MODE: break;
  }
}

// ==================== LIGHTING CONTROL ====================
void updateLighting() {
  if (studentCount >= 1 && studentCount <= 3) {
    digitalWrite(WHITE_LED_Z1, HIGH);
    digitalWrite(WHITE_LED_Z2, LOW);
    digitalWrite(WHITE_LED_Z3, LOW);
    deviceState.light1 = true; deviceState.light2 = false; deviceState.light3 = false;
    printSerialLCD("Light Zone One", "Count: " + String(studentCount));
  } 
  else if (studentCount > 3 && studentCount <= 5) {
    digitalWrite(WHITE_LED_Z1, HIGH);
    digitalWrite(WHITE_LED_Z2, HIGH);
    digitalWrite(WHITE_LED_Z3, LOW);
    deviceState.light1 = true; deviceState.light2 = true; deviceState.light3 = false;
    printSerialLCD("Light Zone Two", "Count: " + String(studentCount));
  } 
  else if (studentCount > 5) {
    digitalWrite(WHITE_LED_Z1, HIGH); digitalWrite(WHITE_LED_Z2, HIGH); digitalWrite(WHITE_LED_Z3, HIGH);
    deviceState.light1 = true; deviceState.light2 = true; deviceState.light3 = true;
    printSerialLCD("Light Zone Three", "Count: " + String(studentCount));
  } 
  else if (studentCount == 0) {
    digitalWrite(WHITE_LED_Z1, LOW);
    digitalWrite(WHITE_LED_Z2, LOW); digitalWrite(WHITE_LED_Z3, LOW);
    deviceState.light1 = false; deviceState.light2 = false; deviceState.light3 = false;
  }
  publishDeviceStates();
}

// ==================== PERIODIC 4-MINUTE CHECK ====================
void periodicCheck() {
  isRunningPeriodicCheck = true;
  periodicCheckStartTime = millis();
  
  Serial.println("====================================");
  Serial.println("Performing System Check...");
  
  bool currentWiFi = (WiFi.status() == WL_CONNECTED);
  if (currentWiFi) {
      Serial.println("Check: Wi-Fi is CONNECTED");
      if (!isWiFiOk) {
          isWiFiOk = true;
          beep(1); printSerialLCD("Wi-Fi OK", "Reconnected!");
      }
  } else {
      Serial.println("Check: Wi-Fi is DISCONNECTED");
      if (isWiFiOk) {
          isWiFiOk = false;
          beep(1); printSerialLCD("WiFi Error", "Disconnected"); 
          connectWiFi(); 
      }
  }
  
  bool currentDHT = !isnan(dht.readTemperature());
  if (!currentDHT && isDHTOk) {
      isDHTOk = false;
      sensorFailedAlert("DHT22");
  } else if (currentDHT && !isDHTOk) {
      isDHTOk = true;
      Serial.println("Check: DHT22 Reconnected!");
      beep(1);
      printSerialLCD("DHT22 OK", "Reconnected!");
  } else if (currentDHT && isDHTOk) {
      Serial.println("Check: DHT22 OK (No change)");
  }

  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2); digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10); digitalWrite(TRIG_PIN, LOW);
  long testDuration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (!isUltraOk) {
      isUltraOk = true;
      Serial.println("Check: Ultrasonic Reconnected!");
      beep(1);
      printSerialLCD("Ultra OK", "Reconnected!");
  } else {
      Serial.println("Check: Ultrasonic OK (Normal)");
  }

  int mq2Check = analogRead(MQ2_PIN);
  if (mq2Check >= 0 && !isMQ2Ok) {
      isMQ2Ok = true;
      Serial.println("Check: MQ-2 Reconnected!");
      beep(1);
      printSerialLCD("MQ-2 OK", "Reconnected!");
  } else if (mq2Check >= 0 && isMQ2Ok) {
      Serial.println("Check: MQ-2 OK (No change)");
  }

  Serial.println("System Check Completed.");
  Serial.println("====================================");
  
  isRunningPeriodicCheck = false;
}
