#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFiManagerConfig.h>
#include <ArduinoOTA.h>

/* ----------------------------------------------------------- */

#define WEMOS_D1
//#define NODEMCU

#if defined(NODEMCU)
  #define PIN_LED_RED   12 // D6
  #define PIN_LED_GREEN  0 // D3
  #define PIN_LED_BLUE   2 // D4
  #define PIN_RCWL_0516 14 // D5
  #define PIN_LDR       17 // A0
  #define PIN_CONFIG    16 // D0
#elif defined(WEMOS_D1)
  #define PIN_LED_RED   16 // D0
  #define PIN_LED_GREEN 14 // D5
  #define PIN_LED_BLUE  12 // D6
  #define PIN_RCWL_0516 13 // D7
  #define PIN_LDR       17 // A0
#endif

#define S_RED 1
#define S_GREEN 2
#define S_BLUE 3
#define MAX_LED 1023

#define DEVICE_NAME  "ESP-Rabbit"
//#define DEVICE_NAME  "ESP-Test"
#define ROOM_CODE "slk2"
//#define ROOM_CODE "bur"

#define MQTT_SENSORS_BASE "sensors/" ROOM_CODE "/"
#define MQTT_ENVIRONMENT_BASE MQTT_SENSORS_BASE "env/"
#define MQTT_LIGHT_BASE "lights/" ROOM_CODE "/night/"
#define MQTT_TOPIC_RGB MQTT_LIGHT_BASE "rgb"
#define MQTT_TOPIC_THRESHOLD MQTT_LIGHT_BASE "threshold"
#define MQTT_TOPIC_TIMEOUT MQTT_LIGHT_BASE "timeout"
#define MQTT_TOPIC_STATE MQTT_LIGHT_BASE "state"
#define MQTT_TOPIC_SWITCH MQTT_LIGHT_BASE "switch"
#define MQTT_TOPIC_SWEEP_MODE MQTT_LIGHT_BASE "sweepMode"
#define MQTT_TOPIC_SWEEP_DELAY MQTT_LIGHT_BASE "sweepDelay"
#define MQTT_TOPIC_PRESENCE MQTT_SENSORS_BASE "presence"
#define MQTT_TOPIC_SENSITIVITY MQTT_SENSORS_BASE "sensitivity"

#define CONFIG_ID_MQTT_HOST "mqttHost"
#define CONFIG_ID_MQTT_PORT "mqttPort"
#define CONFIG_ID_LIGHT_TIMEOUT "presenceTimeout"
#define CONFIG_ID_LIGHT_TRESHOLD "lightThreshold"
#define CONFIG_ID_SWEEP_MODE "sweepMode"
#define CONFIG_ID_SWEEP_DELAY "sweepDelay"
#define CONFIG_ID_SENSITIVITY "sensitivity"

#define ON "ON"
#define OFF "OFF"
#define MODE_SWEEP1 "SWEEP1"
#define MODE_SWEEP2 "SWEEP2"
#define MODE_STATIC "STATIC"

DNSServer dnsServer;
ESP8266WebServer server(80);
Adafruit_BME280 bme;
WiFiManagerConfig config;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

bool darkness = false;
int lastLightValue = 0;
int currentStaticColor = S_BLUE;
float lastTemperature = 0.0;
float lastHumidity = 0.0;
float lastPressure = 0.0;
unsigned long lastLightReadTime = 0;
unsigned long lastBmeReadTime = 0;
unsigned long sweepMillis = 0;
unsigned long lastMqttReconnectAttempt = 0;

bool lastMovementState = false;
bool presence = false;
int presenceDelay = 5000;
int presenceAllowedOffTime = 2000;
unsigned long lastTimeMovementOn = 0;
unsigned long lastTimeMovementOff = 0;
unsigned long lastTimePresenceOff = 0;

int red = 0;
int green = 0;
int blue = 0;

bool ledOn = false;
bool sweep = true;

// begin setup ---------------------------------------------------------------------

void setupIO() 
{
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_RCWL_0516, INPUT);
  digitalWrite(PIN_RCWL_0516, LOW);
  pinMode(PIN_LDR, INPUT);
#if defined(NODEMCU)
  pinMode(PIN_CONFIG, INPUT);
