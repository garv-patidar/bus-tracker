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
#define SerialMon Serial   // Debug output to Serial Monitor
#define SerialAT Serial2   // Communication with EC200U

// Define RX2 and TX2 pins for Serial2
#define RX2 D7
#define TX2 D6
#define DHTPIN D9
#define DHTTYPE DHT22

DHT dht(DHTPIN, DHTTYPE);

// Wi-Fi credentials (for OTA and WebSerial)
const char *ssid = "";
const char *password = "";

// --- Your Server Details ---
const char serverHost[] = "";
const char serverPath[] = "";
const int httpsPort = 443; // Standard port for HTTPS
const char apiKey[] = "";
const char deviceName[] = "";
int consecutive_failures = 0;
const int MAX_FAILURES = 1;
// --- New counter variable ---
unsigned long dataCounter = 1;

unsigned long lastPublishTime = 0;
const unsigned long publishInterval = 5000; // Publish every 5 seconds
bool wifiConnected = false;

// =================================================================
// == SETUP AND LOOP ==
// =================================================================

void setup() {
  // Initialize Serial for debugging
  SerialMon.begin(115200);
  delay(2000);
  dht.begin();

  // Initialize Wi-Fi for OTA and WebSerial
  SerialMon.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 5000) {
    delay(500);
    SerialMon.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    SerialMon.println("\nWi-Fi connected!");
    SerialMon.print("IP address: ");
    SerialMon.println(WiFi.localIP());
    wifiConnected = true;
    setupOTA();
    WebSerial.begin(&server);
    WebSerial.msgCallback(recvMsg);
    server.begin();
  } else {
    SerialMon.println("\nWi-Fi not available, continuing with cellular only.");
    wifiConnected = false;
  }

  // Initialize Serial2 for EC200U communication
  SerialAT.begin(115200, SERIAL_8N1, RX2, TX2);
  delay(1000);

  // Initialize Cellular and HTTPS Connection (One-Time Setup)
  initializeCellular();
  initializeHTTPS();

  SerialMon.println("\n--- Initializing GPS ---");
  sendATCommand("AT+QGPSEND", "OK", 2000); // Stop any previous session

  // Start a new GPS session
  if (sendATCommand("AT+QGPS=1", "OK", 2000)) {
    SerialMon.println("GPS started successfully. Waiting for first fix...");
    // This delay gives the GPS module time to acquire satellite signals
    delay(8000);
  } else {
    SerialMon.println("Failed to start GPS.");
  }
  SerialMon.println("--- GPS Initialization Finished ---");
}

void loop() {
  if (wifiConnected) {
    ArduinoOTA.handle(); // Handle OTA only if Wi-Fi is connected
  }
  checkWiFiConnection();

  // Check if it's time to publish data
  unsigned long currentMillis = millis();
  if (currentMillis - lastPublishTime >= publishInterval) {
    publishData();
    lastPublishTime = currentMillis;
  }
}

// Function to publish data (sends the counter)
void publishData() {
  // 1. Send the current counter value to the server
  sendDataViaHTTPS(dataCounter);

  // 2. Increment the counter for the next transmission
  dataCounter++;
}

// =================================================================
// == MODEM INITIALIZATION FUNCTIONS (CALLED ONCE) ==
// =================================================================

void initializeCellular() {
  SerialMon.println("Initializing Cellular Connection...");
  sendATCommand("ATE0", "OK", 1000); // Echo off
  sendATCommand("AT+QICSGP=1,1,\"www\",\"\",\"\",1", "OK", 2000); // Set APN
  sendATCommand("AT+CGATT=1", "OK", 5000);       // Attach to the network
  sendATCommand("AT+QIACT=1", "OK", 10000);      // Activate PDP context
  SerialMon.println("Cellular Connection Initialized.");
}

/**
 * @brief Configures all HTTPS parameters. This is done only ONCE.
 */
