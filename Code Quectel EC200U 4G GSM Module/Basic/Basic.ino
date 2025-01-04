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
const char *ssid = "realme 7 pro";
const char *password = "9460575212";

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

  // Set module to SMS text mode
  sendATCommand("AT+CMGF=1\r\n", "OK", 2000);
}

void loop() {
  // Handle OTA updates
  ArduinoOTA.handle();

  // Handle EC200U communication
  if (SerialAT.available()) {
    SerialMon.write(SerialAT.read()); // Forward data from EC200U to Serial Monitor
  }

  if (SerialMon.available()) {
    char command = SerialMon.read();  // Read command from Serial Monitor

    if (command == 's') {
      // Sending an SMS to your number with a custom message
      sendSMS("+919460575212", "Hello from ESP32 and Quectel EC200U!");
    } else if (command == 'c') {
      // Making a call to your number
      makeCall("+919460575212");
    } else if (command == 'r') {
      // Enable receiving SMS in text mode
      sendATCommand("AT+CNMI=2,2,0,0,0\r\n", "OK", 2000);
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

void sendSMS(String number, String text) {
  sendATCommand("AT+CMGS=\"" + number + "\"\r\n", ">", 2000); // Command to send SMS
  SerialAT.print(text); // Text of the message
  delay(100);
  SerialAT.write(26); // CTRL+Z to send the message
}

void makeCall(String number) {
  sendATCommand("ATD" + number + ";\r\n", "OK", 20000); // Command to dial a number
}
