#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <AsyncTCP.h>
#include <ArduinoOTA.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>
#include <DHT.h>
AsyncWebServer server(80);
// EC200U communication
#define SerialMon Serial       // Debug output to Serial Monitor
#define SerialAT Serial2       // Communication with EC200U

// Define RX2 and TX2 pins for Serial2
#define RX2 D7  // Replace with your RX2 pin number
#define TX2 D6  // Replace with your TX2 pin number
#define DHTPIN D9   // Change to another GPIO if needed (e.g., 4 or 5)
#define DHTTYPE DHT22 // Using DHT11 sensor

DHT dht(DHTPIN, DHTTYPE);
// Wi-Fi credentials
const char *ssid = "realme 7 pro";
const char *password = "9460575212";

// MQTT credentials
const char mqttServer[] = "io.adafruit.com";
const int mqttPort = 1883;
const char mqttUser[] = "";
const char mqttPassword[] = "";
const char mqttPublishTopic[] = "";
const char mqttSubscribeTopic[] = "";

unsigned long lastPublishTime = 0;           // Tracks the last publish time
const unsigned long publishInterval = 10000;  // Publish interval (5 seconds)

void setup() {
  // Initialize Serial for debugging
  SerialMon.begin(115200);
  //while (!SerialMon) {
     // Wait for Serial Monitor connection
  delay(2000);  // Give the sensor some time to initialize
  dht.begin();

  // Initialize Wi-Fi
  SerialMon.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    SerialMon.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // OTA Setup
 ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
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

  SerialMon.println("Ready");
  SerialMon.print("IP address: ");
  SerialMon.println(WiFi.localIP());
  WebSerial.begin(&server);
    /* Attach Message Callback */
    WebSerial.msgCallback(recvMsg);
    server.begin();

  // Initialize Serial2 for EC200U communication
  SerialAT.begin(115200, SERIAL_8N1, RX2, TX2);
  delay(1000);

  // Initialize MQTT connection
  initializeMQTT();
  SerialMon.println("Stopping any existing GPS session...");
  sendATCommand("AT+QGPSEND", "OK", 2000);

  // Start GPS session
  SerialMon.println("Starting GPS...");
  sendATCommand("AT+QGPS=1", "OK", 2000);
  delay(10000);
}

void loop() {
  // Handle OTA updates
  ArduinoOTA.handle();
  checkWiFiConnection();
  float temp = dht.readTemperature();
  float humidity = dht.readHumidity();

  // Check if readings are valid
  if (isnan(temp) || isnan(humidity)) {
    Serial.println("Error: Failed to read from DHT sensor!");
  } else {
    Serial.print("Temp: ");
    Serial.print(temp);
    Serial.print(" C ");
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" % ");
  }

  // Handle MQTT communication
  mqttLoop();
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    SerialMon.println("Wi-Fi disconnected, attempting to reconnect...");
    WiFi.reconnect();
  }
}

void initializeMQTT() {
  SerialMon.println("Initializing MQTT connection...");
  WebSerial.println("Initializing MQTT connection...");


  // Configure the APN for the network
  sendATCommand("AT+QICSGP=1,1,\"www\",\"\",\"\",1", "OK", 2000); // Replace 'your_apn' with your actual APN
  sendATCommand("AT+CGATT=1", "OK", 5000);                             // Attach to the network
  sendATCommand("AT+QIACT=1", "OK", 5000);                             // Activate PDP context

  sendATCommand("AT+QMTDISC=0", "OK", 2000);                                      // Disconnect previous sessions
  sendATCommand("AT+QMTOPEN=0,\"" + String(mqttServer) + "\"," + String(mqttPort), "OK", 5000);  // Open connection
  sendATCommand("AT+QMTCONN=0,\"123\",\"" + String(mqttUser) + "\",\"" + mqttPassword + "\"", "OK", 5000);  // Connect to broker

  sendATCommand("AT+QMTSUB=0,1,\"" + String(mqttSubscribeTopic) + "\",0", "OK", 5000);  // Subscribe to topic
}

