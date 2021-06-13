#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

WiFiManager wm;

void setup()
{
    Serial.begin(74880);

    WiFi.mode(WIFI_STA);

    Serial.println("Connecting...");
    if (!wm.autoConnect())
    {
        Serial.println("Failed to connect.");
        while (1)
        {
            yield();
        }
    }

    Serial.print("Connected to ");
    Serial.print(WiFi.SSID());
    Serial.print(". IP address: ");
    Serial.println(WiFi.localIP());

    wm.startWebPortal();

    wm.server->on("/hello", []() {
        wm.server->send(200, "text/plain", "Hello world");
    });
}

void loop()
{
    wm.process();
}
