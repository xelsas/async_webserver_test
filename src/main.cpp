#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>

// Define the maximum lengths of the led marquee text, and wifi credentials.
// The 802.11 standard defines that the SSID by definition can not be more
// than 32 characters, and the password can not be longer than 63 characters.
#define LED_MARQUEE_TEXT_LENGTH 1024
#define SSID_LENGTH 33
#define PASSSWORD_LENGTH 64

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 8
#define CS_PIN 5

// GIOP21 pin connected to wifi reset button
#define BUTTON_PIN 21
int lastState = HIGH;
int currentState;

Preferences preferences;

// WiFi credentials
char ssid[SSID_LENGTH] = "";
char password[PASSSWORD_LENGTH] = "";

IPAddress ap_ip(192, 168, 1, 1);
const byte dns_port = 53;
// Max 32 characters
const char* ap_ssid = "ESP32";
// Max 63 characters
const char* ap_password = "1234567890";

const char* mdns_name = "esp32";

bool restart_wifi = false;
bool auto_reconnect_wifi = false;
bool handle_dns_requests = false;

DNSServer dns_server;

// WebServer https://github.com/me-no-dev/ESPAsyncWebServer
AsyncWebServer server(80);

// https://registry.platformio.org/libraries/majicdesigns/MD_MAX72XX
// https://registry.platformio.org/libraries/majicdesigns/MD_Parola
MD_Parola led_marquee = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// Make sure that we're only controlling and updating the led marquee in the loop() function,
// so communicate through variables if the text needs to be updated.
char led_marquee_text[LED_MARQUEE_TEXT_LENGTH] = "scrolling text";
bool led_marquee_text_changed = true;

char const* config_html = "<!DOCTYPE html>"
                          "<html>"
                          "<head>"
                          " <style>"
                          "  :root {"
                          "    font-size:calc(16px + 0.2vw);"
                          "  }"
                          "  .main_content {"
                          "    box-sizing: content-box;"
                          "    margin-inline: auto;"
                          "    max-inline-size: 40rem;"
                          "    display: flex;"
                          "    flex-direction: column;"
                          "    justify-content: flex-start;"
                          "  }"
                          "  .main_content h1 {"
                          "    font-size: 2rem;"
                          "  }"
                          "  .main_content h2 {"
                          "    font-size: 1.75rem;"
                          "  }"
                          "  .main_content > * {"
                          "    margin-block: 0;"
                          "  }"
                          "  .main_content > * + * {"
                          "    margin-block-start: var(--space, 1.5rem);"
                          "  }"
                          "  .main_content_item input {"
                          "    display: block;"
                          "    padding: 0.25rem 0.5rem;"
                          "    box-sizing: border-box;"
                          "    width: 100%%;"
                          "    height: 2rem;"
                          "    font-size: 1rem;"
                          "    vertical-align: middle;"
                          "  }"
                          "  .main_content_item input[type=text], .main_content_item input[type=password] {"
                          "    border: none;"
                          "    background: #f1f1f1;"
                          "  }"
                          "  .main_content_item input[type=text]:focus, .main_content_item input[type=password]:focus {"
                          "    background-color: #ddd;"
                          "    outline: none;"
                          "  }"
                          "  .main_content_item .btn {"
                          "    background-color: #42ebeb;"
                          "    color: white;"
                          "    padding: 0.25rem 0.5rem;"
                          "    border: none;"
                          "    cursor: pointer;"
                          "    width: 100%;"
                          "    opacity: 0.9;"
                          "  }"
                          "  .btn:hover {"
                          "    opacity: 1;"
                          "  }"
                          " </style>"
                          "</head>"
                          "<body>"
                          " <div class=\"main_content\">"
                          "  <h1>ESP32 Web Server</h1>"
                          "  <div class=\"main_content_item\">"
                          "   <h1>LED Matrix Settings</h1>"
                          "   <form method='post' action='/'>"
                          "    <label>LED Matrix Text<br><input type='text' name='data' value='%s' /></label><br>"
                          "    <input class=\"btn\" type='submit' value='Submit LED Matrix Settings' />"
                          "   </form>"
                          "  </div>"
                          "  <div class=\"main_content_item\">"
                          "   <h1>WiFi Settings</h1>"
                          "   <form method='post' action='/'>"
                          "    <label>SSID<br><input type='text' name='ssid' value='%s' /></label><br>"
                          "    <label>Password<br><input type='password' name='password' value='%s' /></label><br>"
                          "    <input class=\"btn\" type='submit' value='Submit WiFi Settings' />"
                          "   </form>"
                          "  </div>"
                          " </div>"
                          "</body>"
                          "</html>";

/**
 * Function to handle any 404 response
 * 
 * @param AsyncWebServerRequest *request
 */
void handleNotFound(AsyncWebServerRequest *request) {
  request->send(404, "text/plain", "Not Found");
}

/**
 * Builds the html string prefilled with the values that need to be prefilled.
 */
