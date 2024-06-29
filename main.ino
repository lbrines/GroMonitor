/*
MIT License

Copyright (c) 2024 Leopoldo Brines

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Created with the help of ChatGPT, 2024.
*/

#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <aREST.h>
#include <DHT.h>
#include <Adafruit_Sensor.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecureBearSSL.h>
#include <ESPTelnet.h>

#define DEBUG 1

#define S0 D5
#define S1 D6
#define S2 D7
#define S3 D8

#define DHT_PIN D1
#define DHT_TYPE DHT22

#define RELAY_PIN D2

const char* WIFI_SSID = "rapanui";
const char* WIFI_PASSWORD = "24242376!";

String ZABBIX_URL = "http://192.168.68.150:8080/api_jsonrpc.php";
String ZABBIX_TOKEN = "3356c36c61a7c46cff5a455902bcb5780fe9b24cc3055cf739cfaadb48d113bf";
String HOST_ID = "10630";

const long INTERVAL = 30000;
const long DEBOUNCE_DELAY = 5000;

bool RELAY_CONNECTED = true;
bool CHECK_ALERT = false;
bool TELNET_ENABLED = true;

int AVERAGE_HYGROMETERS = 30;
int RELAY_ON_TIME = 30;
int RELAY_OFF_TIME = 20;

WiFiServer server(80);
aREST rest;
DHT dht(DHT_PIN, DHT_TYPE);

float HUMIDITY, TEMPERATURE;
int HYGROMETER_0, HYGROMETER_1, CO;

unsigned long previousMillis = 0;
unsigned long lastRelayChangeTime = 0;

const char* DEVICE_NAME = "multi_channel_sensor";
const char* DEVICE_ID = "2";

char setAlertCheckFunction[] = "setAlertCheck";

bool activeAlert = false;
String irrigationTriggerID;
String growMetricsItemID;

int HYGROMETER0_CALIB_MIN = 600;
int HYGROMETER0_CALIB_MAX = 1024;
int HYGROMETER1_CALIB_MIN = 560;
int HYGROMETER1_CALIB_MAX = 1024;

int CO_CALIBRATED = 0;

ESPTelnet telnetServer;

void logMessage(const char* type, const char* message, int line = 0);
void printConfig();

void setupPins();
void setupDHT();
void setupWiFi();
void setupRest();
void readHygrometers();
int readHygrometer(bool s0, bool s1, bool s2, bool s3);
void calibrateCO();
void readDHT();
void sendToZabbix(const String& data);
void handleZabbixResponse(int httpCode, HTTPClient& http);
bool checkZabbixAlert();
bool handleCheckAlertResponse(int httpCode, HTTPClient& http);
void controlRelay();
void printChipInfo();
String getTriggerID(const char* description);
String getItemID(const char* key);
void setupTelnet();
void handleTelnet();

void setup() {
  Serial.begin(9600);
  setupPins();
  setupDHT();
  setupWiFi();
  setupRest();
  printChipInfo();

  irrigationTriggerID = getTriggerID("Irrigation active");
  logMessage("INFO", irrigationTriggerID.isEmpty() ? "Trigger ID for 'Irrigation active' not found" : "Trigger ID for 'Irrigation active' obtained", DEBUG ? __LINE__ : 0);

  growMetricsItemID = getItemID("grow.metrics");
  logMessage("INFO", growMetricsItemID.isEmpty() ? "Item ID for 'grow.metrics' not found" : "Item ID for 'grow.metrics' obtained", DEBUG ? __LINE__ : 0);

  if (TELNET_ENABLED) {
    setupTelnet();
  }

  // OTA configuration
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else {
      type = "filesystem";
    }
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();

  calibrateCO();
}

void setupPins() {
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
}

void setupDHT() {
  dht.begin();
}

void setupWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  logMessage("INFO", "Connecting to WiFi", DEBUG ? __LINE__ : 0);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    logMessage("INFO", "Waiting for WiFi connection...", DEBUG ? __LINE__ : 0);
    if (++attempts > 20) {
      logMessage("ERROR", "Failed to connect to WiFi after 20 attempts", DEBUG ? __LINE__ : 0);
      return;
    }
  }
  logMessage("INFO", "Connected to WiFi", DEBUG ? __LINE__ : 0);
  logMessage("INFO", ("SSID: " + String(WIFI_SSID)).c_str(), DEBUG ? __LINE__ : 0);
  logMessage("INFO", ("Signal strength (RSSI): " + String(WiFi.RSSI())).c_str(), DEBUG ? __LINE__ : 0);
  server.begin();
  logMessage("INFO", "Server started", DEBUG ? __LINE__ : 0);
  logMessage("INFO", WiFi.localIP().toString().c_str(), DEBUG ? __LINE__ : 0);
}