void initializeHTTPS() {
  SerialMon.println("\n--- Configuring HTTPS Parameters (One-Time Setup) ---");

  // --- Step 1: Configure HTTP/SSL Parameters ---
  sendATCommand("AT+QHTTPCFG=\"contextid\",1", "OK", 2000);
  sendATCommand("AT+QHTTPCFG=\"sslctxid\",1", "OK", 2000);
  // **OPTIMIZATION**: Enable SNI (Server Name Indication), crucial for many modern servers
  sendATCommand("AT+QSSLCFG=\"sni\",1,1", "OK", 2000);
  sendATCommand("AT+QSSLCFG=\"sslversion\",1,4", "OK", 2000);
  sendATCommand("AT+QSSLCFG=\"ciphersuite\",1,0XFFFF", "OK", 2000);
  sendATCommand("AT+QSSLCFG=\"seclevel\",1,0", "OK", 2000);
  // Tell the module we will provide custom headers with the POST data
  sendATCommand("AT+QHTTPCFG=\"requestheader\",1", "OK", 2000);

  // --- Step 2: Set the URL (also only done once) ---
  String fullUrl = "https://" + String(serverHost) + String(serverPath);
  String setUrlCmd = "AT+QHTTPURL=" + String(fullUrl.length()) + ",80";

  if (sendATCommand(setUrlCmd, "CONNECT", 5000)) {
    SerialAT.println(fullUrl);
    // After sending the URL, we need to wait for the final "OK"
    waitForResponse("OK", 2000); // Helper to wait for the confirmation
    SerialMon.println("URL configured successfully.");
  } else {
    SerialMon.println("Failed to set URL.");
  }
  SerialMon.println("--- HTTPS Configuration Finished ---");
}

// =================================================================
// == DATA TRANSMISSION FUNCTIONS (CALLED IN LOOP) ==
// =================================================================

/**
 * @brief Sends data using the pre-configured HTTPS session. Much faster.
 */
void sendDataViaHTTPS(unsigned long counterValue) {
  SerialMon.println("\n--- Starting HTTPS POST Request ---");
  static int counter = 0;              // Data to publish
  //String payload = String(counter++);  // Increment data to publish
  String gpsData = getGPSData();
  String latitude = "28.545451";
  String longitude = "77.187215";
  if (gpsData.length() > 0) {
    int comma1 = gpsData.indexOf(",");
    int comma2 = gpsData.indexOf(",", comma1 + 1);
    int comma3 = gpsData.indexOf(",", comma2+1);
    if (comma1 != -1 && comma2 != -1) {
      latitude = gpsData.substring(comma1+1, comma2);
      longitude = gpsData.substring(comma2+1, comma3);
    }
  }
  // --- Step 1: Construct the POST Body (Payload) ---
  // String payload = "{\"device_name\":\"" + String(deviceName) + "\","
  //                  + "\"latitude\":\"28.548863\","
  //                  + "\"longitude\":\"77.185033\","
  //                  + "\"value\":" + String(counterValue) + "}";
 String payload = String("{") +
                 "\"device_name\":\"" + deviceName + "\"," +
                 "\"latitude\":\"" + latitude + "\"," +
                 "\"longitude\":\"" + longitude + "\"," +
                 "\"value\":" + String(counterValue, 6) +
                 "}";


  SerialMon.println("Sending Payload: " + payload);

  // --- Step 2: Construct the full HTTP Request (Headers + Body) ---
  // **OPTIMIZATION**: Replaced "Connection: close" with "Connection: keep-alive"
  String httpRequest = "POST " + String(serverPath) + " HTTP/1.1\r\n";
  httpRequest += "Host: " + String(serverHost) + "\r\n";
  httpRequest += "Api-Key: " + String(apiKey) + "\r\n";
  httpRequest += "Content-Type: application/json\r\n";
  httpRequest += "Content-Length: " + String(payload.length()) + "\r\n";
  httpRequest += "Connection: keep-alive\r\n"; // Ask server to keep connection open
  httpRequest += "\r\n";
  httpRequest += payload;

  // --- Step 3: Send the POST Request ---
  // String postCmd = "AT+QHTTPPOST=" + String(httpRequest.length()) + ",60,60";
  // if (sendATCommand(postCmd, "CONNECT", 10000)) {
  //   SerialAT.print(httpRequest);
  //   SerialMon.println("HTTP Request Sent. Waiting for server response...");
  //   // The module will respond with "OK" and then "+QHTTPPOST: ..."
  //   // We can wait for the final response here.
  //   waitForResponse("+QHTTPPOST:", 15000);
  // }

  // // --- Step 4: Read the Server's Response ---
  // // The old command here used "CONNECT", but after a POST, the response is immediate.
  // // We just need to wait for the URC (+QHTTPREAD) if it comes, or just read buffer.
  // if(sendATCommand("AT+QHTTPREAD=80", "OK", 5000)) {
  //    // The response is now printed inside the sendATCommand function
  //    SerialMon.println("Read successful.");
  // }

  // SerialMon.println("--- HTTPS POST Request Finished ---\n");

  String postCmd = "AT+QHTTPPOST=" + String(httpRequest.length()) + ",60,60";
    bool success = false;

    if (sendATCommand(postCmd, "CONNECT", 10000)) {
        SerialAT.print(httpRequest);
        //SerialMon.println("HTTP Request Sent. Waiting for server response...");
        
        if (waitForResponse("+QHTTPPOST:", 10000)) {
            if(sendATCommand("AT+QHTTPREAD=80", "OK", 5000)) {
                //SerialMon.println("Read successful. Data sent to server!");
                success = true;
            } else {
                //SerialMon.println("Error: Failed to read server response.");
            }
        } else {
            //SerialMon.println("Error: Did not get confirmation after sending POST data.");
        }
    } else {
        SerialMon.println("Error: Failed to initiate POST request. Check cellular connection.");
    }

    if (success) {
        consecutive_failures = 0;
        SerialMon.println("Transmission successful. Failure counter reset.");
    } else {
        consecutive_failures++;
        // Use log_message for consistent output, handle printf style
        String failureMsg = "Transmission failed. Consecutive failures: " + String(consecutive_failures);
        SerialMon.println(failureMsg);

        if (consecutive_failures >= MAX_FAILURES) {
            //SerialMon.println("Maximum failure limit reached. Restarting device...");
            delay(1000);
            ESP.restart();
        }
    }

    SerialMon.println("--- HTTPS POST Request Finished ---\n");
}


