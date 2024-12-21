#include <WiFi.h>
#include <ESPmDNS.h>
#include <NetworkUdp.h>
#include <ArduinoOTA.h>
#include <TinyGPSPlus.h>
#include <SoftwareSerial.h>
#include "config.h"
const char *ssid = "";
const char *password = "";
static const int RXPin = D7, TXPin = D6;
static const uint32_t GPSBaud = 9600;
TinyGPSPlus gps;
SoftwareSerial ss(RXPin, TXPin);

#include "config.h"  // Configuration for Adafruit IO
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
int value = 0;
double lat = 28.545684;
double lon = 77.185579;
double ele = 0;
double vel = 0;
#define IO_LOOP_DELAY 2000
unsigned long lastUpdate;

AdafruitIO_Feed *latitudeFeed = io.feed("latitude");

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

  Serial.begin(115200);
  ss.begin(GPSBaud);

  Serial.print("Connecting to Adafruit IO");
  io.connect();
  while(io.status() < AIO_CONNECTED) {
    Serial.print(".");
    delay(500);

  Serial.println();
  Serial.println(io.statusText());
  latitudeFeed->get();
  }


  // Port defaults to 3232
  // ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
  // ArduinoOTA.setHostname("myesp32");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else {  // U_SPIFFS
        type = "filesystem";
      }

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

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  ArduinoOTA.handle();

  // Check if the Adafruit IO connection is lost and reconnect
  if(io.status() != AIO_CONNECTED) {
    Serial.println("Connection lost, attempting to reconnect...");
    io.connect();

    // Wait for reconnection
    while(io.status() < AIO_CONNECTED) {
      Serial.print(".");
      delay(500);
    }
    
    Serial.println();
    Serial.println("Reconnected to Adafruit IO");
  }

  // Read GPS data
  while (ss.available() > 0) {
    gps.encode(ss.read());
  }

  /*if (gps.location.isUpdated()) {
    Serial.print("LAT="); Serial.print(gps.location.lat(), 6);
    Serial.print("LNG="); Serial.println(gps.location.lng(), 6);
  }*/

  // Keep Adafruit IO connection alive
  io.run();
  //printFloat(gps.speed.kmph(), gps.speed.isValid(), 6, 2);

  if (millis() > (lastUpdate + IO_LOOP_DELAY)) {
    Serial.print(value);
    Serial.print(",");
    Serial.print(lat, 6);
    Serial.print(",");
    Serial.print(lon, 6);
    Serial.print(",");
    Serial.println(vel, 2);
    //printFloat(gps.speed.kmph(), gps.speed.isValid(), 6, 2);

    latitudeFeed->save(value, lat, lon, vel);

    value += 1;
    lat = gps.location.lat();
    lon = gps.location.lng();
    ele += 1;
    vel = gps.speed.kmph();

    lastUpdate = millis();
  }
}
static void printFloat(float val, bool valid, int len, int prec)
{
  if (!valid)
  {
    while (len-- > 1)
      Serial.print('*');
    Serial.print(' ');
  }
  else
  {
    Serial.print(val, prec);
    int vi = abs((int)val);
    int flen = prec + (val < 0.0 ? 2 : 1); // . and -
    flen += vi >= 1000 ? 4 : vi >= 100 ? 3 : vi >= 10 ? 2 : 1;
    for (int i=flen; i<len; ++i)
      Serial.print(' ');
  }
  smartDelay(0);
}
/*void handleMessage(AdafruitIO_Data *data) {
  int received_value = data->toInt();
  double received_lat = data->lat();
  double received_lon = data->lon();
  double received_ele = data->ele();*/

  //Serial.println("Received data from Adafruit IO");
  static void smartDelay(unsigned long ms)
{
  unsigned long start = millis();
  do 
  {
    while (ss.available())
      gps.encode(ss.read());
  } while (millis() - start < ms);
}