void setupRest() {
  rest.variable("humidity", &HUMIDITY);
  rest.variable("temperature", &TEMPERATURE);
  rest.variable("hygrometer0", &HYGROMETER_0);
  rest.variable("hygrometer1", &HYGROMETER_1);
  rest.variable("co", &CO);
  rest.set_id(DEVICE_ID);
  rest.set_name(DEVICE_NAME);
  rest.function(setAlertCheckFunction, setAlertCheck);
}

int setAlertCheck(String command) {
  if (command == "on") {
    CHECK_ALERT = true;
  } else if (command == "off") {
    CHECK_ALERT = false;
  }
  return CHECK_ALERT;
}

void readHygrometers() {
  int rawHygrometer0 = readHygrometer(LOW, LOW, LOW, LOW);
  int rawHygrometer1 = readHygrometer(HIGH, LOW, LOW, LOW);
  int rawCO = readCO(LOW, HIGH, LOW, LOW);

  CO = rawCO - CO_CALIBRATED;

  HYGROMETER_0 = map(rawHygrometer0, HYGROMETER0_CALIB_MIN, HYGROMETER0_CALIB_MAX, 100, 0);
  HYGROMETER_1 = map(rawHygrometer1, HYGROMETER1_CALIB_MIN, HYGROMETER1_CALIB_MAX, 100, 0);

  HYGROMETER_0 = constrain(HYGROMETER_0, 0, 100);
  HYGROMETER_1 = constrain(HYGROMETER_1, 0, 100);

#if DEBUG
  logMessage("DEBUG", ("Raw Hygrometer 0: " + String(rawHygrometer0) + " -> Calibrated: " + String(HYGROMETER_0)).c_str(), __LINE__);
  logMessage("DEBUG", ("Raw Hygrometer 1: " + String(rawHygrometer1) + " -> Calibrated: " + String(HYGROMETER_1)).c_str(), __LINE__);
  logMessage("DEBUG", ("Raw CO: " + String(rawCO) + " -> Calibrated CO: " + String(CO)).c_str(), __LINE__);
#endif
}

int readHygrometer(bool s0, bool s1, bool s2, bool s3) {
  digitalWrite(S0, s0);
  digitalWrite(S1, s1);
  digitalWrite(S2, s2);
  digitalWrite(S3, s3);
  delay(200);
  return analogRead(A0);
}

int readCO(bool s0, bool s1, bool s2, bool s3) {
  digitalWrite(S0, s0);
  digitalWrite(S1, s1);
  digitalWrite(S2, s2);
  digitalWrite(S3, s3);
  delay(200);
  return analogRead(A0);
}

void calibrateCO() {
  int rawCO = readCO(LOW, HIGH, LOW, LOW);
  CO_CALIBRATED = rawCO;
  logMessage("INFO", ("CO Calibrated Value: " + String(CO_CALIBRATED)).c_str(), DEBUG ? __LINE__ : 0);
  if (rawCO == 0) {
    logMessage("WARN", "CO reading for calibration is 0, check sensor", DEBUG ? __LINE__ : 0);
  }
}

void readDHT() {
  HUMIDITY = dht.readHumidity();
  TEMPERATURE = dht.readTemperature();

  if (isnan(HUMIDITY) || isnan(TEMPERATURE)) {
    logMessage("ERROR", "Error reading DHT22 sensor", DEBUG ? __LINE__ : 0);
  }
}

void sendToZabbix(const String& data) {
  if (WiFi.status() == WL_CONNECTED && !growMetricsItemID.isEmpty()) {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, ZABBIX_URL);
    http.addHeader("Content-Type", "application/json-rpc");
    http.addHeader("Authorization", "Bearer " + ZABBIX_TOKEN);

    StaticJsonDocument<256> doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "history.push";
    JsonObject params = doc.createNestedObject("params");
    params["itemid"] = growMetricsItemID;
    params["value"] = data;
    doc["id"] = 1;

    String payload;
    serializeJson(doc, payload);

    logMessage("INFO", ("Payload sent: " + payload).c_str(), DEBUG ? __LINE__ : 0);

    int httpCode = http.POST(payload);
    handleZabbixResponse(httpCode, http);

    http.end();
  } else {
    logMessage("ERROR", "Not connected to WiFi or growMetricsItemID not available", DEBUG ? __LINE__ : 0);
  }
}

