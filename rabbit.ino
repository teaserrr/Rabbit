#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <WiFiManagerConfig.h>

/* ----------------------------------------------------------- */

#define WEMOS_D1
//#define NODEMCU

#if defined(NODEMCU)
  #define PIN_LED_RED   16 // D0
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
#define ROOM_CODE "slk2"
#define DEFAULT_MQTT_TOPIC_RGB "lights/" ROOM_CODE "/night/rgb"
#define DEFAULT_MQTT_TOPIC_THRESHOLD "lights/" ROOM_CODE "/night/threshold"
#define DEFAULT_MQTT_TOPIC_TIMEOUT "lights/" ROOM_CODE "/night/timeout"
#define DEFAULT_MQTT_BASE "sensors/" ROOM_CODE "/"
#define concat(first, second) first second


DNSServer dnsServer;
ESP8266WebServer server(80);
Adafruit_BME280 bme;
WiFiManagerConfig config;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

bool lastMovement = false;
bool presence = false;
bool darkness = false;
int lastLightValue = 0;
int currentStaticColor = S_BLUE;
int lightThreshold = 400;
int presenceTimeout = 1800;
float lastTemperature = 0.0;
float lastHumidity = 0.0;
float lastPressure = 0.0;
unsigned long lastMovementTime = 0;
unsigned long lastLightReadTime = 0;
unsigned long lastBmeReadTime = 0;
unsigned long sweepMillis = 0;
unsigned long lastMqttReconnectAttempt = 0;

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
  pinMode(PIN_CONFIG, INPUT);

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
  config.addParameter("test", "test parameter", "testValue", 255);
  config.addParameter("mqttHost", "MQTT host name or ip", "192.168.0.180", 255);
  config.addParameter("mqttPort", "MQTT port", "1883", 5);
}

void setupWifi() 
{
  WiFiManager wifiManager;
  config.init(wifiManager);
  
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
  server.on("/temperature", httpHandleTemperature);  
  server.on("/humidity", httpHandleHumidity);
  server.on("/pressure", httpHandlePressure);
  server.on("/led", httpHandleLed);
  server.begin();
  Serial.println("HTTP server started");
}

void setupMqtt()
{
  Serial.print("Set MQTT server: ");
  Serial.print(config.getValue("mqttHost"));
  Serial.print(" port: ");
  Serial.println(config.getValue("mqttPort"));
  //mqttClient.setServer("192.168.0.180", 1883);
  mqttClient.setServer(config.getValue("mqttHost"), config.getIntValue("mqttPort"));
  mqttClient.setCallback(OnMqttMessageReceived);
}

void setup() 
{
  setupIO();
  setupConfigParameters();
  setupWifi();
  setupHttpServer();
  setupMqtt();
  setLed(0, 0, 0);
  Serial.println("Ready.");
}

// end setup -----------------------------------------------------------------------

// begin HTTP ----------------------------------------------------------------------

String createHTML(float temperature, float humidity, float pressure){
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>ESP8266 Rabbit</title>\n";
  ptr += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;}\n";
  ptr += "p {font-size: 24px;color: #444444;margin-bottom: 10px;}\n";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";
  ptr += "<div id=\"webpage\">\n";
  ptr += "<h1>ESP8266 Rabbit</h1>\n";
  ptr += "<p>Temperature: ";
  ptr += temperature;
  ptr += "&deg;C</p>";
  ptr += "<p>Humidity: ";
  ptr += humidity;
  ptr += "%</p>";
  ptr += "<p>Pressure: ";
  ptr += pressure;
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

void httpHandleRoot() {
  Serial.println("HTTP handle root");
  
  readBme();
/*
  setLed(map(lastTemperature, 0, 35, 0, 1023),
         map(lastPressure, 1000, 1050, 0, 1023),
         map(lastHumidity, 0, 100, 0, 1023));  
    */
  server.send(200, "text/html", createHTML(lastTemperature, lastHumidity, lastPressure)); 
}

void httpHandleTemperature() {
  readBme();
  server.send(200, "text/plain", String(lastTemperature, 1));
}

void httpHandleHumidity() {
  readBme();
  server.send(200, "text/plain", String(lastHumidity, 1));
}

void httpHandlePressure() {
  readBme();
  server.send(200, "text/plain", String(lastPressure, 1));
}

