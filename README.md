# About
I wanted a tempreature sensor for some rooms in my house and I don't want any cloud based systems.

This takes an ESP32 and a BME680, connects it to wifi then hosts on prometheus exporter on /metrics to export the data.

# building
Update wifi SSID and password in sdkconfig to your own.
You will need to build and flash with the ESP IDF framework.
