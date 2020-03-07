# esp8266_dht22_mqtt

This project uses an ESP-01 to periodically read the current temperature and humidity from a DHT22 sensor and publish the result on two separate MQTT topics.

To simplify code management, the ESP8266 is subscribing to a Software Update command topic. The message payload should contain the server, port and path of the new firmware file.  
