#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// EC200U communication
#define SerialMon Serial       // Debug output to Serial Monitor
#define SerialAT Serial2       // Communication with EC200U

// Define RX2 and TX2 pins for Serial2
#define RX2 16  // Replace with your RX2 pin number
#define TX2 17  // Replace with your TX2 pin number

// Wi-Fi credentials
const char *ssid = "";
const char *password = "";

// MQTT credentials
const char mqttServer[] = "";
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
  while (!SerialMon) {
    ; // Wait for Serial Monitor connection
  }

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
        if (ArduinoOTA.getCommand() == U_FLASH) {
          type = "sketch";
        } else {  // U_SPIFFS
          type = "filesystem";
        }
        SerialMon.println("Start updating " + type);
      })
      .onEnd([]() {
        SerialMon.println("\nEnd");
      })
      .onProgress([](unsigned int progress, unsigned int total) {
        SerialMon.printf("Progress: %u%%\r", (progress / (total / 100)));
      })
      .onError([](ota_error_t error) {
        SerialMon.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
          SerialMon.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
          SerialMon.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
          SerialMon.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
          SerialMon.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
          SerialMon.println("End Failed");
        }
      });

  ArduinoOTA.begin();

  SerialMon.println("Ready");
  SerialMon.print("IP address: ");
  SerialMon.println(WiFi.localIP());

  // Initialize Serial2 for EC200U communication
  SerialAT.begin(115200, SERIAL_8N1, RX2, TX2);
  delay(1000);

  // Initialize MQTT connection
  initializeMQTT();
}

void loop() {
  // Handle OTA updates
  ArduinoOTA.handle();

  // Handle MQTT communication
  mqttLoop();
}

void initializeMQTT() {
  SerialMon.println("Initializing MQTT connection...");

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
  String payload = String(counter++);  // Increment data to publish

  SerialMon.println("Publishing data: " + payload);
  sendATCommand("AT+QMTPUB=0,0,0,0,\"" + String(mqttPublishTopic) + "\"", ">", 5000);
  SerialAT.print(payload);
  SerialAT.write(26);  // End of input with Ctrl+Z
}

void checkForMQTTMessages() {
  while (SerialAT.available()) {
    String response = SerialAT.readString();

    if (response.indexOf(mqttSubscribeTopic) != -1) {
      SerialMon.println("Message received on subscribed topic!");
      int startIndex = response.indexOf(',') + 1;
      String message = response.substring(startIndex);
      SerialMon.println("Message content: " + message);
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
  } else {
    SerialMon.println("Error");
  }
}
