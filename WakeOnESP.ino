#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <mqttif.h>

#ifndef BOOL
#define BOOL boolean
#endif

extern "C"
{
#include "mqtt/mqtt.h"
}

WiFiManager wm;
WiFiManagerParameter username("username", "HTTP Username", NULL, 16);
WiFiManagerParameter password("password", "HTTP Password", NULL, 32);
WiFiManagerParameter mqtt_broker("mqtt_broker", "MQTT Broker Address", NULL, 48);
WiFiManagerParameter mqtt_port("mqtt_port", "MQTT Broker Port", NULL, 5);
WiFiManagerParameter mqtt_username("mqtt_username", "MQTT Username", NULL, 16);
WiFiManagerParameter mqtt_password("mqtt_password", "MQTT Password", NULL, 32);
WiFiManagerParameter mqtt_ip("mqtt_ip", "MQTT VPN IP", NULL, 15);
WiFiManagerParameter mqtt_secret("mqtt_secret", "MQTT Encryption Password", NULL, 16);

struct mqtt_if_data
{
    struct netif netif;
    ip_addr_t ipaddr;
    MQTT_Client *mqttcl;
};

struct mqtt_if_data *mqtt;
IPAddress mqtt_ipaddr(0, 0, 0, 0);

char config_file[] = "/config.json";

void mqttConnect()
{
    char *broker = (char *)mqtt_broker.getValue();
    char *username = NULL;
    char *password = NULL;
    char *secret = (char *)mqtt_secret.getValue();

    if (mqtt_username.getValueLength())
    {
        username = (char *)mqtt_username.getValue();
        password = (char *)mqtt_password.getValue();
    }

    uint16_t port = 1883;
    if (mqtt_port.getValueLength())
    {
        sscanf(mqtt_port.getValue(), "%u", &port);
    }

    /* String client_name = String(WIFI_getChipId(), HEX);
    client_name.toUpperCase();
    client_name = "ESP_" + client_name; */

    volatile uint32_t addr;
    unsigned char *addrBytes = (unsigned char *)&addr;
    sscanf(mqtt_ip.getValue(), "%hhu.%hhu.%hhu.%hhu", addrBytes + 0, addrBytes + 1, addrBytes + 2, addrBytes + 3);
    mqtt_ipaddr = IPAddress(addr);

    mqtt = mqtt_if_init(broker, username, password, port, "mqttip", secret, mqtt_ipaddr, IPAddress(255, 255, 255, 0), IPAddress(0, 0, 0, 0));
}

bool mqttEnabled()
{
    return mqtt_broker.getValueLength() > 0;
}

void readConfig()
{
    Serial.print(F("Reading config... "));
    if (SPIFFS.exists(config_file))
    {
        File file = SPIFFS.open(config_file, "r");

        if (file)
        {
            size_t size = file.size();
            std::unique_ptr<char[]> buf(new char[size]);
            file.readBytes(buf.get(), size);
            Serial.println(buf.get());

            DynamicJsonDocument json(1024);
            DeserializationError error = deserializeJson(json, buf.get());
            if (error)
            {
                Serial.println(F("invalid JSON."));
                return;
            }

            Serial.println(F("JSON valid."));
            const char *tmp;

            tmp = json["username"];
            username.setValue(tmp, 16);
            tmp = json["password"];
            password.setValue(tmp, 32);
            tmp = json["mqtt_broker"];
            mqtt_broker.setValue(tmp, 48);
            tmp = json["mqtt_port"];
            mqtt_port.setValue(tmp, 5);
            tmp = json["mqtt_username"];
            mqtt_username.setValue(tmp, 16);
            tmp = json["mqtt_password"];
            mqtt_password.setValue(tmp, 32);
            tmp = json["mqtt_ip"];
            mqtt_ip.setValue(tmp, 15);
            tmp = json["mqtt_secret"];
            mqtt_secret.setValue(tmp, 16);
        }

        file.close();
    }
    else
    {
        Serial.println(F("not found."));
    }
}

