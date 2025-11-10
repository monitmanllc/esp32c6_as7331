/*
 * AS7331 UV Sensor Data Logger
 * Reads UV sensor data and posts to remote server every minute
 * 
 * Hardware: ESP32-C6 + SparkFun AS7331 UV Sensor
 * I2C Connections:
 *   SDA: GPIO6
 *   SCL: GPIO7
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <SparkFun_AS7331.h>

// ===== CONFIGURATION =====
// WiFi Credentials
const char* ssid = "SomeWifiNetwork";
const char* password = "ILikeTurkeyBaconSwiss";

// Server Configuration
const char* serverUrl = "https://app.monitman.com/dashboard/receive.php?sensor=uv";  // Change to your server URL

// I2C Configuration
#define SDA_PIN 6
#define SCL_PIN 7

// Timing Configuration
const unsigned long POST_INTERVAL = 60000;  // 60 seconds (1 minute)

// ===== GLOBAL OBJECTS =====
SfeAS7331ArdI2C uvSensor;
unsigned long lastPostTime = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== AS7331 UV Sensor Logger ===");
  
  // Initialize I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Serial.println("I2C initialized");
  
  // Initialize AS7331 Sensor
  if (!uvSensor.begin()) {
    Serial.println("ERROR: AS7331 sensor not detected!");
    Serial.println("Check wiring and I2C address (default: 0x74)");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("AS7331 sensor initialized");
  
  // Configure sensor
  configureSensor();
  
  // Connect to WiFi
  connectWiFi();
  
  Serial.println("Setup complete. Starting measurements...\n");
}

void loop() {
  // Check if it's time to read and post data
  if (millis() - lastPostTime >= POST_INTERVAL) {
    readAndPostData();
    lastPostTime = millis();
  }
  
  // Small delay to prevent excessive CPU usage
  delay(100);
}

void configureSensor() {
  Serial.println("Configuring AS7331 sensor...");
  
  // Try to read current configuration
  Serial.print("Current Gain: ");
  Serial.println(uvSensor.getGainValue());
  Serial.print("Current Conversion Time (ms): ");
  Serial.println(uvSensor.getConversionTimeMillis());
  
  // Set measurement mode to continuous AND start measurements
  // prepareMeasurement(mode, startMeasure=true) configures and starts the sensor
  if (uvSensor.prepareMeasurement(MEAS_MODE_CONT, true) == false) {
    Serial.println("ERROR: Failed to start continuous measurement mode");
    while (1) {
      delay(1000);
    }
  }
  
  Serial.println("Sensor configured and measurement started in CONT mode");
  Serial.println("Waiting for first conversion to complete...");
  delay(1000);  // Give it time for first measurement
  
  Serial.println("\nSensor is now running. Shine UV light on it!");
}

void connectWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed!");
  }
}

void readAndPostData() {
  Serial.println("--- Reading UV Sensor Data ---");
  
  // Read all sensor data (UV + temperature)
  Serial.println("Reading from sensor...");
  sfTkError_t result = uvSensor.readAll();
  
  if (result != ksfTkErrOk) {
    Serial.print("ERROR: Failed to read sensor data. Error code: ");
    Serial.println(result);
    Serial.println();
    return;
  }
  
  Serial.println("Read successful!");
  
  // Get the UV values (stored locally in the library after readAll)
  // Note: These return floats, not raw uint16_t values
  float uvaData = uvSensor.getUVA();
  float uvbData = uvSensor.getUVB();
  float uvcData = uvSensor.getUVC();
  float tempData = uvSensor.getTemp();
  
  Serial.print("UVA: "); Serial.print(uvaData, 4); Serial.println(" µW/cm²");
  Serial.print("UVB: "); Serial.print(uvbData, 4); Serial.println(" µW/cm²");
  Serial.print("UVC: "); Serial.print(uvcData, 4); Serial.println(" µW/cm²");
  Serial.print("Temperature: "); Serial.print(tempData, 2); Serial.println(" °C");
  
  // Check if we're getting any non-zero values
  if (uvaData == 0 && uvbData == 0 && uvcData == 0) {
    Serial.println("WARNING: All UV values are zero!");
    Serial.println("Try: 1) Shining UV light directly on sensor");
    Serial.println("     2) Checking if UV light emits in 220-400nm range");
    Serial.println("     3) Removing any protective covering on sensor");
  }
  
  // Create JSON payload with float values
  String jsonPayload = createJsonPayload(uvaData, uvbData, uvcData, tempData);
  Serial.println("\nJSON Payload:");
  Serial.println(jsonPayload);
  
  // Send data to server
  if (WiFi.status() == WL_CONNECTED) {
    sendDataToServer(jsonPayload);
  } else {
    Serial.println("WiFi not connected. Attempting to reconnect...");
    connectWiFi();
  }
  
  Serial.println();
}

String createJsonPayload(float uva, float uvb, float uvc, float temp) {
  // Build JSON string manually with float values
  String json = "{";
  json += "\"timestamp\":" + String(millis()) + ",";
  json += "\"uva\":" + String(uva, 2) + ",";
  json += "\"uvb\":" + String(uvb, 2) + ",";
  json += "\"uvc\":" + String(uvc, 2) + ",";
  json += "\"temperature\":" + String(temp, 2) + ",";
  json += "\"device\":\"ESP32-C6\",";
  json += "\"sensor\":\"AS7331\"";
  json += "}";
  
  return json;
}

void sendDataToServer(String jsonPayload) {
  HTTPClient http;
  
  Serial.print("Posting to: ");
  Serial.println(serverUrl);
  
  // Begin HTTP connection
  http.begin(serverUrl);
  
  // Set content type header
  http.addHeader("Content-Type", "application/json");
  
  // Send POST request
  int httpResponseCode = http.POST(jsonPayload);
  
  // Check response
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    
    String response = http.getString();
    Serial.print("Server response: ");
    Serial.println(response);
  } else {
    Serial.print("HTTP POST failed. Error: ");
    Serial.println(http.errorToString(httpResponseCode));
  }
  
  // Free resources
  http.end();
}
