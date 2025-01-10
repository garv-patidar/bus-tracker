#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h> // Include the ArduinoJson library for JSON parsing

const char *ssid = "";
const char *password = "";

// Define your server details
const char *serverUrl = "";

WiFiClientSecure client;

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

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Define your JSON data
  const char *jsonString = R"([
    {
        "latitude": 28.54668,
        "longitude": 77.18527
    },
    
    {
        "latitude": 28.5443502,
        "longitude": 77.1928497
    },
    {
        "latitude": 28.5443855,
        "longitude": 77.1929456
    },
    {
        "latitude": 28.5444492,
        "longitude": 77.1931241
    },
    {
        "latitude": 28.5444203,
        "longitude": 77.1930416
    },
    {
        "latitude": 28.5444893,
        "longitude": 77.1932394
    },
    {
        "latitude": 28.5445199,
        "longitude": 77.1933293
    },
    {
        "latitude": 28.5445564,
        "longitude": 77.1934265
    },
    {
        "latitude": 28.5445929,
        "longitude": 77.1935217
    },
    {
        "latitude": 28.54864,
        "longitude": 77.18561
    },
    {
        "latitude": 28.54867,
        "longitude": 77.18554
    },
    {
        "latitude": 28.54870857,
        "longitude": 77.18543857
    },
    {
        "latitude": 28.54874714,
        "longitude": 77.18533714
    },
    {
        "latitude": 28.54878571,
        "longitude": 77.18523571
    },
    {
        "latitude": 28.54882429,
        "longitude": 77.18513429
    },
    {
        "latitude": 28.54886286,
        "longitude": 77.18503286
    },
    {
        "latitude": 28.54890143,
        "longitude": 77.18493143
    },
    {
        "latitude": 28.54894,
        "longitude": 77.18483
    },
    
    
    {
        "latitude": 28.5448269,
        "longitude": 77.1941848
    }
]
)";

  // Parse the JSON data and send each location
  sendLocationsFromJson(jsonString);
}

void loop() {
  // Empty loop
}

void sendLocationsFromJson(const char *jsonString) {
  StaticJsonDocument<2048> doc;

  // Parse the JSON string
  DeserializationError error = deserializeJson(doc, jsonString);
  if (error) {
    Serial.print("JSON Parsing failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Loop through each object in the JSON array
  for (JsonObject location : doc.as<JsonArray>()) {
    double latitude = location["latitude"];
    double longitude = location["longitude"];

    // Send data to the server
    sendToServer(latitude, longitude);
    delay(1000); // Delay between sending requests
  }
}

void sendToServer(double latitude, double longitude) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    client.setInsecure();

    // Generate a unique URL with a timestamp (to avoid caching)
    String uniqueUrl = String(serverUrl) + "?timestamp=" + String(millis());

    // Use HTTPS with WiFiClientSecure
    http.begin(client, uniqueUrl);  // Use WiFiClientSecure for HTTPS connection

    // Set headers and payload
    String payload = String("{") +
                     "\"device_name\":\"testCar\"," +
                     "\"latitude\":" + String(latitude, 6) + "," +
                     "\"longitude\":" + String(longitude, 6) +
                     "}";

    // Set the content type header and other headers
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Api-Key", "");
    http.addHeader("Cache-Control", "no-cache");
    http.addHeader("Pragma", "no-cache");

    // Send the POST request with the payload
    int httpResponseCode = http.POST(payload);

    // Print the response code
    Serial.println(httpResponseCode);

    if (httpResponseCode > 0) {
      Serial.printf("Server Response: %s\n", http.getString().c_str());
    } else {
      Serial.printf("Error sending data: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end(); // End the HTTP request
  } else {
    Serial.println("WiFi not connected, skipping data send.");
    }
}
