#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_AM2320.h>
#include <ESP32Servo.h>
#include <math.h>

// ================= Wi-Fi =================
const char* SSID = "iPhone";
const char* PASS = "goondemon";

// ================= API ===================
const char* API_URL = "https://p5-autonomous-robot-temp.onrender.com/telemetry";
const char* API_KEY = "Liquid-Team";

// ================= Sensors ===============
Adafruit_AM2320 am2320;

#define TRIG_PIN 25
#define ECHO_PIN 14
#define I2C_SDA 32
#define I2C_SCL 33

// Servos
#define LEFT_WHEEL_CTRL  13
#define RIGHT_WHEEL_CTRL 27

Servo leftServo;
Servo rightServo;

// ================= Feedback360 =================
#define LEFT_FB_PIN  26
#define RIGHT_FB_PIN 34

const float DC_MIN = 2.9;
const float DC_MAX = 97.1;
const bool LEFT_MIRRORED = true;  // This is for ENCODER direction, not servo
const int  NOISE_DEADBAND_DEG = 1;

float METERS_PER_DEG = 0.0002009f;
const float WHEEL_BASE_M = 0.140f;

int prevLeftAngle  = -1;
int prevRightAngle = -1;

// ================= Pose =================
float posX = 0.0f;
float posY = 0.0f;
float thetaRad = 0.0f;

// ================= Timing =================
unsigned long tOdom = 0;
unsigned long tSensor = 0;
unsigned long tSend = 0;

const unsigned long ODOM_EVERY_MS = 50;    // 20 Hz
const unsigned long SENSOR_EVERY_MS = 250; // 4 Hz
const unsigned long SEND_EVERY_MS = 2000;  // 0.5 Hz

// ================= Servo Control ==========
const int STOP_US = 1500;
const int DELTA_FWD  = 120;
const int DELTA_BACK = 200;
const int DELTA_TURN = 220;

// ⭐ TRIM VALUES TO FIX DRIFT
// If drifts LEFT: increase LEFT_TRIM or decrease RIGHT_TRIM
// If drifts RIGHT: decrease LEFT_TRIM or increase RIGHT_TRIM
const int LEFT_TRIM_US  = 20;   // Adjust in steps of 5-10
const int RIGHT_TRIM_US = 0;   // Adjust in steps of 5-10

void driveStop() {
  leftServo.writeMicroseconds(STOP_US);
  rightServo.writeMicroseconds(STOP_US);
}

// ⭐ CORRECTED: Standard differential drive (opposite signals)
// Left wheel: higher value = forward
// Right wheel: lower value = forward
void driveForward() {
  leftServo.writeMicroseconds(STOP_US + DELTA_FWD + LEFT_TRIM_US);
  rightServo.writeMicroseconds(STOP_US - DELTA_FWD - RIGHT_TRIM_US);
}

void driveBackward() {
  leftServo.writeMicroseconds(STOP_US - DELTA_BACK - LEFT_TRIM_US);
  rightServo.writeMicroseconds(STOP_US + DELTA_BACK + RIGHT_TRIM_US);
}

// ⭐ CORRECTED: For turning
void turnRight() {
  // Left forward, right backward
  leftServo.writeMicroseconds(STOP_US + DELTA_TURN);
  rightServo.writeMicroseconds(STOP_US + DELTA_TURN);
}

void turnLeft() {
  // Left backward, right forward
  leftServo.writeMicroseconds(STOP_US - DELTA_TURN);
  rightServo.writeMicroseconds(STOP_US - DELTA_TURN);
}

// ================= Sensor Cache =================
float cachedDistCm = -1.0f;
float cachedTemp = NAN;
float cachedHumid = NAN;

float readDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long us = pulseIn(ECHO_PIN, HIGH, 30000);
  if (us == 0) return -1.0;
  
  float cm = (us * 0.0343f) / 2.0f;
  
  // Sanity check
  if (cm < 2.0f || cm > 400.0f) return -1.0f;
  
  return cm;
}

void updateSensors() {
  cachedDistCm = readDistanceCm();
  cachedTemp = am2320.readTemperature();
  cachedHumid = am2320.readHumidity();
}

// ================= Feedback360 =================
int readAngle(int pin) {
  unsigned long highTime = pulseIn(pin, HIGH, 20000);
  unsigned long lowTime  = pulseIn(pin, LOW, 20000);
  unsigned long period = highTime + lowTime;
  if (period == 0) return -1;

  float duty = (100.0f * highTime) / period;
  duty = constrain(duty, DC_MIN, DC_MAX);

  int angle = map((int)(duty * 100.0f),
                  (int)(DC_MIN * 100.0f),
                  (int)(DC_MAX * 100.0f),
                  0, 359);
  return angle;
}

int deltaAngle(int nowA, int prevA) {
  int d = nowA - prevA;
  if (d > 180) d -= 360;
  if (d < -180) d += 360;
  if (abs(d) <= NOISE_DEADBAND_DEG) d = 0;
  return d;
}

void updateOdometry() {
  int LA = readAngle(LEFT_FB_PIN);
  int RA = readAngle(RIGHT_FB_PIN);
  if (LA < 0 || RA < 0) return;

  if (prevLeftAngle < 0 || prevRightAngle < 0) {
    prevLeftAngle = LA;
    prevRightAngle = RA;
    return;
  }

  int dLdeg = deltaAngle(LA, prevLeftAngle);
  int dRdeg = deltaAngle(RA, prevRightAngle);

  prevLeftAngle = LA;
  prevRightAngle = RA;

  // LEFT_MIRRORED refers to the encoder direction, not servo
  if (LEFT_MIRRORED) dLdeg = -dLdeg;

  float dL = dLdeg * METERS_PER_DEG;
  float dR = dRdeg * METERS_PER_DEG;

  float dCenter = (dL + dR) / 2.0f;
  float dTheta  = (dR - dL) / WHEEL_BASE_M;

  posX += dCenter * cosf(thetaRad);
  posY += dCenter * sinf(thetaRad);
  thetaRad += dTheta;

  // Wrap to [-pi, pi]
  if (thetaRad >  (float)M_PI) thetaRad -= 2.0f * (float)M_PI;
  if (thetaRad < -(float)M_PI) thetaRad += 2.0f * (float)M_PI;
}