#endif

  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting up...");

  if (!bme.begin(0x76))
  {   
      Serial.println("Could not find a valid BME280 sensor");
      while (1);
  }
  bme.setSampling(Adafruit_BME280::MODE_FORCED, 
                    Adafruit_BME280::SAMPLING_X1, // temperature 
                    Adafruit_BME280::SAMPLING_X1, // pressure 
                    Adafruit_BME280::SAMPLING_X1, // humidity 
                    Adafruit_BME280::FILTER_OFF   ); 
                    
  Serial.println("BME280 ok");
}

void setupConfigParameters()
{
  config.addParameter(CONFIG_ID_MQTT_HOST, "MQTT host name or ip", "192.168.0.180", 255);
  config.addParameter(CONFIG_ID_MQTT_PORT, "MQTT port", "1883", 5);
  config.addParameter(CONFIG_ID_LIGHT_TIMEOUT, "Light timeout (s)", "1800", 8);
  config.addParameter(CONFIG_ID_LIGHT_TRESHOLD, "Light treshold (0-1024)", "400", 5);
  config.addParameter(CONFIG_ID_SWEEP_MODE, "RGB sweep mode", MODE_SWEEP1, strlen(MODE_SWEEP1)+1);
  config.addParameter(CONFIG_ID_SWEEP_DELAY, "RGB sweep delay (ms)", "5", 8);
  config.addParameter(CONFIG_ID_SENSITIVITY, "Presence detection sensitivity (1-10)", "5", 3);
}

