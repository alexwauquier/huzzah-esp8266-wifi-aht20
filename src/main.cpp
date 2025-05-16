#include <Arduino.h>
#include <Adafruit_AHTX0.h>
#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>

const char* wifiSsid = "YOUR_WIFI_SSID";
const char* wifiPassword = "YOUR_WIFI_PASSWORD";

const char* hostName = "example.com";
const int hostPort = 443;

const char* userUsername = "USER_USERNAME";
const char* userPassword = "USER_PASSWORD";

const int sensorIdTemperature = 1;
const int sensorIdHumidity = 2;
const int measurementIntervalInMS = 60000;

String jwtToken = "";

Adafruit_AHTX0 aht;

String readChunkedBody(WiFiClientSecure &client) {
  String body = "";
  while (true) {
    String line = client.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;

    int chunkSize = (int) strtol(line.c_str(), NULL, 16);
    if (chunkSize == 0) break;

    while (chunkSize > 0) {
      if (!client.connected()) break;
      int c = client.read();
      if (c < 0) break;
      body += (char)c;
      chunkSize--;
    }

    client.read();
    client.read();
  }
  return body;
}

void connectToWiFi() {
  WiFi.begin(wifiSsid, wifiPassword);
  Serial.print("Connecting to WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("\nConnected!\n");
}

bool authenticate() {
  WiFiClientSecure client;
  client.setInsecure();

  if (!client.connect(hostName, hostPort)) {
    Serial.println("Authentication connection failed.\n");
    return false;
  }

  JsonDocument loginDoc;
  loginDoc["username"] = userUsername;
  loginDoc["password"] = userPassword;
  String jsonStr;
  serializeJson(loginDoc, jsonStr);

  client.println(String("POST ") + "/api/auth/login/employee" + " HTTP/1.1");
  client.println("Host: " + String(hostName));
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(jsonStr.length());
  client.println();
  client.println(jsonStr);

  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  String response = readChunkedBody(client);
  JsonDocument resDoc;
  DeserializationError error = deserializeJson(resDoc, response);

  if (error) {
    Serial.print("JSON error: ");
    Serial.println(error.c_str());
    return false;
  }

  if (resDoc["success"] && resDoc["data"]["token"]) {
    jwtToken = resDoc["data"]["token"].as<String>();
    Serial.println("Authentication successful!\n");
    return true;
  }

  Serial.println("Authentication failed.\n");
  return false;
}

bool sendSensorMeasurement(int sensorId, float value) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, attempt to reconnect...");
    connectToWiFi();
  }

  if (jwtToken == "") {
    Serial.println("Token not available, authentication attempt...");
    if (!authenticate()) return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  if (client.connect(hostName, hostPort)) {
    JsonDocument json;
    json["value"] = value;
    String payload;
    serializeJson(json, payload);

    String url = "/api/sensors/" + String(sensorId) + "/measurements";

    client.println("POST " + url + " HTTP/1.1");
    client.println("Host: " + String(hostName));
    client.println("User-Agent: ESP8266");
    client.println("Content-Type: application/json");
    client.print("Authorization: Bearer ");
    client.println(jwtToken);
    client.print("Content-Length: ");
    client.println(payload.length());
    client.println();
    client.println(payload);

    while (client.connected()) {
      String line = client.readStringUntil('\n');
      if (line == "\r") break;
    }

    String response = readChunkedBody(client);
    JsonDocument resDoc;
    DeserializationError error = deserializeJson(resDoc, response);

    if (error) {
      Serial.print("JSON error: ");
      Serial.println(error.c_str());
      client.stop();
      return false;
    }

    if (resDoc["success"] && sensorId == sensorIdTemperature) {
      Serial.println("Temperature successfully sent!");
      client.stop();
      return true;
    }

    if (resDoc["success"] && sensorId == sensorIdHumidity) {
      Serial.println("Humidity sent with success!");
      client.stop();
      return true;
    }

    if (!resDoc["success"] && resDoc["error"]["code"] == 401) {
      Serial.println("Invalid token, re-authentication attempt...");
      jwtToken = "";

      if (authenticate()) {
        return sendSensorMeasurement(sensorId, value);
      }

      return false;
    }

    Serial.println("Error when sending.\n");
    client.stop();
    return false;
  } else {
    Serial.printf("Sensor %d connection failed.\n", sensorId);
    return false;
  }
}

void setup() {
  delay(1000);
  Serial.begin(74880);
  Serial.print("\n");

  if (!aht.begin()) {
    Serial.println("AHT20 sensor not detected.");
    while (1) delay(10);
  }

  connectToWiFi();

  if (!authenticate()) {
    Serial.println("Unable to retrieve token. Please check login details.");
  }
}

void loop() {
  sensors_event_t humidity, temp;
  aht.getEvent(&humidity, &temp);

  float temperature = temp.temperature;
  float hum = humidity.relative_humidity;

  Serial.printf("Temperature: %.2f °C\n", temperature);
  Serial.printf("Humidity: %.2f %%\n", hum);

  sendSensorMeasurement(sensorIdTemperature, temperature);
  sendSensorMeasurement(sensorIdHumidity, hum);

  Serial.print("\n");

  delay(measurementIntervalInMS);
}