// ================= Wi-Fi ===================
void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 15000) {
      Serial.println("\n❌ WiFi failed");
      return;
    }
  }

  Serial.println("\n✅ WiFi connected");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

// ================= Telemetry =================
void sendTelemetry() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
  }
  
  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_KEY);
  
  float thetaDeg = thetaRad * 180.0f / (float)M_PI;
  
  String json = "{";
  json += "\"temperature\":" + String(isnan(cachedTemp) ? -999 : cachedTemp, 1) + ",";
  json += "\"humidity\":" + String(isnan(cachedHumid) ? -999 : cachedHumid, 1) + ",";
  json += "\"distance_cm\":" + String(cachedDistCm, 1) + ",";
  json += "\"pose\":{";
  json += "\"x\":" + String(posX, 3) + ",";
  json += "\"y\":" + String(posY, 3) + ",";
  json += "\"theta_deg\":" + String(thetaDeg, 1);
  json += "},";
  json += "\"timestamp_ms\":" + String(millis());
  json += "}";
  
  int code = http.POST(json);
  Serial.print("POST -> ");
  Serial.print(code);
  if (code == 200) {
    Serial.println(" ✅");
  } else {
    Serial.println(" ❌");
  }
  http.end();
}

// ================= Navigation State =================
enum NavState {
  NAV_FORWARD,
  NAV_BACKING,
  NAV_TURNING
};

NavState navState = NAV_FORWARD;
unsigned long navStateStart = 0;

const float SAFE_DIST_CM = 40.0f;
const float DANGER_DIST_CM = 15.0f;
const unsigned long BACK_TIME_MS = 420;
const unsigned long TURN_TIME_MS = 560;

void autonomousNav() {
  unsigned long now = millis();
  unsigned long elapsed = now - navStateStart;
  
  // Handle sensor errors
  if (cachedDistCm < 0) {
    driveStop();
    return;
  }
  
  switch (navState) {
    case NAV_FORWARD:
      driveForward();
      
      if (cachedDistCm < DANGER_DIST_CM) {
        navState = NAV_BACKING;
        navStateStart = now;
        Serial.println("🔄 BACKING");
      }
      else if (cachedDistCm < SAFE_DIST_CM) {
        navState = NAV_TURNING;
        navStateStart = now;
        Serial.println("🔄 TURNING");
      }
      break;
      
    case NAV_BACKING:
      driveBackward();
      
      if (elapsed >= BACK_TIME_MS) {
        navState = NAV_TURNING;
        navStateStart = now;
        Serial.println("🔄 TURNING");
      }
      break;
      
    case NAV_TURNING:
      // Random turn direction for better coverage
      if (random(0, 2) == 0) {
        turnLeft();
      } else {
        turnRight();
      }
      
      if (elapsed >= TURN_TIME_MS) {
        navState = NAV_FORWARD;
        navStateStart = now;
        Serial.println("➡️ FORWARD");
      }
      break;
  }
}

// ================= Setup ===================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("🤖 Robot Initializing...");

  connectWiFi();

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!am2320.begin()) {
    Serial.println("⚠️ AM2320 not found!");
  }

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  leftServo.attach(LEFT_WHEEL_CTRL);
  rightServo.attach(RIGHT_WHEEL_CTRL);
  driveStop();
  delay(500);

  pinMode(LEFT_FB_PIN, INPUT);
  pinMode(RIGHT_FB_PIN, INPUT);

  prevLeftAngle  = readAngle(LEFT_FB_PIN);
  prevRightAngle = readAngle(RIGHT_FB_PIN);
  
  Serial.print("Encoder baseline: L=");
  Serial.print(prevLeftAngle);
  Serial.print("° R=");
  Serial.print(prevRightAngle);
  Serial.println("°");
  
  navStateStart = millis();
  tOdom = tSensor = tSend = millis();

  Serial.println("\n========================================");
  Serial.println("🚀 ROBOT READY - AUTONOMOUS MODE");
  Serial.println("========================================");
  Serial.print("METERS_PER_DEG = "); Serial.println(METERS_PER_DEG, 7);
  Serial.println("========================================\n");
  
  delay(1000);
}

// ================= Loop ====================
void loop() {
  unsigned long now = millis();
  
  // Update sensors on schedule
  if (now - tSensor >= SENSOR_EVERY_MS) {
    tSensor = now;
    updateSensors();
  }
  
  // Navigate based on cached sensor values
  autonomousNav();
  
  // Update odometry continuously
  if (now - tOdom >= ODOM_EVERY_MS) {
    tOdom = now;
    updateOdometry();
  }
  
  // Send telemetry on schedule
  if (now - tSend >= SEND_EVERY_MS) {
    tSend = now;
    sendTelemetry();
    
    Serial.printf(
      "📊 T=%.1f°C H=%.1f%% D=%.1fcm | 📍 X=%.3fm Y=%.3fm θ=%.1f°\n",
      cachedTemp, cachedHumid, cachedDistCm, 
      posX, posY, thetaRad * 180.0f / (float)M_PI
    );
  }
}