#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include <DNSServer.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 8
#define CS_PIN 5

// WiFi credentials
String ssid     = "";
String password = "";

IPAddress ap_ip(192, 168, 1, 1);
const byte dns_port = 53;
const char* ap_ssid     = "ESP32";
const char* ap_password = "1234567890";

bool restart_wifi = false;
bool auto_reconnect_wifi = false;
bool handle_dns_requests = false;

String wifi_config_html = "<!DOCTYPE html>"
                     "<html>"
                     "<head>"
                     "</head>"
                     "<body>"
                     " <h1>ESP32 Web Server</h1>"
                     " <form method='post' action='/wifi'>"
                     "  <label>SSID<input type='text' name='ssid' value='' /></label><br>"
                     "  <label>Password<input type='password' name='password' value='' /></label><br>"
                     "  <input type='submit' value='submit' />"
                     " </form>"
                     "</body>"
                     "</html>";

String config_html = "<!DOCTYPE html>"
                     "<html>"
                     "<head>"
                     "</head>"
                     "<body>"
                     " <h1>ESP32 Web Server</h1>"
                     " <form method='post' action='/'>"
                     "  <input type='text' name='data' value='' /><br>"
                     "  <input type='submit' value='submit' />"
                     " </form>"
                     "</body>"
                     "</html>";

DNSServer dns_server;

// WebServer https://github.com/me-no-dev/ESPAsyncWebServer
AsyncWebServer server(80);

// https://registry.platformio.org/libraries/majicdesigns/MD_MAX72XX
// https://registry.platformio.org/libraries/majicdesigns/MD_Parola
MD_Parola led_marquee = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// Make sure that we're only controlling and updating the led marquee in the loop() function,
// so communicate through variables if the text needs to be updated.
String led_marquee_text = "scrolling text";
bool led_marquee_text_changed = true;

/**
 * Function to handle any 404 response
 * 
 * @param AsyncWebServerRequest *request
 */
void handleNotFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not Found");
}

/**
 * Function to handle the configuration page post requests
 * 
 * @param AsyncWebServerRequest *request
 */
void handleConfigPostRequest(AsyncWebServerRequest *request) {
  if (request->hasParam("data", true, false)) {
    AsyncWebParameter* p_data = request->getParam("data", true, false);

    Serial.printf("Received data: %s\n", p_data->value().c_str());

    // Set the new text
    led_marquee_text = p_data->value();
    // Set the boolean marking that the text that needs to be displayed has changed to true
    led_marquee_text_changed = true;
  }

  request->send(200, "text/html", config_html);
}

/**
 * Function to handle the wifi configuration page post requests
 * 
 * @param AsyncWebServerRequest *request
 */
void handleWiFiConfigPostRequest(AsyncWebServerRequest *request) {
  if (request->hasParam("ssid", true, false) && request->hasParam("password", true, false)) {
    AsyncWebParameter* p_ssid = request->getParam("ssid", true, false);
    AsyncWebParameter* p_password = request->getParam("password", true, false);

    Serial.printf("Received SSID: %s\n", p_ssid->value().c_str());

    ssid     = p_ssid->value();
    password = p_password->value();
    
    restart_wifi = true;
  }

  request->send(200, "text/html", config_html);
}

/**
 * Setup the web server routing:
 * / root for the configuration page
 * /wifi for the wifi configuration page
 * any other path should respond with a 404
 */
void setupWebServerRouting() {
  // Setup the root get handler
  server.on(
    "/",
    HTTP_GET,
    [](AsyncWebServerRequest *request){
      request->send(200, "text/html", config_html);
    }
  );
  // Setup the root post handler
  server.on(
    "/",
    HTTP_POST,
    handleConfigPostRequest
  );

  // Setup the /wifi get handler
  server.on(
    "/wifi",
    HTTP_GET,
    [](AsyncWebServerRequest *request){
      request->send(200, "text/html", wifi_config_html);
    }
  );
  // Setup the /wifi post handler
  server.on(
    "/wifi",
    HTTP_POST,
    handleWiFiConfigPostRequest
  );

  server.onNotFound(handleNotFound);
}

/**
 * @param event
 * @param info
 */
void handleEventStaConnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("Connected with AP.");
}

/**
 * @param event
 * @param info
 */
void handleEventStaGotIp(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.print("Connected with IP address: ");
  Serial.print(WiFi.localIP());
  Serial.print('\n');
  auto_reconnect_wifi = true;
}

/**
 * @param event
 * @param info
 */
void handleEventStaDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.print("WiFi connection lost: ");
  Serial.print(info.disconnected.reason);
  Serial.print('\n');

  if (auto_reconnect_wifi) {
    Serial.println("Attempting reconnect.");
    WiFi.begin(ssid.c_str(), password.c_str());
  } else {
    Serial.println("Reconnect not allowed.");
  }
}

/**
 * Handle WiFi setup, try to connect and fall back to starting as access point.
 */
void setupWiFi() {
  // Prevent automatic reconnection
  auto_reconnect_wifi = false;
  // Stop DNS requests from being handled
  handle_dns_requests = false;
  // Disconnect if connected, and stop the dns server
  dns_server.stop();
  WiFi.disconnect(true, true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  
  if (ssid.length()) {
    int count = 0;
    WiFi.begin(ssid.c_str(), password.c_str());
    Serial.println("Setting up WiFi");
    while (WiFi.status() != WL_CONNECTED && count < 20) { 
      delay(500);
      count++;
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect(true, true);
    Serial.println("WiFi setup failed, starting as captive portal access point.");
    WiFi.softAPConfig(ap_ip, ap_ip, IPAddress(255, 255, 255, 0));
    WiFi.softAP(ap_ssid, ap_password);

    dns_server.start(dns_port, "*", ap_ip);
    handle_dns_requests = true;

    return;
  }
}

/**
 * The standard Arduino setup function used for setup and configuration tasks.
 */
void setup() {
  Serial.begin(115200);         
  delay(10);
  Serial.println('\n');

  // Register WiFi events
  WiFi.onEvent(handleEventStaConnected, SYSTEM_EVENT_STA_CONNECTED);
  WiFi.onEvent(handleEventStaGotIp, SYSTEM_EVENT_STA_GOT_IP);
  WiFi.onEvent(handleEventStaDisconnected, SYSTEM_EVENT_STA_DISCONNECTED);
  setupWiFi();

  setupWebServerRouting();
  
  // Start web server
  server.begin();
  
  // Handle led matrix setup
  led_marquee.begin();
  led_marquee.setIntensity(0);
  led_marquee.displayClear();
}

/**
 * The main loop handles updating the led matrix, and changes the text when needed.
 */
void loop() {
  if (handle_dns_requests) {
    dns_server.processNextRequest();
  }

  if (restart_wifi) {
    restart_wifi = false;

    setupWiFi();
  }
  
  if (led_marquee_text_changed) {
    led_marquee_text_changed = false;
    led_marquee.displayClear();
    led_marquee.displayText(led_marquee_text.c_str(), PA_CENTER, 100, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  }

  if (led_marquee.displayAnimate()) {
    led_marquee.displayReset();
  }
}
