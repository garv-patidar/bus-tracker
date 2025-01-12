#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>

AsyncWebServer server(80);
// EC200U communication
#define SerialMon Serial       // Debug output to Serial Monitor
#define SerialAT Serial2       // Communication with EC200U

// Define RX2 and TX2 pins for Serial2
#define RX2 16  // Replace with your RX2 pin number
#define TX2 17  // Replace with your TX2 pin number

// Wi-Fi credentials
const char *ssid = "";
const char *password = "";

void setup() {
  // Initialize Serial for debugging
  SerialMon.begin(115200);
  
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

  // Set module to SMS text mode
  sendATCommand("AT+CMGF=1\r\n", "OK", 2000);

  // Enable GPS
  sendATCommand("AT+QGPS=1\r\n", "OK", 2000); // Turn on GPS
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
      // Sending an SMS with a custom message
      sendSMS("+91", "Hello from ESP32 and Quectel EC200U!");
    } else if (command == 'c') {
      // Making a call to your number
      makeCall("+91");
    } else if (command == 'r') {
      // Enable receiving SMS in text mode
      sendATCommand("AT+CNMI=2,2,0,0,0\r\n", "OK", 2000);
    } else if (command == 'g') {
      // Send GPS location via SMS
      sendGPSLocation("+91");
    }
  }
}

void sendATCommand(String command, String response, int timeout) {
  String receivedData;
  WebSerial.println(response);
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

void sendSMS(String number, String text) {
  sendATCommand("AT+CMGS=\"" + number + "\"\r\n", ">", 2000); // Command to send SMS
  SerialAT.print(text); // Text of the message
  delay(100);
  SerialAT.write(26); // CTRL+Z to send the message
}

void makeCall(String number) {
  sendATCommand("ATD" + number + ";\r\n", "OK", 20000); // Command to dial a number
}

void sendGPSLocation(String number) {
  String gpsData = getGPSData();
  if (gpsData != "") {
    String message = "My current location is: " + gpsData;
    sendSMS(number, message);
  } else {
    SerialMon.println("Failed to get GPS data.");
    WebSerial.println("Failed to get GPS data.");
  }
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
      int firstComma = location.indexOf(',');
      int secondComma = location.indexOf(',', firstComma + 1);
      int thirdComma = location.indexOf(',', secondComma + 1);
      String latitude = location.substring(firstComma + 1, secondComma);
      String longitude = location.substring(secondComma + 1, thirdComma);
      SerialMon.println(latitude);
      SerialMon.println(longitude);
      WebSerial.println(latitude);
      WebSerial.println(longitude);
      
      return "Latitude: " + latitude + ", Longitude: " + longitude;;
    }
  }

  SerialMon.println("Invalid GPS response: " + gpsResponse);
  WebSerial.println("Invalid GPS response: " + gpsResponse);
  return "";
}
// String getGPSData() {
//     sendATCommand("AT+QGPSLOC=2\r\n", "+QGPSLOC:", 2000);
//     String gpsResponse;
//     long int time = millis();
//     while ((time + 2000) > millis()) {
//         while (SerialAT.available()) {
//             char c = SerialAT.read();
//             gpsResponse += c;
//         }
//     }

//     // Look for +QGPSLOC: response
//     int startIndex = gpsResponse.indexOf("+QGPSLOC:");
//     if (startIndex != -1) {
//         // Split the response into values
//         // Format is: <UTC>,<lat>,<lon>,<hdop>,<altitude>,<fix>,<cog>,<spkm>,<spkn>,<date>,<nsat>
//         String locationData = gpsResponse.substring(startIndex);
        
//         // Split by comma to get individual values
//         int firstComma = locationData.indexOf(',');
//         int secondComma = locationData.indexOf(',', firstComma + 1);
//         int thirdComma = locationData.indexOf(',', secondComma + 1);
        
//         if (firstComma != -1 && secondComma != -1 && thirdComma != -1) {
//             String latitude = locationData.substring(firstComma + 1, secondComma);
//             String longitude = locationData.substring(secondComma + 1, thirdComma);
            
//             // Format the location string
//             String location = "Latitude: " + latitude + ", Longitude: " + longitude;
//             SerialMon.println("GPS Data: " + location);
//             WebSerial.println("GPS Data: " + location);
//             return location;
//         }
//     }

//     SerialMon.println("Invalid GPS response: " + gpsResponse);
//     WebSerial.println("Invalid GPS response: " + gpsResponse);
//     return "";
// }

void recvMsg(uint8_t *data, size_t len) {
  String command = "";
  for (int i = 0; i < len; i++) {
    command += char(data[i]);
  }

  command.trim(); // Remove any extra spaces or newlines

  WebSerial.println("Received Command: " + command);

  if (command == "s") {
    // Sending an SMS with a custom message
    String number = "+91"; // Replace with your number
    String message = "Hello from ESP32 and WebSerial!";
    WebSerial.println("Sending SMS to " + number);
    sendSMS(number, message);
  } 
  else if (command == "c") {
    // Making a call
    String number = "+91"; // Replace with your number
    WebSerial.println("Making a call to " + number);
    makeCall(number);
  } 
  else if (command == "r") {
    // Enable receiving SMS in text mode
    WebSerial.println("Enabling SMS reception in text mode.");
    sendATCommand("AT+CNMI=2,2,0,0,0\r\n", "OK", 2000);
  } 
  else if (command == "g") {
    // Send GPS location via SMS
    String number = "+91"; // Replace with your number
    WebSerial.println("Sending GPS location to " + number);
    sendGPSLocation(number);
  } 
  else {
    WebSerial.println("Invalid command. Use one of the following: s, c, r, g.");
  }
}

