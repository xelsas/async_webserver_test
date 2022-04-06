#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include <SPI.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 8
#define CS_PIN 5

// WiFi credentials
const char* ssid     = "";     
const char* password = "";

// WebServer https://github.com/me-no-dev/ESPAsyncWebServer
AsyncWebServer server(80);

// https://registry.platformio.org/libraries/majicdesigns/MD_MAX72XX
// https://registry.platformio.org/libraries/majicdesigns/MD_Parola
MD_Parola led_marquee = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
String led_marquee_text = "scrolling text";
bool led_marquee_text_changed = true;

String config_html = "<!DOCTYPE html>"
                     "<html>"
                     "<head>"
                     "</head>"
                     "<body>"
                     " <h1>ESP32 Web Server</h1>"
                     " <form method='post' action='/'>"
                     "  <input type='text' name='data' value='' />"
                     "  <input type='submit' value='submit' />"
                     " </form>"
                     "</body>"
                     "</html>";

void handleNotFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not Found");
}

void handleConfigPostRequest(AsyncWebServerRequest *request) {
  int headers = request->headers();
  for(int i=0;i<headers;i++){
    Serial.printf("%s: %s\n", request->headerName(i).c_str(), request->header(i).c_str());
  }

  Serial.print("\n");
  if (request->hasParam("data", true, false)) {
    AsyncWebParameter* p_data = request->getParam("data", true, false);

    Serial.printf("Received data: %s\n", p_data->value().c_str());

    led_marquee_text = p_data->value();
    led_marquee_text_changed = true;
  }

  request->send(200, "text/html", config_html);
}

void setupWebServerRouting() {
  server.on(
    "/",
    HTTP_GET,
    [](AsyncWebServerRequest *request){
      request->send(200, "text/html", config_html);
    }
  );
  server.on(
    "/",
    HTTP_POST,
    handleConfigPostRequest
  );
}

/**
 * The standard Arduino setup function used for setup and configuration tasks.
 */
void setup() {
  Serial.begin(115200);         
  delay(10);
  Serial.println('\n');
  
  WiFi.begin(ssid, password);             
  while (WiFi.status() != WL_CONNECTED) 
  { 
    delay(500);
    Serial.print('.');
  }
  Serial.println('\n');
  Serial.println("Connected with IP address:\t");
  Serial.println(WiFi.localIP());
  Serial.println('\n');

  setupWebServerRouting();
  
  // Start web server
  server.begin();

  led_marquee.begin();
  led_marquee.setIntensity(0);
  led_marquee.displayClear();
}

void loop() {
  if (led_marquee_text_changed) {
    led_marquee_text_changed = false;
    led_marquee.displayClear();
    led_marquee.displayText(led_marquee_text.c_str(), PA_CENTER, 100, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  }

  if (led_marquee.displayAnimate()) {
    led_marquee.displayReset();
  }
}
