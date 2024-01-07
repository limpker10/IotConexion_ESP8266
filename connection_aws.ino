#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "Secrets.h"
#include "DHT.h"
 
#define DHTPIN 4        // Digital pin connected to the DHT sensor
#define DHTTYPE DHT11   // DHT 11
#define LED_PIN 5
 
DHT dht(DHTPIN, DHTTYPE);
 
float h ;
float t;
unsigned long lastMillis = 0;
unsigned long previousMillis = 0;
const long interval = 5000;
 
#define AWS_IOT_PUBLISH_TOPIC   "esp8266/mosquitto"
#define AWS_IOT_SUBSCRIBE_TOPIC "mosquitto/esp8266"
 
WiFiClientSecure net;
 
BearSSL::X509List cert(cacert);
// BearSSL::X509List client_crt(client_cert);
// BearSSL::PrivateKey key(privkey);
 
PubSubClient client(net);

time_t now;
time_t nowish = 1510592825;

void NTPConnect(void)
{
  Serial.println("Setting time using SNTP");
  configTime(TIME_ZONE * 3600, 0 * 3600, "pool.ntp.org", "time.nist.gov");
  now = time(nullptr);
  while (now < nowish)
  {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("done!");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}
 
void messageReceived(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  
  String receivedValue;
  for (int i = 0; i < length; i++)
  {
    receivedValue += (char)payload[i];
    Serial.print((char)payload[i]);
  }
  Serial.println();
  // Analizar el valor recibido como JSON
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, receivedValue);
  if (error) {
    Serial.println("Failed to parse JSON!");
    return;
  }
  String valueString = doc["value"];
  
  // Interpretando el valor como un entero en el rango de 0 a 100
  int value = valueString.substring(valueString.indexOf('(') + 1, valueString.indexOf(')')).toInt();

  value = constrain(value, 0, 100); // Limitando el rango a 0-100

  // Mapeando el valor a la escala de PWM (0-255)
  int pwmValue = map(value, 0, 100, 0, 255);

  // Ajustando la intensidad del LED
  analogWrite(LED_PIN, pwmValue);

}
 
 
void connectAWS()
{
  delay(8000);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
 
  Serial.println(String("Attempting to connect to SSID: ") + String(WIFI_SSID));
 
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(1000);
  }
  
  NTPConnect();
  
  net.setTrustAnchors(&cert);
  //net.setClientRSACert(&client_crt, &key);
 
  client.setServer(MQTT_HOST, 8883);
  client.setCallback(messageReceived);
 
 
  Serial.println("Connecting to Mosquito Broker");
 
  while (!client.connect(THINGNAME))
  {
    Serial.print(".");
    delay(1000);
  }
 
  if (!client.connected()) {
    Serial.println("Mosquito Broker Timeout!");
    return;
  }
  // Subscribe to a topic
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
 
  Serial.println("Mosquito Broker Connected!");
}
 
 
void publishMessage()
{
   // Obtener la hora actual en un formato legible
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  char timeBuffer[25]; // Buffer para almacenar la hora formateada
  strftime(timeBuffer, sizeof(timeBuffer), "%Y-%m-%d %H:%M:%S", &timeinfo);

  StaticJsonDocument<200> doc;
  doc["timestamp"] = timeBuffer;
  doc["humidity"] = h;
  doc["temperature"] = t;
  doc["UnitHumidity"] = "g/m^3";
  doc["UnitTemperature"] = "°C";
  doc["Notes"] = "Envio de datos del sensor DHT11";
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer); // print to client
 
  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);
}
 
 
void setup()
{
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT); // Configurar el pin del LED como salida
  connectAWS();
  dht.begin();
  delay(1000);
}
 
 
void loop()
{
  h = dht.readHumidity();
  t = dht.readTemperature();
 
  if (isnan(h) || isnan(t) )  // Check if any reads failed and exit early (to try again).
  {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }
 
  Serial.print(F("Humidity: "));
  Serial.print(h);
  Serial.print(F("%  Temperature: "));
  Serial.print(t);
  Serial.println(F("°C "));
  delay(6000);
  
  now = time(nullptr);
  
  if (!client.connected())
  {
    connectAWS();
  }
  else
  {
    client.loop();
    if (millis() - lastMillis > 5000)
    {
      lastMillis = millis();
      publishMessage();
    }
  }
}