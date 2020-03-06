#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266httpUpdate.h>
#include <PubSubClient.h>

// REQUIRES the following Arduino libraries:
// - DHT Sensor Library: https://github.com/adafruit/DHT-sensor-library
// - Adafruit Unified Sensor Lib: https://github.com/adafruit/Adafruit_Sensor

#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>

/************************* MQTT Messages *********************************/

// subscribed MQTT topics:
// dht22/cmd/<client_id>
// dht22/sw/<client_id>

// public MQTT topics:
// dht22/temperature/<client_id>
// dht22/humidity/<client_id>
// debug/dht22/<client_id>

// Examples:
// SW update
// mosquitto_pub  -t 'dht22/sw/esp_1b6f29' -m 'troubadix.home 80 /webdav/esp_sw/fw_dht22_200301.bin'

// change wakeup iter to "every 1 min"
// mosquitto_pub  -t 'dht22/cmd/esp_1b6f29' -m '1'


/************************* Manual Config *********************************/

const char* code_version = "190304-1.3";

const char* ssid = "....";
const char* password = "....";

const char* mqtt_server = "mqtt.home";
int mqtt_server_port = 1883;

long interval = 1000 *60 *10; // interval at which to read sensor [in ms]

const char* purpose = "dht22"; // mqtt topic becomes dht22/temperature/mqttClientID

int cur_iter = 1;
long next = 0;
/************************* DHT22 Sensor *********************************/

#define DHTTYPE DHT22
#define DHTPIN 2

// Lib instantiate
DHT_Unified dht(DHTPIN, DHTTYPE);

/************************* Power *********************************/

int vcc;
ADC_MODE(ADC_VCC);

/************************* WiFi Access Point *********************************/

String wifimac ="";
IPAddress ipaddress;

/************************* MQTT Server *********************************/
String mqttClientId = "";

char msg_buf[120];

char topicTemp[80];
char topicHumid[80];
char topicDebug[80];
char topicInCmd[80];
char topicInSwUpdate[80];

/************************* ESP8266 WiFiClient *********************************/
WiFiClient wifiClient;

/************************* MQTT client *********************************/
PubSubClient client(wifiClient );


/************************* DHT Sensor *********************************/

float humidity, temp_f; // Values read from sensor

/************************* SW Update *************************************/
void DoSwUpdate(char* pyld) {
  char* tok;
  char s_name[40];
  int s_port = 0;
  char s_path[80];
  tok = strtok(pyld, " ");

  if (tok != NULL) {
    snprintf(s_name, sizeof(s_name), "%s", tok);
    tok = strtok(NULL, " ");
    if (tok != NULL) {
      s_port = atoi(tok);      
      tok = strtok(NULL, " ");
      if (tok != NULL) {
        snprintf(s_path, sizeof(s_path), "%s", tok);
      }
    }
  }
  Serial.print("Params: host= ");
  Serial.print(s_name);
  Serial.print(" port= ");
  Serial.print(s_port);
  Serial.print(" path= ");
  Serial.println(s_path);

  client.publish(topicDebug, ("SW Update: host= " + String (s_name) + " port= "+ String (s_port) + " path= " + String (s_path)).c_str() );

  t_httpUpdate_return ret = ESPhttpUpdate.update(s_name, s_port, s_path);
  switch(ret) {
    case HTTP_UPDATE_FAILED:
        client.publish(topicDebug, "SW Update: Update failed" );
        Serial.println("[update] Update failed.");
        break;
    case HTTP_UPDATE_NO_UPDATES:
        Serial.println("[update] Update no Update.");
        client.publish(topicDebug, "SW Update: Update no Updates." );
        break;
    case HTTP_UPDATE_OK:
        client.publish(topicDebug, "SW Update: Update ok." );
        Serial.println("[update] Update ok."); // may not be called since we reboot the ESP
        break;
   }
}

/*************MQTT Callback ******************************/

