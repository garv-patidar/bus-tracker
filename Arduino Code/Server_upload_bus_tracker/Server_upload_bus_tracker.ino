#include <WiFi.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <TinyGPSPlus.h>
#include <SoftwareSerial.h>
#include <HTTPClient.h>

const char *ssid = "------";
const char *password = "----";
static const int RXPin = 3, TXPin = 1;
static const uint32_t GPSBaud = 9600;
TinyGPSPlus gps;
SoftwareSerial ss(RXPin, TXPin);

int value = 0;
double lat = 10;
double lon = 10;
double ele = 0;
double vel = 0;
#define IO_LOOP_DELAY 2000
unsigned long lastUpdate;

// Define your server details
const char *serverUrl = "----"; // Adjust port if necessary

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  ss.begin(GPSBaud);

  ArduinoOTA
    .onStart([]() {
      String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  ArduinoOTA.handle();

  // Read GPS data
  while (ss.available() > 0) {
    gps.encode(ss.read());
  }

  if (millis() > (lastUpdate + IO_LOOP_DELAY)) {
    // Update GPS data
    lat = gps.location.lat();
    lon = gps.location.lng();
    vel = gps.speed.kmph();

    // Log data
    Serial.print(value);
    Serial.print(", ");
    Serial.print(lat, 6);
    Serial.print(", ");
    Serial.print(lon, 6);
    Serial.print(", ");
    Serial.println(vel, 2);

    // Send data to the private server
    sendToServer(value, lat, lon, vel);

    value += 1;
    lastUpdate = millis();
  }
}

void sendToServer(int id, double latitude, double longitude, double velocity) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);

    // JSON payload

    String payload = String("{") +
                     "\"device_name\":\"Bus775\"," +
                     "\"latitude\":" + String(latitude, 6) + "," +
                     "\"longitude\":" + String(longitude, 6) +
                     "}";




    // Set headers and send POST request
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Api-Key", "");
    
    int httpResponseCode = http.POST(payload);

    if (httpResponseCode > 0) {
      Serial.printf("Server Response: %s\n", http.getString().c_str());
    } else {
      Serial.printf("Error sending data: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
  } else {
    Serial.println("WiFi not connected, skipping data send.");
  }
}