void handleZabbixResponse(int httpCode, HTTPClient& http) {
  if (httpCode > 0) {
    String response = http.getString();
    logMessage("INFO", ("Zabbix Server response: " + response).c_str(), DEBUG ? __LINE__ : 0);

    StaticJsonDocument<512> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    if (error) {
      logMessage("ERROR", ("Error parsing JSON response: " + String(error.f_str())).c_str(), DEBUG ? __LINE__ : 0);
      return;
    }

    if (responseDoc.containsKey("error")) {
      logMessage("ERROR", responseDoc["error"]["data"].as<String>().c_str(), DEBUG ? __LINE__ : 0);
    } else if (responseDoc.containsKey("result") && responseDoc["result"]["response"] == "success") {
      logMessage("INFO", "Data sent successfully", DEBUG ? __LINE__ : 0);
    } else {
      logMessage("ERROR", "Error in Zabbix response", DEBUG ? __LINE__ : 0);
    }
  } else {
    logMessage("ERROR", ("HTTP request error, code: " + String(httpCode)).c_str(), DEBUG ? __LINE__ : 0);
  }
}

bool checkZabbixAlert() {
  if (WiFi.status() == WL_CONNECTED && !irrigationTriggerID.isEmpty()) {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, ZABBIX_URL);
    http.addHeader("Content-Type", "application/json-rpc");
    http.addHeader("Authorization", "Bearer " + ZABBIX_TOKEN);

    StaticJsonDocument<256> doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "trigger.get";
    JsonObject params = doc.createNestedObject("params");
    params["triggerids"] = irrigationTriggerID;
    JsonArray output = params.createNestedArray("output");
    output.add("triggerid");
    output.add("description");
    output.add("priority");
    output.add("status");
    output.add("value");

    doc["id"] = 1;

    String payload;
    serializeJson(doc, payload);

    logMessage("INFO", ("Payload sent to check alert: " + payload).c_str(), DEBUG ? __LINE__ : 0);

    int httpCode = http.POST(payload);
    bool alert = handleCheckAlertResponse(httpCode, http);

    http.end();
    return alert;
  } else {
    logMessage("ERROR", "Not connected to WiFi or irrigationTriggerID not available", DEBUG ? __LINE__ : 0);
  }
  return false;
}

bool handleCheckAlertResponse(int httpCode, HTTPClient& http) {
  if (httpCode > 0) {
    String response = http.getString();
    logMessage("INFO", ("Zabbix Server response to check alert: " + response).c_str(), DEBUG ? __LINE__ : 0);

    StaticJsonDocument<1024> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    if (error) {
      logMessage("ERROR", ("Error parsing JSON response: " + String(error.f_str())).c_str(), DEBUG ? __LINE__ : 0);
      return false;
    }

    if (responseDoc.containsKey("result")) {
      JsonArray result = responseDoc["result"].as<JsonArray>();
      if (result.size() > 0) {
        JsonObject trigger = result[0];
        int value = trigger["value"].as<int>();
        return value == 1;
      }
    }
  } else {
    logMessage("ERROR", ("HTTP request error to check alert, code: " + String(httpCode)).c_str(), DEBUG ? __LINE__ : 0);
  }
  return false;
}

void controlRelay() {
  static bool lastActiveAlert = false;
  static bool relayOn = false;
  static unsigned long onTime = 0;
  static unsigned long offTime = 0;
  unsigned long currentMillis = millis();

  if (activeAlert) {
    if (relayOn) {
      if (currentMillis - lastRelayChangeTime >= RELAY_ON_TIME * 1000) {
        digitalWrite(RELAY_PIN, LOW);
        relayOn = false;
        lastRelayChangeTime = currentMillis;
        onTime++;
        logMessage("INFO", String("Relay off after " + String(RELAY_ON_TIME) + " seconds").c_str(), DEBUG ? __LINE__ : 0);
#if DEBUG
        logMessage("DEBUG", String("On time: " + String(onTime) + " cycles of " + String(RELAY_ON_TIME) + " seconds").c_str(), __LINE__);
#endif
      }
    } else {
      if (currentMillis - lastRelayChangeTime >= RELAY_OFF_TIME * 1000) {
        digitalWrite(RELAY_PIN, HIGH);
        relayOn = true;
        lastRelayChangeTime = currentMillis;
        offTime++;
        logMessage("INFO", String("Relay on after " + String(RELAY_OFF_TIME) + " seconds").c_str(), DEBUG ? __LINE__ : 0);
#if DEBUG
        logMessage("DEBUG", String("Off time: " + String(offTime) + " cycles of " + String(RELAY_OFF_TIME) + " seconds").c_str(), __LINE__);
#endif
      }
    }
  } else {
    if (relayOn) {
      digitalWrite(RELAY_PIN, LOW);
      relayOn = false;
      logMessage("INFO", "Relay off: No active alert", DEBUG ? __LINE__ : 0);
#if DEBUG
      logMessage("DEBUG", "Relay off: No active alert", __LINE__);
#endif
    }
  }
  lastActiveAlert = activeAlert;
}