void httpHandleLed() {

  if(server.args() > 0) {
    
    String red = server.arg("r");
    String green = server.arg("g");
    String blue = server.arg("b");

    setLed(mapColor(red), mapColor(green), mapColor(blue));
    sweep = false;
  }
  server.send(200, "text/plain", "ok");
}

// end HTTP ----------------------------------------------------------------------

void loop() 
{  
  readBme();
  checkMovement();
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
    mqttClient.subscribe(DEFAULT_MQTT_TOPIC_RGB);
    mqttClient.subscribe(DEFAULT_MQTT_TOPIC_THRESHOLD);
    mqttClient.subscribe(DEFAULT_MQTT_TOPIC_TIMEOUT);
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

void checkMovement()
{
  bool movement = digitalRead(PIN_RCWL_0516);
  if (movement != lastMovement)
  {
    lastMovement = movement;
    if (movement)
      onMovementDetected();
    else
      onNoMovementDetected();
      
    lastMovementTime = millis();
  }
  
  if (!movement)
  {
    checkPresenceTimeout();
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
  mqttClient.publish(concat(DEFAULT_MQTT_BASE, "env/temperature"), buf);
  sprintf(buf, "%.2f", lastHumidity);
  mqttClient.publish(concat(DEFAULT_MQTT_BASE, "env/humidity"), buf);
  sprintf(buf, "%.2f", lastPressure);
  mqttClient.publish(concat(DEFAULT_MQTT_BASE, "env/pressure"), buf);
}

void onLightValueChanged()
{
  boolean newDarkness = lastLightValue < lightThreshold;

  if (newDarkness != darkness)
  {
    Serial.print("Darkness ");
    Serial.println(newDarkness ? "on" : "off");
    darkness = newDarkness;
    evaluateTurnLedOnOrOff();
  }
}

void onMovementDetected()
{
  Serial.println("Movement detected");
  presence = true;
  evaluateTurnLedOnOrOff();
  mqttClient.publish(concat(DEFAULT_MQTT_BASE, "presence"), "true");  
}

void onNoMovementDetected()
{  
  mqttClient.publish(concat(DEFAULT_MQTT_BASE, "presence"), "false");  
}

void checkPresenceTimeout()
{  
  if (presence && millis() - lastMovementTime > presenceTimeout * 1000)
  {
    Serial.println("Presence timed out");
    presence = false;
    evaluateTurnLedOnOrOff();
  }
}

void evaluateTurnLedOnOrOff()
{
  // do not turn off again when it becomes lighter, this leads to blinking
  if (darkness || !presence)
    ledOn = presence;
  setLed(red, green, blue);
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
  if (millis() - sweepMillis < 5 /*config.getInt("sweepDelay")*/)
    return;

  sweepMillis = millis();

  if (red == 0 && green == 0 && blue == 0)
  {
    // start sweep
    currentStaticColor = S_BLUE;
    green = MAX_LED;
  }

  //String sweepType = config.getString("sweepType");
  //if (sweepType == String("sweep1"))
    sweep1();
  //if (sweepType == String("sweep2"))
  //  sweep2();
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

void OnMqttMessageReceived(char* topic, byte* payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (String(topic) == String(DEFAULT_MQTT_TOPIC_THRESHOLD)) 
  {
    int result = atoi((const char*)payload);
    
    Serial.print("Set light threshold to ");
    Serial.println(result);
    lightThreshold = result;
    onLightValueChanged();
  }
  if (String(topic) == String(DEFAULT_MQTT_TOPIC_TIMEOUT)) 
  {
    int result = atoi((const char*)payload);
    
    Serial.print("Set light timeout to (s) ");
    Serial.println(result);
    presenceTimeout = result;
    checkPresenceTimeout();
  }
  if (String(topic) == String(DEFAULT_MQTT_TOPIC_RGB)) 
  {
    int r, g, b;
    int result = sscanf((const char *)payload, "%d,%d,%d", &r, &g, &b);
    if (result > 0 && (r > 0 || g > 0 || b > 0))
    { 
      Serial.println("Set RGB value");
      sweep = false;
      ledOn = true;
      red = map(r, 0, 255, 0, MAX_LED);
      green = map(g, 0, 255, 0, MAX_LED);
      blue = map(b, 0, 255, 0, MAX_LED);
      setLed(red, green, blue);
    }
    else
    {
      sweep = true;
    }
  }
}