void saveConfig()
{
    Serial.println(F("Updating config:"));

    DynamicJsonDocument json(1024);
    json["username"] = username.getValue();
    json["password"] = password.getValue();
    json["mqtt_broker"] = mqtt_broker.getValue();
    json["mqtt_port"] = mqtt_port.getValue();
    json["mqtt_username"] = mqtt_username.getValue();
    json["mqtt_password"] = mqtt_password.getValue();
    json["mqtt_ip"] = mqtt_ip.getValue();
    json["mqtt_secret"] = mqtt_secret.getValue();

    File file = SPIFFS.open(config_file, "w");
    if (!file)
    {
        Serial.println(F("Failed."));
        return;
    }

    serializeJsonPretty(json, Serial);
    serializeJson(json, file);
    Serial.println();
    file.close();
    ESP.restart();
}

class AuthHandler : public RequestHandler
{
public:
    AuthHandler() {}

    bool handle(ESP8266WebServer &server, HTTPMethod requestMethod, const String &requestUri) override
    {
        if (!server.authenticate(username.getValue(), password.getValue()))
        {
            server.requestAuthentication();
            return true;
        }

        RequestHandler<WiFiServer> *handler;
        for (handler = next(); handler; handler = handler->next())
        {
            if (handler->canHandle(requestMethod, requestUri))
                return handler->handle(server, requestMethod, requestUri);
        }
        return false;
    }

    bool canHandle(HTTPMethod method, const String &uri) override
    {
        return true;
    }
};

void setupAuth()
{
    wm.server->addHandler(new AuthHandler());
}

void setup()
{
    Serial.begin(74880);

    SPIFFS.begin();
    readConfig();

    WiFi.mode(WIFI_STA);
    wm.setScanDispPerc(true);

    wm.addParameter(&username);
    wm.addParameter(&password);
    wm.addParameter(&mqtt_broker);
    wm.addParameter(&mqtt_port);
    wm.addParameter(&mqtt_username);
    wm.addParameter(&mqtt_password);
    wm.addParameter(&mqtt_ip);
    wm.addParameter(&mqtt_secret);
    wm.setSaveParamsCallback(saveConfig);

    Serial.println(F("Connecting..."));
    if (!wm.autoConnect())
    {
        Serial.println(F("Failed to connect."));
        while (1)
        {
            yield();
        }
    }

    Serial.print(F("Connected to "));
    Serial.print(WiFi.SSID());
    Serial.print(F(". IP address: "));
    Serial.println(WiFi.localIP());

    wm.setWebServerCallback(setupAuth);
    wm.setParamsPage(true);
    wm.setCaptivePortalEnable(false);
    wm.setCustomHeadElement("<script>fetch('/mqttinfo').then((r) => r.text()).then((t) => document.getElementsByClassName('msg')[0].outerHTML += t);</script>");
    wm.startWebPortal();

    wm.server->on("/hello", []() {
        wm.server->send(200, F("text/plain"), F("Hello world"));
    });

    if (mqttEnabled())
    {
        mqttConnect();
    }

    wm.server->on("/mqttinfo", []() {
        wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
        wm.server->send(200, F("text/html"), F("<div class='msg "));
        if (!mqttEnabled())
        {
            wm.server->sendContent(F("P'>MQTT Disabled</div>"));
            return;
        }

        bool connected = mqtt->mqttcl->connState == MQTT_DATA;
        if (connected)
        {
            wm.server->sendContent(F("S'><strong>MQTT Connected: </strong>"));
        }
        else
        {
            wm.server->sendContent(F("D'><strong>MQTT Disconnected: </strong>"));
        }

        char port[6];
        sprintf(port, "%u", mqtt->mqttcl->port);

        wm.server->sendContent((char *)mqtt->mqttcl->host);
        wm.server->sendContent(F("<br/><em><small>Broker Address: "));
        wm.server->sendContent(IPAddress(mqtt->mqttcl->ip).toString());
        wm.server->sendContent(":");
        wm.server->sendContent(port);
        wm.server->sendContent(F("<br/>Node IP: "));
        wm.server->sendContent(IPAddress(mqtt->ipaddr).toString());
        wm.server->sendContent(F("<br/>Node Name: "));
        wm.server->sendContent(mqtt->mqttcl->connect_info.client_id);
        wm.server->sendContent(F("</small></em></div>"));

        wm.server->sendContent("");
    });
}

void loop()
{
    wm.process();
}