void loop() {
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= INTERVAL) {
    previousMillis = currentMillis;

    readHygrometers();
    readDHT();

    StaticJsonDocument<256> doc;
    doc["temperature"] = TEMPERATURE;
    doc["humidity"] = HUMIDITY;
    doc["hygrometer0"] = HYGROMETER_0;
    doc["hygrometer1"] = HYGROMETER_1;
    doc["CO"] = CO;

    String json_data;
    serializeJson(doc, json_data);

    sendToZabbix(json_data);

    activeAlert = checkZabbixAlert();

    controlRelay();
  }

  WiFiClient client = server.available();
  if (client) {
    while (!client.available()) {
      delay(1);
    }
    rest.handle(client);
  }

  if (TELNET_ENABLED) {
    handleTelnet();
  }

  ArduinoOTA.handle();
}

void printChipInfo() {
  logMessage("INFO", "Chip Information:", 0);
  logMessage("INFO", ("Chip ID: " + String(ESP.getChipId())).c_str(), 0);
  logMessage("INFO", ("Flash Chip ID: " + String(ESP.getFlashChipId())).c_str(), 0);
  logMessage("INFO", ("Flash Chip Size: " + String(ESP.getFlashChipSize())).c_str(), 0);
  logMessage("INFO", ("Real Flash Chip Size: " + String(ESP.getFlashChipRealSize())).c_str(), 0);
  logMessage("INFO", ("Flash Chip Speed: " + String(ESP.getFlashChipSpeed())).c_str(), 0);
  logMessage("INFO", ("Sketch Size: " + String(ESP.getSketchSize())).c_str(), 0);
  logMessage("INFO", ("Free Sketch Space: " + String(ESP.getFreeSketchSpace())).c_str(), 0);
  logMessage("INFO", ("SDK Version: " + String(ESP.getSdkVersion())).c_str(), 0);
}

String getTriggerID(const char* description) {
  String triggerID = "";
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, ZABBIX_URL);
    http.addHeader("Content-Type", "application/json-rpc");

    StaticJsonDocument<512> doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "trigger.get";
    JsonObject params = doc.createNestedObject("params");
    params["hostids"] = HOST_ID;
    JsonArray output = params.createNestedArray("output");
    output.add("triggerid");
    output.add("description");
    output.add("priority");
    output.add("status");
    doc["auth"] = ZABBIX_TOKEN;
    doc["id"] = 1;

    String payload;
    serializeJson(doc, payload);

    int httpCode = http.POST(payload);
    if (httpCode > 0) {
      String response = http.getString();
      StaticJsonDocument<1024> responseDoc;
      DeserializationError error = deserializeJson(responseDoc, response);

      if (error) {
        logMessage("ERROR", ("Error parsing JSON response: " + String(error.f_str())).c_str(), DEBUG ? __LINE__ : 0);
        return "";
      }

      JsonArray results = responseDoc["result"].as<JsonArray>();
      for (JsonObject trigger : results) {
        if (trigger["description"] == description) {
          triggerID = trigger["triggerid"].as<String>();
          break;
        }
      }
    } else {
      logMessage("ERROR", ("HTTP request error, code: " + String(httpCode)).c_str(), DEBUG ? __LINE__ : 0);
    }

    http.end();
  } else {
    logMessage("ERROR", "Not connected to WiFi", DEBUG ? __LINE__ : 0);
  }
  return triggerID;
}