char* buildConfigPageHtml() {
  char* buffer = (char*)malloc(snprintf(NULL, 0, config_html, led_marquee_text, ssid, password) + 1);
  sprintf(buffer, config_html, led_marquee_text, ssid, password);

  return buffer;
}

/**
 * Handle the post request to set the new text on the led marquee, if set.
 * 
 * @param AsyncWebServerRequest *request
 */
void handleLedMarqueeConfigPostRequest(AsyncWebServerRequest *request) {
  if (!request->hasParam("data", true, false)) {
    return;
  }

  AsyncWebParameter* p_data = request->getParam("data", true, false);

  // Set the new text, but make sure it doesn't exceed the maximum size.
  led_marquee_text[0] = '\0';
  strncat(led_marquee_text, p_data->value().c_str(), LED_MARQUEE_TEXT_LENGTH - 1);
  // Set the boolean marking that the text that needs to be displayed has changed to true
  led_marquee_text_changed = true;

  Serial.printf("Received data: %s\n", led_marquee_text);
}

/**
 * Handle the post request to set the new WiFi credentials, if set.
 * 
 * @param AsyncWebServerRequest *request
 */
void handleWifiCredentialsConfigPostRequest(AsyncWebServerRequest *request) {
  if (!request->hasParam("ssid", true, false) || !request->hasParam("password", true, false)) {
    return;
  }

  AsyncWebParameter* p_ssid = request->getParam("ssid", true, false);
  AsyncWebParameter* p_password = request->getParam("password", true, false);

  Serial.printf("Received SSID: %s\n", p_ssid->value().c_str());

  if (p_ssid->value().length() < SSID_LENGTH && p_password->value().length() < PASSSWORD_LENGTH) {
    strcpy(ssid, p_ssid->value().c_str());
    strcpy(password, p_password->value().c_str());

    preferences.begin("app.settings", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.end();


    restart_wifi = true;
  }
}

/**
 * Function to handle the configuration page post requests. Should give error
 * feedback to the user on invalid input, but doesn't.
 * 
 * @param AsyncWebServerRequest *request
 */
void handleConfigPostRequest(AsyncWebServerRequest *request) {
  handleLedMarqueeConfigPostRequest(request);

  handleWifiCredentialsConfigPostRequest(request);

  char* buffer = buildConfigPageHtml();
  request->send(200, "text/html", buffer);
}

/**
 * Setup the web server routing:
 * / root for the configuration page
 * any other path should respond with a 404
 */
void setupWebServerRouting() {
  // Setup the root get handler
  server.on(
    "/",
    HTTP_GET,
    [](AsyncWebServerRequest *request){
      char* buffer = buildConfigPageHtml();
      request->send(200, "text/html", buffer);
    }
  );
  // Setup the root post handler
  server.on(
    "/",
    HTTP_POST,
    handleConfigPostRequest
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
  Serial.print(info.wifi_sta_disconnected.reason);
  Serial.print('\n');

  if (auto_reconnect_wifi) {
    Serial.println("Attempting reconnect.");
    WiFi.begin(ssid, password);
  } else {
    Serial.println("Reconnect not allowed.");
  }
}

/**
 * Handle WiFi setup, try to connect and fall back to starting as access point.
 */
void setupWiFi() {
  preferences.begin("app.settings", false);
  String s_ssid = preferences.getString("ssid", "");
  String s_password = preferences.getString("password", "");
  preferences.end();

  if (s_ssid.length() < SSID_LENGTH && s_password.length() < PASSSWORD_LENGTH) {
    strcpy(ssid, s_ssid.c_str());
    strcpy(password, s_password.c_str());
  }

  if (ssid == "" || password == ""){
    Serial.println("No values saved for ssid or password");
  }

  // Prevent automatic reconnection
  auto_reconnect_wifi = false;
  // Stop DNS requests from being handled
  handle_dns_requests = false;
  // Disconnect if connected, and stop the dns server
  dns_server.stop();
  // Stop the mDNS service if it is running
  MDNS.end();
  WiFi.disconnect(true, true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  
  if (strlen(ssid) > 0) {
    int count = 0;
    WiFi.begin(ssid, password);
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

  Serial.print("Starting mDNS... ");
  if(MDNS.begin(mdns_name)) {
    Serial.print("success.\n");
  } else {
    Serial.print("failed.\n");
  }
}

/**
 * The standard Arduino setup function used for setup and configuration tasks.
 */
void setup() {
  Serial.begin(115200);         
  delay(10);
  Serial.println('\n');

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Register WiFi events
  WiFi.onEvent(handleEventStaConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(handleEventStaGotIp, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(handleEventStaDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
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
  // Reset WiFi if we're going from low to high
  currentState = digitalRead(BUTTON_PIN);
  if(lastState == LOW && currentState == HIGH) {
    preferences.begin("app.settings", false);
    preferences.clear();
    preferences.end();
    restart_wifi = true;
    strcpy(ssid, "");
    strcpy(password, "");
  }
  lastState = currentState;

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
    led_marquee.displayText(led_marquee_text, PA_CENTER, 100, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  }

  if (led_marquee.displayAnimate()) {
    led_marquee.displayReset();
  }
}