void setupWifi() 
{
  WiFiManager wifiManager;
  config.init(wifiManager);
#if defined(NODEMCU) 
  if (digitalRead(PIN_CONFIG) == LOW) 
  {
    if (!wifiManager.startConfigPortal("OnDemandAP")) 
    {
      Serial.println("failed to start Config Portal");
      delay(3000);
      ESP.reset();
    }
  }
  else
#endif
  {
    wifiManager.autoConnect();
  }
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void setupHttpServer() 
{
  server.on("/", httpHandleRoot); 
  server.begin();
  Serial.println("HTTP server started");
}

void setupMqtt()
{
  Serial.print("Set MQTT server: ");
  Serial.print(config.getValue(CONFIG_ID_MQTT_HOST));
  Serial.print(" port: ");
  Serial.println(config.getValue(CONFIG_ID_MQTT_PORT));
  mqttClient.setServer(config.getValue(CONFIG_ID_MQTT_HOST), config.getIntValue(CONFIG_ID_MQTT_PORT));
  mqttClient.setCallback(OnMqttMessageReceived);
}

void setupOTA()
{
  ArduinoOTA.setHostname(DEVICE_NAME);

  ArduinoOTA.onStart([]() {
    Serial.println("OTA Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nOTA End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OTA ready");
}

void setup() 
{
  setupIO();
  setupConfigParameters();
  setupWifi();
  OnConfigurationUpdated();
  setupOTA();
  setupHttpServer();
  setupMqtt();
  setLed(0, 0, 0);
  Serial.println("Ready.");
}

// end setup -----------------------------------------------------------------------

// begin HTTP ----------------------------------------------------------------------

String createHTML()
{
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>";
  ptr += DEVICE_NAME;
  ptr += "</title>\n";
  ptr += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
  ptr += "p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";
  ptr += "<div id=\"webpage\">\n";
  ptr += "<h1>";
  ptr += DEVICE_NAME;
  ptr += "</h1>\n";
  ptr += "<p>Temperature: ";
  ptr += lastTemperature;
  ptr += "&deg;C</p>";
  ptr += "<p>Humidity: ";
  ptr += lastHumidity;
  ptr += "%</p>";
  ptr += "<p>Pressure: ";
  ptr += lastPressure;
  ptr += "hPa</p>";
  ptr += "<p>Light value: ";
  ptr += lastLightValue;
  ptr += "</p>";
  ptr += "<p>Presence: ";
  ptr += (presence ? "yes" : "no");
  ptr += "</p>";
  ptr += "<p>Light on: ";
  ptr += (ledOn ? "yes" : "no");
  ptr += "</p>";
  ptr += "</div>\n";
  ptr += "</body>\n";
  ptr += "</html>\n";
  return ptr;
}

void httpHandleRoot() 
{
  Serial.println("HTTP handle root");
  
  readBme();
  server.send(200, "text/html", createHTML()); 
}

// end HTTP ----------------------------------------------------------------------

void loop() 
{  
  ArduinoOTA.handle();
  readBme();
  checkMovementDetectorState();
  readLightValue();
  server.handleClient();
  mqttLoop();
  rgbSweep();
}

void mqttLoop()
{
  if (!mqttClient.connected()) 
  {
    if (lastMqttReconnectAttempt == 0)
      Serial.println("MQTT disconnected");
      
    if (millis() - lastMqttReconnectAttempt > 5000) 
    {
      lastMqttReconnectAttempt = millis();
      Serial.println("attempt MQTT reconnect");
      if (reconnectMqtt()) 
      {
        lastMqttReconnectAttempt = 0;
      }
      else 
      {
        Serial.println("MQTT reconnect failed");
      }
    }
  }
  mqttClient.loop();
}

boolean reconnectMqtt() 
{
  if (mqttClient.connect(DEVICE_NAME)) {
    Serial.println("MQTT connected");
    mqttClient.subscribe(MQTT_TOPIC_RGB);
    mqttClient.subscribe(MQTT_TOPIC_THRESHOLD);
    mqttClient.subscribe(MQTT_TOPIC_TIMEOUT);
    mqttClient.subscribe(MQTT_TOPIC_SWITCH);
    mqttClient.subscribe(MQTT_TOPIC_SWEEP_MODE);
    mqttClient.subscribe(MQTT_TOPIC_SWEEP_DELAY);
    mqttClient.subscribe(MQTT_TOPIC_SENSITIVITY);
    return true;
  }
  return mqttClient.connected();
}

void readBme()
{
  if (lastBmeReadTime == 0 || millis() - lastBmeReadTime > 60000)
  {
    bme.takeForcedMeasurement();
    lastTemperature = bme.readTemperature();
    lastHumidity = bme.readHumidity();
    lastPressure = bme.readPressure() / 100.0F;
    lastBmeReadTime = millis();
    onBmeValuesChanged();
  }
}

void readLightValue()
{
  if (millis() - lastLightReadTime > 1000)
  {
    int newLightValue = analogRead(PIN_LDR);
    lastLightReadTime = millis();
    if (lastLightValue != newLightValue)
    {
      //Serial.print("Light value changed to ");
      //Serial.println(newLightValue);
      lastLightValue = newLightValue;
      onLightValueChanged();
    }
  }  
}

void onBmeValuesChanged()
{
  char buf[16];
  sprintf(buf, "%.2f", lastTemperature);
  mqttClient.publish(MQTT_ENVIRONMENT_BASE "temperature", buf);
  sprintf(buf, "%.2f", lastHumidity);
  mqttClient.publish(MQTT_ENVIRONMENT_BASE "humidity", buf);
  sprintf(buf, "%.2f", lastPressure);
  mqttClient.publish(MQTT_ENVIRONMENT_BASE "pressure", buf);
}

void onLightValueChanged()
{
  boolean newDarkness = lastLightValue < config.getIntValue(CONFIG_ID_LIGHT_TRESHOLD);

  if (newDarkness != darkness)
  {
    Serial.print("Darkness ");
    Serial.println(newDarkness ? "on" : "off");
    darkness = newDarkness;
    if (darkness)
      changeLedState(presence);
  }
}

void checkMovementDetectorState()
{
  bool movementState = digitalRead(PIN_RCWL_0516);
  if (movementState != lastMovementState)
  {
    lastMovementState = movementState;
    if (movementState)
      onMovementDetected();
    else
      onNoMovementDetected();
  }
  
  if (movementState)
    checkPresence();
  else
    checkPresenceTimeout();
}

void onMovementDetected()
{
  unsigned long now = millis();
  Serial.print(millis());
  Serial.println(" Movement detected");
  if (lastTimeMovementOff > 0)
  {
    if (now - lastTimeMovementOff < presenceAllowedOffTime)
    {
      // off time was smaller than threshold, pretend it was on all the time
      lastTimeMovementOff = 0;
      return;
    }
  }
  lastTimeMovementOff = 0;
  lastTimeMovementOn = now;
}

void onNoMovementDetected()
{  
  Serial.print(millis());
  Serial.println(" No more movement detected");
  lastTimeMovementOff = millis();
}

void checkPresence()
{
  if (presence || millis() - lastTimeMovementOn < presenceDelay)
    return;

  Serial.print(millis());
  Serial.println(" Presence ON");
  presence = true;
  mqttClient.publish(MQTT_TOPIC_PRESENCE, "true");

  if (darkness)
    changeLedState(true);
}

void checkPresenceTimeout()
{  
  if (millis() - lastTimeMovementOff < presenceDelay) 
    return;

  if (presence)
  {
    lastTimePresenceOff = millis();
    Serial.print(lastTimePresenceOff);
    Serial.println(" Presence OFF");
    presence = false;
    mqttClient.publish(MQTT_TOPIC_PRESENCE, "false");  
  }
  checkLightTimeout();
}

void checkLightTimeout()
{  
  if (ledOn && millis() - lastTimePresenceOff > config.getIntValue(CONFIG_ID_LIGHT_TIMEOUT) * 1000)
  {
    Serial.print(millis());
    Serial.println(" Light timed out");
    changeLedState(false);
  }
}

void setSensitivity(int sensitivity)
{
  // convert sensitivity value (1-10) to times 
  // 1 = 10000ms
  // 10 = 0ms
  presenceDelay = (11 - sensitivity) * 1000;

  // 1 = 300ms
  // 10 = 3000ms
  presenceAllowedOffTime = sensitivity * 300;
}

int mapColor(String value)
{
  int numValue = value.toInt();
  if (numValue < 0) numValue = 0;
  if (numValue > 255) numValue = 255;
  return map(numValue, 0, 255, 0, MAX_LED);
}

void rgbSweep()
{
  if (!sweep || !ledOn) return;
  if (millis() - sweepMillis < config.getIntValue(CONFIG_ID_SWEEP_DELAY))
    return;

  sweepMillis = millis();

  if (red == 0 && green == 0 && blue == 0)
  {
    // start sweep
    currentStaticColor = S_BLUE;
    green = MAX_LED;
  }

  String sweepMode = config.getValue("sweepMode");
  if (sweepMode == String(MODE_SWEEP1))
    sweep1();
  if (sweepMode == String(MODE_SWEEP2))
    sweep2();
}

// change 1 led color at a time: normal rgb sweep
void sweep1()
{   
  switch(currentStaticColor)
  {
    case S_RED:
      red = MAX_LED;
      if (blue > 0)
        blue--;  
      else 
        green++;
      if (green == MAX_LED)
        currentStaticColor = S_GREEN;
      break;
    case S_GREEN:
      green = MAX_LED;
      if (red > 0)
        red--;  
      else 
        blue++;
      if (blue == MAX_LED)
        currentStaticColor = S_BLUE;
      break;
    case S_BLUE:
      blue = MAX_LED;
      if (green > 0)
        green--;  
      else 
        red++;
      if (red == MAX_LED)
        currentStaticColor = S_RED;
      break;
  }
  setLed(red, green, blue);
}

// change 2 led colors at a time: pastel effect
void sweep2()
{ 
  switch(currentStaticColor)
  {
    case S_RED:
      red = MAX_LED;
      green++;
      blue--;  
      if (blue <= 0)
      {
        currentStaticColor = S_GREEN;
      }
      break;
    case S_GREEN:
      green = MAX_LED;
      blue++;
      red--;
      if (red <= 0)
      {
        currentStaticColor = S_BLUE;
      }
      break;
    case S_BLUE:
      blue = MAX_LED;
      red++;
      green--;
      if (green <= 0)
      {
        currentStaticColor = S_RED;
      }
      break;
  }
  setLed(red, green, blue);
}

void setLed(int r, int g, int b)
{
  if (!ledOn)
  {
    r = 0;
    g = 0;
    b = 0;
  }

  analogWrite(PIN_LED_RED, r);
  analogWrite(PIN_LED_GREEN, g);
  analogWrite(PIN_LED_BLUE, b);  
}

void OnConfigurationUpdated()
{
  setSensitivity(config.getIntValue(CONFIG_ID_SENSITIVITY));
}

void OnMqttMessageReceived(char* topic, byte* payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] [length=");
  Serial.print(length);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (String(topic) == String(MQTT_TOPIC_THRESHOLD)) 
  {
    int result = atoi((const char*)payload);
    if (result < 0 || result > 1024)
      return;
    
    Serial.print("Set light threshold to ");
    Serial.println(result);
    config.setValue(CONFIG_ID_LIGHT_TRESHOLD, result);
    config.saveConfiguration();
    onLightValueChanged();
  }
  if (String(topic) == String(MQTT_TOPIC_TIMEOUT)) 
  {
    int result = atoi((const char*)payload);
    if (result < 1 || result > 999999)
      return;

    Serial.print("Set light timeout to (s) ");
    Serial.println(result);
    config.setValue(CONFIG_ID_LIGHT_TIMEOUT, result);
    config.saveConfiguration();
    checkPresenceTimeout();
  }
  if (String(topic) == String(MQTT_TOPIC_SWITCH)) 
  {
    if (startsWith((const char*)payload, length, ON))
    {
      Serial.println("Turn on light");
      changeLedState(true);
      return;
    }
    if (startsWith((const char*)payload, length, OFF))
    {
      Serial.println("Turn off light");
      changeLedState(false);
      return;
    }
  }
  if (String(topic) == String(MQTT_TOPIC_SWEEP_MODE)) 
  {
    // expecting one of: SWEEP1 SWEEP2 STATIC
    if (startsWith((const char*)payload, length, MODE_SWEEP1))
      setSweepMode(MODE_SWEEP1);
    else if (startsWith((const char*)payload, length, MODE_SWEEP2))
      setSweepMode(MODE_SWEEP2);
    else if (startsWith((const char*)payload, length, MODE_STATIC))
      setSweepMode(MODE_STATIC);
    return;
  }
  if (String(topic) == String(MQTT_TOPIC_SWEEP_DELAY)) 
  {
    int result = atoi((const char*)payload);
    if (result < 1 || result > 999999)
      return;

    Serial.print("Set sweep delay to (ms) ");
    Serial.println(result);
    config.setValue(CONFIG_ID_SWEEP_DELAY, result);
    config.saveConfiguration();
    return;
  }
  if (String(topic) == String(MQTT_TOPIC_SENSITIVITY)) 
  {
    int result = atoi((const char*)payload);
    if (result < 1 || result > 10)
      return;

    Serial.print("Set sensitivity to ");
    Serial.println(result);
    setSensitivity(result);
    config.setValue(CONFIG_ID_SENSITIVITY, result);
    config.saveConfiguration();
    return;
  }
  if (String(topic) == String(MQTT_TOPIC_RGB)) 
  {
    // expecting a string with format: rrr,ggg,bbb
    int r, g, b;
    int result = sscanf((const char *)payload, "%d,%d,%d", &r, &g, &b);
    if (result > 0 && (r > 0 || g > 0 || b > 0))
    { 
      Serial.println("Set RGB value");
      sweep = false;
      red = map(r, 0, 255, 0, MAX_LED);
      green = map(g, 0, 255, 0, MAX_LED);
      blue = map(b, 0, 255, 0, MAX_LED);
      changeLedState(true);
    }
    else
    {
      sweep = true;
    }
  }
}

void setSweepMode(const char* sweepMode)
{
  Serial.print("Set sweep mode ");
  Serial.println(sweepMode);
  config.setValue(CONFIG_ID_SWEEP_MODE, sweepMode);
  config.saveConfiguration();
  sweep = strcmp(sweepMode, MODE_STATIC) != 0;
}

boolean startsWith(const char* str, int length, const char* token)
{
  if (length < strlen(token))
    return false;
  return strncmp(str, token, length) == 0;
}

void changeLedState(bool onOffState)
{
  if (ledOn == onOffState)
    return;
  
  ledOn = onOffState;
  setLed(red, green, blue);
  mqttClient.publish(MQTT_TOPIC_STATE, onOffState ? ON : OFF);  
}