void MqttCallback(char* topic, byte* payload, unsigned int length) {

  char* p_copy = new char[length+1];
  char* p_topic = new char[strlen(topic)+1];

  memcpy(p_copy, payload, length);
  memcpy(p_topic, topic, strlen(topic));
  p_copy[length] = '\0';
  p_topic[strlen(topic)] = '\0';

  Serial.println();
  Serial.print("Mqtt Message received for topic");
  Serial.println(p_topic);

  snprintf(msg_buf, sizeof(msg_buf), "New Msg topic= %s, Pyld= %s", p_topic, p_copy );
  client.publish(topicDebug, msg_buf);

  if (strncmp(p_topic, topicInSwUpdate, strlen(topicInSwUpdate)) == 0) {
    //char* pyld = (char*)payload;
    if (length == 0) {
      snprintf(msg_buf, sizeof(msg_buf), "%s: SW Update received with no payload. Ignore.", p_topic );
      client.publish(topicDebug, msg_buf);
      Serial.println(msg_buf);
      return;
    }
    snprintf(msg_buf, sizeof(msg_buf), "%s: Start SW Update", p_topic );
    client.publish(topicDebug, msg_buf);
    Serial.println(msg_buf);

    DoSwUpdate(p_copy);
    return;    
  }

  if (strncmp(p_topic, topicInCmd, strlen(topicInCmd)) == 0) {
    //char* pyld = (char*)payload;
    if (length == 0) {
      snprintf(msg_buf, sizeof(msg_buf), "%s: Command received with no value. Ignore.", p_topic );
      client.publish(topicDebug, msg_buf);
      Serial.println(msg_buf);
      return;
    }
    int new_val = atoi(p_copy);
    interval = 1000 *60 *new_val;

    snprintf(msg_buf, sizeof(msg_buf), "%s: Command received val= %d new interval= %.3f s", p_topic, new_val, interval/1000.0 );
    client.publish(topicDebug, msg_buf);
    Serial.println(msg_buf);

    return;    
  }
  
  snprintf(msg_buf, sizeof(msg_buf), "%s: Not handled pyld: ", p_topic, p_copy );
  client.publish(topicDebug, msg_buf);
  Serial.println(msg_buf);
}

long startMillis;

String getLowerMacAddress() {
  byte mac[6];
  WiFi.macAddress(mac);
  char  buff[10];
  String cMac = "";
  for (int i = 3; i < 6; ++i) {
    snprintf(buff, sizeof(buff), "%02X", mac[i]);
    cMac += buff;
    }
  cMac.toLowerCase();
  return cMac;
}

String getMacAddress() {
  byte mac[6];
  WiFi.macAddress(mac);
  char  buff[10];
  String cMac = "";
  for (int i = 0; i < 6; ++i) {
    snprintf(buff, sizeof(buff), "%02X", mac[i]);
    cMac += buff;
    }
  cMac.toLowerCase();
  return cMac;
}

String ipAddressToString(IPAddress address) {
 return String(address[0]) + "." + 
        String(address[1]) + "." + 
        String(address[2]) + "." + 
        String(address[3]);
}

/******* Utility function to connect or re-connect to MQTT-Server ********************/
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection to ");
    Serial.print(mqtt_server);
    Serial.print(" ");

    // Attempt to connect
    //if (client.connect(mqtt_server, mqtt_user, mqtt_password)) {
    //if (client.connect(mqtt_server)) {
    if (client.connect(mqttClientId.c_str())) {

      vcc = ESP.getVcc();//readvdd33();

      Serial.println("connected");
      //" ver= (" + version +
      snprintf(msg_buf, sizeof(msg_buf), "Client %s (%s)  with IP Address= %s %s VCC= %d mV", mqttClientId.c_str(), getMacAddress().c_str(), ipAddressToString(WiFi.localIP()).c_str(), code_version, vcc );
      client.publish(topicDebug, msg_buf);

      snprintf(msg_buf, sizeof(msg_buf), "Subscribing to %s and %s", topicInSwUpdate, topicInCmd);
      client.publish(topicDebug, msg_buf);

      client.subscribe(topicInSwUpdate);
      client.subscribe(topicInCmd);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");

      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

/************* Functionname says it all! ******************************/
void setup(void) {
  startMillis = millis();
  Serial.begin(115200);

  Serial.print("Chip-ID =");
  Serial.print ( ESP.getChipId() );

  WiFi.mode(WIFI_STA);

  // Connect to WiFi network
  WiFi.begin(ssid, password);
  Serial.print("\n\r \n\rConnecting to ");
  Serial.println(ssid);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  wifimac = getLowerMacAddress();
  mqttClientId = "esp_" +wifimac; 
  ipaddress = WiFi.localIP();
  Serial.println (" mqttClientId= " + mqttClientId);

  // Create String for MQTT Topics
  snprintf(topicInSwUpdate, sizeof(topicInSwUpdate),  "dht22/sw/%s", mqttClientId.c_str());
  snprintf(topicInCmd, sizeof(topicInCmd),  "dht22/cmd/%s", mqttClientId.c_str());
  
  snprintf(topicTemp, sizeof(topicTemp),  "%s/temperature/%s", purpose, mqttClientId.c_str());
  snprintf(topicHumid, sizeof(topicHumid),  "%s/humidity/%s", purpose, mqttClientId.c_str());
  snprintf(topicDebug, sizeof(topicDebug),  "debug/%s/%s", purpose, mqttClientId.c_str());

  startMillis = millis();
  dht.begin();

  Serial.println("\n\rESP8266 &amp;amp; DHT22 based temperature and humidity sensor working!");
  Serial.print("\n\rIP address: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqtt_server, mqtt_server_port);
  client.setCallback(MqttCallback);

  if (!client.connected()) { // Connect to mqtt broker
    reconnect();
  }

  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  Serial.println(F("------------------------------------"));
  Serial.println(F("Temperature Sensor"));
  Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
  Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
  Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
  Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("°C"));
  Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("°C"));
  Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("°C"));

  Serial.println(F("------------------------------------"));
  snprintf(msg_buf, sizeof(msg_buf),"Temp: %s v= %d id= %d max= %f min= %f res= %f", sensor.name, sensor.version, sensor.sensor_id, sensor.max_value, sensor.min_value,  sensor.resolution);
  client.publish(topicDebug, msg_buf);

  // Print humidity sensor details.
  dht.humidity().getSensor(&sensor);
  Serial.println(F("Humidity Sensor"));
  Serial.print  (F("Sensor Type: ")); Serial.println(sensor.name);
  Serial.print  (F("Driver Ver:  ")); Serial.println(sensor.version);
  Serial.print  (F("Unique ID:   ")); Serial.println(sensor.sensor_id);
  Serial.print  (F("Max Value:   ")); Serial.print(sensor.max_value); Serial.println(F("%"));
  Serial.print  (F("Min Value:   ")); Serial.print(sensor.min_value); Serial.println(F("%"));
  Serial.print  (F("Resolution:  ")); Serial.print(sensor.resolution); Serial.println(F("%"));
  Serial.println(F("------------------------------------"));
  snprintf(msg_buf, sizeof(msg_buf),"Humi: %s v= %d id= %d max= %f min= %f res= %f", sensor.name, sensor.version, sensor.sensor_id, sensor.max_value, sensor.min_value,  sensor.resolution);
  client.publish(topicDebug, msg_buf);
}