String getItemID(const char* key) {
  String itemID = "";
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, ZABBIX_URL);
    http.addHeader("Content-Type", "application/json-rpc");

    StaticJsonDocument<512> doc;
    doc["jsonrpc"] = "2.0";
    doc["method"] = "item.get";
    JsonObject params = doc.createNestedObject("params");
    params["hostids"] = HOST_ID;
    JsonArray output = params.createNestedArray("output");
    output.add("itemid");
    output.add("name");
    output.add("key_");
    output.add("type");
    output.add("value_type");
    output.add("status");
    doc["auth"] = ZABBIX_TOKEN;
    doc["id"] = 1;

    String payload;
    serializeJson(doc, payload);

    int httpCode = http.POST(payload);
    if (httpCode > 0) {
      String response = http.getString();
      StaticJsonDocument<1024> responseDoc;
      DeserializationError error = deserializeJson(responseDoc, response);

      if (error) {
        logMessage("ERROR", ("Error parsing JSON response: " + String(error.f_str())).c_str(), DEBUG ? __LINE__ : 0);
        return "";
      }

      JsonArray results = responseDoc["result"].as<JsonArray>();
      for (JsonObject item : results) {
        if (item["key_"] == key) {
          itemID = item["itemid"].as<String>();
          break;
        }
      }
    } else {
      logMessage("ERROR", ("HTTP request error, code: " + String(httpCode)).c_str(), DEBUG ? __LINE__ : 0);
    }

    http.end();
  } else {
    logMessage("ERROR", "Not connected to WiFi", DEBUG ? __LINE__ : 0);
  }
  return itemID;
}

void logMessage(const char* type, const char* message, int line) {
  String formattedMessage = String(message);
  formattedMessage.replace("'", "\"");
  
  String logMessage = "{";
  logMessage += "\"type\":\"" + String(type) + "\",";
  logMessage += "\"message\":\"" + formattedMessage + "\",";
  logMessage += "\"timestamp\":" + String(millis());
  if (strcmp(type, "INFO") != 0 && line > 0) {
    logMessage += ",\"line\":" + String(line);
  }
  logMessage += "}";

  Serial.println(logMessage);
  if (TELNET_ENABLED && telnetServer.isConnected()) {
    telnetServer.println(logMessage);
  }
}

void setupTelnet() {
  telnetServer.onConnect([](String ip) {
    logMessage("INFO", ("Telnet client connected from " + ip).c_str(), DEBUG ? __LINE__ : 0);
    telnetServer.println("Welcome to the Telnet server.");
  });

  telnetServer.onConnectionAttempt([](String ip) {
    logMessage("INFO", ("Telnet connection attempt from " + ip).c_str(), DEBUG ? __LINE__ : 0);
  });

  telnetServer.onReconnect([](String ip) {
    logMessage("INFO", ("Telnet client reconnected from " + ip).c_str(), DEBUG ? __LINE__ : 0);
  });

  telnetServer.onDisconnect([](String ip) {
    logMessage("INFO", ("Telnet client disconnected from " + ip).c_str(), DEBUG ? __LINE__ : 0);
  });

  telnetServer.onInputReceived([](String str) {
    logMessage("INFO", ("Telnet received: " + str).c_str(), DEBUG ? __LINE__ : 0);
    if (str == "exit") {
      telnetServer.disconnectClient();
    } else if (str == "CONFIG") {
      printConfig();
    } else {
      telnetServer.print("> ");
      telnetServer.println(str);
    }
  });

  telnetServer.begin();
  logMessage("INFO", "Telnet server started", DEBUG ? __LINE__ : 0);
}

void handleTelnet() {
  telnetServer.loop();
}

void printConfig() {
  String configInfo = "Current configuration: ";
  configInfo += "ZABBIX_URL: " + ZABBIX_URL + " ";
  configInfo += "ZABBIX_TOKEN: " + ZABBIX_TOKEN + " ";
  configInfo += "HOST_ID: " + HOST_ID + " ";
  configInfo += "RELAY_CONNECTED: " + String(RELAY_CONNECTED) + " ";
  configInfo += "CHECK_ALERT: " + String(CHECK_ALERT) + " ";
  configInfo += "TELNET_ENABLED: " + String(TELNET_ENABLED) + " ";
  configInfo += "AVERAGE_HYGROMETERS: " + String(AVERAGE_HYGROMETERS) + " ";
  configInfo += "RELAY_ON_TIME: " + String(RELAY_ON_TIME) + " ";
  configInfo += "RELAY_OFF_TIME: " + String(RELAY_OFF_TIME) + " ";
  configInfo += "HYGROMETER0_CALIB_MIN: " + String(HYGROMETER0_CALIB_MIN) + " ";
  configInfo += "HYGROMETER0_CALIB_MAX: " + String(HYGROMETER0_CALIB_MAX) + " ";
  configInfo += "HYGROMETER1_CALIB_MIN: " + String(HYGROMETER1_CALIB_MIN) + " ";
  configInfo += "HYGROMETER1_CALIB_MAX: " + String(HYGROMETER1_CALIB_MAX) + " ";
  configInfo += "CO_CALIBRATED: " + String(CO_CALIBRATED);

  telnetServer.println(configInfo);
  logMessage("INFO", configInfo.c_str());
}
