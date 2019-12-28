#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

#define PIN_LED_RED   16 // D0
#define PIN_LED_GREEN  0 // D3
#define PIN_LED_BLUE   2 // D4
#define PIN_RCWL_0516 14 // D5
#define PIN_LDR       A0

ESP8266WebServer server(80);
Adafruit_BME280 bme;

bool lastMovement = false;
long lastMovementTime = 0;
int lastLightValue = 0;
long lastLightReadTime = 0;
float lastTemperature = 0.0;
float lastHumidity = 0.0;
float lastPressure = 0.0;

bool ledOn = false;

void setupIO() {
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_GREEN, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_RCWL_0516, INPUT);
  pinMode(PIN_LDR, INPUT);

  Serial.begin(115200);

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

void setupWifi(void) {
  WiFiManager wifiManager;
  wifiManager.autoConnect();
  Serial.print("Connected to ");
  Serial.println(WiFi.SSID());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void readBme()
{
  bme.takeForcedMeasurement();
  lastTemperature = bme.readTemperature();
  lastHumidity = bme.readHumidity();
  lastPressure = bme.readPressure() / 100.0F;
}

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

  setLed(map(lastTemperature, 0, 35, 0, 1023),
         map(lastPressure, 1000, 1050, 0, 1023),
         map(lastHumidity, 0, 100, 0, 1023));  
    
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

int mapColor(String value)
{
  int numValue = value.toInt();
  if (numValue < 0) numValue = 0;
  if (numValue > 255) numValue = 255;
  return map(numValue, 0, 255, 0, 1023);
}

void httpHandleLed() {

  if(server.args() > 0) {
    
    String red = server.arg("r");
    String green = server.arg("g");
    String blue = server.arg("b");

    setLed(mapColor(red), mapColor(green), mapColor(blue));
  }
  server.send(200, "text/plain", "ok");
}

void setupHttpServer(void) {
  server.on("/", httpHandleRoot);  
  server.on("/temperature", httpHandleTemperature);  
  server.on("/humidity", httpHandleHumidity);
  server.on("/pressure", httpHandlePressure);
  server.on("/led", httpHandleLed);
  server.begin();
  Serial.println("HTTP server started");
}

void setup() {
  setupIO();
  setupWifi();
  setupHttpServer();
  setLed(0, 0, 0);
}

void loop() {
  server.handleClient();
  checkMovement();
  if (millis() - lastLightReadTime > 1000)
  {
    lastLightValue = analogRead(PIN_LDR);
    lastLightReadTime = millis();
  }
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

void checkMovement()
{
  bool movement = digitalRead(PIN_RCWL_0516);
  if (movement != lastMovement)
  {
    Serial.print(millis());
    if (movement)
      Serial.println(" Movement detected");
    else
      Serial.println(" No more movement");

    lastMovement = movement;
    if (movement)
    {
      ledOn = true;
      setLed(200, 100, 150);
    }
    lastMovementTime = millis();
  }
  
  if (!movement && millis() - lastMovementTime > 10000)
  {
    ledOn = false;
    setLed(0, 0, 0);
  }
}
