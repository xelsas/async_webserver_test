#include <Arduino.h>
#ifdef ESP32
#include <WiFi.h>
#include <AsyncTCP.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#endif
#include <ESPAsyncWebServer.h>

// WiFi credentials
const char* ssid     = "";     
const char* password = "";

// WebServer https://github.com/me-no-dev/ESPAsyncWebServer
AsyncWebServer server(80);

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
  } else {
    Serial.println("Received no data.");
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
}

void loop() {}
