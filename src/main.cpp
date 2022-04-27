#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Adafruit_PWMServoDriver.h>

// Define the maximum lengths of the led marquee text, and wifi credentials.
// The 802.11 standard defines that the SSID by definition can not be more
// than 32 characters, and the password can not be longer than 63 characters.
#define LED_MARQUEE_TEXT_LENGTH 1024
#define SSID_LENGTH 33
#define PASSSWORD_LENGTH 64

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 8
#define CS_PIN 5

// Servo control is done by pulse width modulation. The PCA9685
// used for this. First set the frequency of the pulse. This is
// written for an sg90, which expects a pulse every 20ms.

// PWM period is 20ms, so 50 Hz
#define PWM_FREQ 50

// PCA9685 is 12 bit, so it has 4096 ticks in a single period.
// You control the pulse width by specifying at what point in
// the 4096-part cycle to turn the PWM output ON and OFF.
// Ticks start at 0, so 4095 is max.

// Positional servos rotate 180 degrees.
// By definition the neutral or the middle position of a servo is
// always a pulse length of 1.5ms, or a pulse length of 307 ticks,
// start here when calibrating the servo. From here, find the left
// extreme by lowering the pulse length and the right extreme by
// raising the pulse length.

// For the sg90:
// ~0.6ms is all the way to the left
// ~2.4ms is all the way to the right
#define SERVO_MIN  132  // Found experimentally (this is approximately 0.645ms, this fits with the expectations)
#define SERVO_MAX  482  // 307-132+307

// The servo is connected to servo connector 0
#define SERVO_CONNECTOR 0

// I2C pins
#define I2C_SDA 21
#define I2C_SCK 22

int servo_angle = 0;
int current_servo_angle = 0;
int pwm = SERVO_MIN;

// WiFi credentials
char ssid[SSID_LENGTH] = "";
char password[PASSSWORD_LENGTH] = "";

IPAddress ap_ip(192, 168, 1, 1);
const byte dns_port = 53;
const char* ap_ssid = "ESP32";
const char* ap_password = "1234567890";

const char* mdns_name = "esp32";

bool restart_wifi = false;
bool auto_reconnect_wifi = false;
bool handle_dns_requests = false;

DNSServer dns_server;

// WebServer https://github.com/me-no-dev/ESPAsyncWebServer
AsyncWebServer server(80);

// PCA9685 at default I2C address
TwoWire wire = TwoWire(0);
Adafruit_PWMServoDriver pca9685 = Adafruit_PWMServoDriver(0x40);

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
                          "   <h1>Servo Settings</h1>"
                          "   <form method='post' action='/'>"
                          "    <label>Servo Angle<br><input type='numeric' name='servo_angle' value='%d' /></label><br>"
                          "    <input class=\"btn\" type='submit' value='Submit Servo Settings' />"
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
  char* buffer = (char*)malloc(snprintf(NULL, 0, config_html, led_marquee_text, servo_angle, ssid, password) + 1);
  sprintf(buffer, config_html, led_marquee_text, servo_angle, ssid, password);

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

    restart_wifi = true;
  }
}

/**
 * Handle the post request to set the new servo angle credentials, if set.
 * 
 * @param AsyncWebServerRequest *request
 */
void handleServoConfigPostRequest(AsyncWebServerRequest *request) {
  if (!request->hasParam("servo_angle", true, false)) {
    return;
  }

  // Set the servo angle if it is a number between 0 and 180
  String servo_angle_string = request->getParam("servo_angle", true, false)->value();
  bool is_numeric = servo_angle_string.length() > 0;

  for(int i=0;i<servo_angle_string.length();i++) {
    if(!isDigit(servo_angle_string.charAt(i))) {
      is_numeric = false;
      break;
    }
  }

  // If the input is not numeric, don't set the angle.
  if (!is_numeric) {
    return;
  }

  int input_angle = servo_angle_string.toInt();

  // If the input is not in the range 0 to 180, don't set the angle.
  if (input_angle < 0 || input_angle > 180) {
    return;
  }

  servo_angle = input_angle;
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

  handleServoConfigPostRequest(request);

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

  // Setup I2C
  wire.begin(I2C_SDA, I2C_SCK);

  // Setup PCA9685
  pca9685.begin();

  // Set PWM Frequency to 50Hz
  pca9685.setPWMFreq(PWM_FREQ);

  pca9685.setPWM(SERVO_CONNECTOR, 0, pwm);
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
    led_marquee.displayText(led_marquee_text, PA_CENTER, 100, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  }

  if (led_marquee.displayAnimate()) {
    led_marquee.displayReset();
  }

  if (current_servo_angle != servo_angle) {
    // Calculate PWM pulse width, and write to PCA9685
    pwm = map(servo_angle, 0, 180, SERVO_MIN, SERVO_MAX);
    current_servo_angle = servo_angle;
    pca9685.setPWM(SERVO_CONNECTOR, 0, pwm);
  }
  
}