// =================================================================
// == HELPER FUNCTIONS ==
// =================================================================

/**
 * @brief **OPTIMIZED** Sends an AT command and waits for a specific response.
 * @return Returns true if the expected response is found, false on timeout.
 */
bool sendATCommand(const String& command, const String& expectedResponse, unsigned int timeout) {
  String response = "";
  // Clear any junk data in the serial buffer
  while (SerialAT.available()) {
    SerialAT.read();
  }

  SerialMon.println("Sending: " + command);
  SerialAT.println(command);

  unsigned long startTime = millis();
  while (millis() - startTime < timeout) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
    }
    if (response.indexOf(expectedResponse) != -1) {
      SerialMon.println("Received: \n" + response);
      return true; // Success
    }
  }

  SerialMon.println("Timeout! No response or unexpected response for: " + command);
  SerialMon.println("Received: \n" + response);
  return false; // Timeout
}

/**
 * @brief Helper function to wait for a response without sending a command first.
 * Useful for multi-stage command sequences.
 */
bool waitForResponse(const String& expectedResponse, unsigned int timeout) {
    String response = "";
    unsigned long startTime = millis();
    while (millis() - startTime < timeout) {
        while (SerialAT.available()) {
            char c = SerialAT.read();
            response += c;
        }
        if (response.indexOf(expectedResponse) != -1) {
            SerialMon.println("Received: \n" + response);
            return true;
        }
    }
    SerialMon.println("Timeout waiting for: " + expectedResponse);
    return false;
}

// --- Wi-Fi and OTA Management Functions (Unchanged) ---
void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    SerialMon.println("Wi-Fi disconnected. Attempting to reconnect...");
    WiFi.begin(ssid, password);
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 1000) {
      delay(500);
      SerialMon.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      SerialMon.println("\nReconnected to Wi-Fi!");
      wifiConnected = true;
      setupOTA();
    } else {
      SerialMon.println("\nWi-Fi still not available.");
    }
  }
}

void setupOTA() {
  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("Start updating " + type);
  }).onEnd([]() {
    Serial.println("\nEnd");
  }).onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  }).onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void recvMsg(uint8_t *data, size_t len) {
  WebSerial.println("Received Data...");
  String d = "";
  for (int i = 0; i < len; i++) {
    d += char(data[i]);
  }
  WebSerial.println(d);
}

String getGPSData() {
  // sendATCommand("AT+QGPSLOC=2\r\n", "+QGPSLOC:", 2000); // Request GPS location
  SerialAT.println("AT+QGPSLOC=2\r\n");
  SerialMon.println("Hello ");
  String gpsResponse;
  long int time = millis();
  while ((time + 1000) > millis()) {
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
      //WebSerial.println("GPS Data: " + location);

      return location;
    }
  }

  SerialMon.println("Invalid GPS response: " + gpsResponse);
  //WebSerial.println("Invalid GPS response: " + gpsResponse);
  return "";
}