void mqttLoop() {
  unsigned long currentMillis = millis();

  // Check if it's time to publish data
  if (currentMillis - lastPublishTime >= publishInterval) {
    publishData();
    lastPublishTime = currentMillis;
  }

  // Check for incoming MQTT messages
  checkForMQTTMessages();
}
void publishData() {
  static int counter = 0;              // Data to publish
  //String payload = String(counter++);  // Increment data to publish
  String gpsData = getGPSData();
  String latitude = "N/A";
  String longitude = "N/A";
  float temp = dht.readTemperature();
  float humidity = dht.readHumidity();
  if (isnan(temp) || isnan(humidity)) {
    Serial.println("Error: Failed to read from DHT sensor!");
  } else {
    Serial.print("Temp: ");
    Serial.print(temp);
    Serial.print(" C ");
    Serial.print("Humidity: ");
    Serial.print(humidity);
    Serial.println(" % ");
  }

  if (gpsData.length() > 0) {
    int comma1 = gpsData.indexOf(",");
    int comma2 = gpsData.indexOf(",", comma1 + 1);
    int comma3 = gpsData.indexOf(",", comma2+1);
    if (comma1 != -1 && comma2 != -1) {
      latitude = gpsData.substring(comma1+1, comma2);
      longitude = gpsData.substring(comma2+1, comma3);
    }
  }

  String payload = String(counter++) + "," + latitude + "," + longitude + "," + temp + "," + humidity;

  SerialMon.println("Publishing data: " + payload);
  WebSerial.println("Publishing data: " + payload);
  sendATCommand("AT+QMTPUB=0,0,0,0,\"" + String(mqttPublishTopic) + "\"", ">", 5000);
  SerialAT.print(payload);
  SerialAT.write(26);  // End of input with Ctrl+Z
}

void checkForMQTTMessages() {
  while (SerialAT.available()) {
    String response = SerialAT.readString();

    if (response.indexOf(mqttSubscribeTopic) != -1) {
      SerialMon.println("Message received on subscribed topic!");
      WebSerial.println("Message received on subscribed topic!");
      int startIndex = response.indexOf(',') + 1;
      String message = response.substring(startIndex);
      SerialMon.println("Message content: " + message);
      WebSerial.println("Message content: " + message);
    }
  }
}

void sendATCommand(String command, String response, int timeout) {
  String receivedData;
  SerialAT.println(command); // Send the AT command
  long int time = millis();
  while ((time + timeout) > millis()) {
    while (SerialAT.available()) {
      char c = SerialAT.read(); // Read the response from the module
      receivedData += c;
    }
  }
  if (receivedData.indexOf(response) != -1) {
    SerialMon.println("Received: " + receivedData); // If the response is as expected
    WebSerial.println("Received: " + receivedData);
  } else {
    SerialMon.println("Error");
    WebSerial.println("Error");
  }
}
void recvMsg(uint8_t *data, size_t len){
  WebSerial.println("Received Data...");
  String d = "";
  for(int i=0; i < len; i++){
    d += char(data[i]);
  }
  WebSerial.println(d);
}

String getGPSData() {
  // sendATCommand("AT+QGPSLOC=2\r\n", "+QGPSLOC:", 2000); // Request GPS location
  SerialAT.println("AT+QGPSLOC=2\r\n");
  String gpsResponse;
  long int time = millis();
  while ((time + 2000) > millis()) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      Serial.println(c);
      gpsResponse += c;
    }
  }

  int startIndex = gpsResponse.indexOf("+QGPSLOC: ");
  if (startIndex != -1) {
    int endIndex = gpsResponse.indexOf("\r\n", startIndex);
    if (endIndex != -1) {
      String location = gpsResponse.substring(startIndex + 10, endIndex);
      SerialMon.println("GPS Data: " + location);
      WebSerial.println("GPS Data: " + location);

      return location;
    }
  }

  SerialMon.println("Invalid GPS response: " + gpsResponse);
  WebSerial.println("Invalid GPS response: " + gpsResponse);
  return "";
}