/************* Functionname says it all! ******************************/
void loop(void) {
  //char buff[20];
  long now = millis();

  sensors_event_t event;

  if (!client.connected()) { // Connect to mqtt broker
    reconnect();
  }
  client.loop();

  if (next > now) {
    Serial.print (".");
    delay (1000);
    return;
  }
  Serial.println ("");

  vcc = ESP.getVcc();  //readvdd33();

  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  dht.humidity().getSensor(&sensor);

  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) {
    Serial.println(F("Error reading temperature!"));
    snprintf(msg_buf, sizeof(msg_buf), "Error reading temperature");
    client.publish(topicDebug, msg_buf);
  }
  else {
    Serial.print(F("Temperature: "));
    Serial.print(event.temperature);
    Serial.println(F("°C"));
    snprintf(msg_buf, sizeof(msg_buf), "%f %d mV", event.temperature, vcc);
    client.publish(topicTemp, msg_buf);

  }
  // Get humidity event and print its value.
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) {
    Serial.println(F("Error reading humidity!"));
    snprintf(msg_buf, sizeof(msg_buf), "Error reading humidity");
    client.publish(topicDebug, msg_buf);
  }
  else {
    Serial.print(F("Humidity: "));
    Serial.print(event.relative_humidity);
    Serial.println(F("%"));
    snprintf(msg_buf, sizeof(msg_buf), "%f %d mV ", event.relative_humidity, vcc);
    client.publish(topicHumid, msg_buf);

  }

  // Now we can publish stuff!
//  int iter = (now - startMillis) / interval;
//  long next = (iter + 1) * interval;
 next = (cur_iter) * interval + startMillis;
  cur_iter++;

  if (cur_iter == 10) {
  snprintf(msg_buf, sizeof(msg_buf), "Iter= %d Resetting  ", cur_iter);
  client.publish(topicDebug, msg_buf);
    startMillis = next;
    cur_iter = 1;
  }

//  snprintf(buff, sizeof(buff), "%l", (next-now));
  snprintf(msg_buf, sizeof(msg_buf), "Iter= %d Dur= %.3f s Sleeping now for %.3f s ", cur_iter, (now-startMillis)/1000.0, (next - now)/ 1000.0);
  client.publish(topicDebug, msg_buf);
  //message = String(iter) + " Dur= " + String(now-startMillis) + " Sleeping now for " + String((next - now)/ 1000.0) + " s";
  //client.publish(topicDebug, message.c_str());
  Serial.print (cur_iter);
  Serial.print(" Sleeping for ");
  Serial.print((next -now));
  Serial.println(" ms");

  if ((next-now) > 0)
    delay (1000);
//    delay(next - now);
  else
    delay (2000);
}
