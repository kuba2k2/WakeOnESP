#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <FS.h>
#include <ArduinoJson.h>
#include <mqttif.h>
#include <PCF8574.h>
#include "mqtt.h"

WiFiManager wm;
WiFiManagerParameter username("username", "HTTP Username", NULL, 16);
WiFiManagerParameter password("password", "HTTP Password", NULL, 32);
WiFiManagerParameter mqtt_broker("mqtt_broker", "MQTT Broker Address", NULL, 48);
WiFiManagerParameter mqtt_port("mqtt_port", "MQTT Broker Port", NULL, 5);
WiFiManagerParameter mqtt_username("mqtt_username", "MQTT Username", NULL, 16);
WiFiManagerParameter mqtt_password("mqtt_password", "MQTT Password", NULL, 32);
WiFiManagerParameter mqtt_ip("mqtt_ip", "MQTT VPN IP", NULL, 15);
WiFiManagerParameter mqtt_secret("mqtt_secret", "MQTT Encryption Password", NULL, 16);

IPAddress mqtt_ipaddr(0, 0, 0, 0);

PCF8574 pcf(0x20);

char config_file[] = "/config.json";

#define PIN_POWER   3
#define PIN_RESET   2
#define PIN_LED     4

uint32_t disconnectTimer = 0;
uint32_t restartTimer = 0;
uint16_t connectionAttempts = 0;

static void mqttOnConnected(uint32_t *args)
{
    Serial.println("MQTT Connected");
    mqtt_if_subscribe(mqtt_if);
    disconnectTimer = 1;
}

static void mqttOnDisconnected(uint32_t *args)
{
    Serial.println("MQTT Disconnected");
    disconnectTimer = millis();
    connectionAttempts = 0;
    mqtt_if_unsubscribe(mqtt_if);
}

void mqttInit()
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

    String client_name = String(WIFI_getChipId(), HEX);
    client_name.toUpperCase();
    client_name = "ESP_" + client_name;

    volatile uint32_t addr;
    unsigned char *addrBytes = (unsigned char *)&addr;
    sscanf(mqtt_ip.getValue(), "%hhu.%hhu.%hhu.%hhu", addrBytes + 0, addrBytes + 1, addrBytes + 2, addrBytes + 3);
    mqtt_ipaddr = IPAddress(addr);


    system_os_task(mqtt_if_Task, 1, mqtt_if_procTaskQueue, 2);

    MQTT_InitConnection(&mqttClient, (uint8_t *)broker, port, 0);
    MQTT_InitClient(&mqttClient, (uint8_t*)client_name.c_str(), (uint8_t *)username, (uint8_t *)password, 120, 1);

    MQTT_OnConnected(&mqttClient, mqttOnConnected);
    MQTT_OnDisconnected(&mqttClient, mqttOnDisconnected);
    MQTT_OnData(&mqttClient, mqttOnData);

    mqtt_if = mqtt_if_add(&mqttClient, "mqttip");
    mqtt_if_set_ipaddr(mqtt_if, mqtt_ipaddr);
    mqtt_if_set_netmask(mqtt_if, IPAddress(255, 255, 255, 0));
    mqtt_if_set_gw(mqtt_if, IPAddress(0, 0, 0, 0));
    mqtt_if_set_up(mqtt_if);

    mqtt_if_add_reading_topic(mqtt_if, mqtt_ipaddr);
    mqtt_if_add_reading_topic(mqtt_if, IPAddress(255,255,255,255));

    mqtt_if_set_password(mqtt_if, secret);

    mqttConnect();
    // reconnect later when this fails
    disconnectTimer = millis();
}

bool mqttConnected()
{
    return mqttClient.connState == MQTT_DATA;
}

void mqttConnect()
{
    if (mqttConnected())
        return;
    MQTT_Connect(&mqttClient);
}

void mqttDisconnect()
{
    MQTT_Disconnect(&mqttClient);
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
    restartTimer = millis();
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

void wakeRedirect()
{
    wm.server->sendHeader(F("Location"), F("/wake"), true);
    wm.server->sendHeader(F("Cache-Control"), F("no-cache, no-store, must-revalidate"));
    wm.server->send(301, F("text/plain"), F(""));
}

void setup()
{
    pcf.begin(2, 0);
    pcf.write(0, LOW);

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
    wm.setCustomHeadElement(R"(<script>if(location.pathname[1]){window.onload=()=>{var x=document.getElementsByClassName('wrap')[0];x.innerHTML="<form action='/'><button style='width:25%;margin:0 0 20px 0;'>Back</button></form>"+x.innerHTML;};}fetch('/mqttinfo').then((r)=>r.text()).then((t)=>document.getElementsByClassName('msg')[0].outerHTML+=t);</script>)");
    wm.startWebPortal();

    wm.server->on("/hello", []() {
        wm.server->send(200, F("text/plain"), F("Hello world"));
    });

    if (mqttEnabled())
    {
        mqttInit();
    }

    pcf.write(0, HIGH);

    wm.server->on("/mqttinfo", []() {
        wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
        wm.server->send(200, F("text/html"), F("<div class='msg "));
        if (!mqttEnabled())
        {
            wm.server->sendContent(F("P'>MQTT Disabled</div>"));
            return;
        }

        if (mqttConnected())
        {
            wm.server->sendContent(F("S'><strong>MQTT Connected: </strong>"));
        }
        else
        {
            wm.server->sendContent(F("D'><strong>MQTT Disconnected: </strong>"));
        }

        char port[6];

        wm.server->sendContent((char *)mqttClient.host);
        if (!mqttConnected()) {
            wm.server->sendContent(F("<br>Connection State: "));
            tConnState state = mqttClient.connState;
            String stateStr;
            if (state == DNS_RESOLVE)
                stateStr = F("Resolving DNS");
            else if (state == TCP_DISCONNECTED)
                stateStr = F("TCP Disconnected");
            else if (state == TCP_CLIENT_DISCONNECTED)
                stateStr = F("TCP Client Disconnected");
            else if (state == TCP_CONNECTING)
                stateStr = F("TCP Connecting");
            else if (state == TCP_CONNECTED)
                stateStr = F("TCP Connected");
            else if (state == TCP_CONNECTING_ERROR)
                stateStr = F("TCP Connection Error");
            else if (state == MQTT_DELETED)
                stateStr = F("MQTT Deleted");
            else
                stateStr = F("Unknown");
            wm.server->sendContent(stateStr);
            sprintf(port, "%u", connectionAttempts);
            wm.server->sendContent(F("<br>Connection Attempts: "));
            wm.server->sendContent(port);
        }
        sprintf(port, "%u", mqttClient.port);
        wm.server->sendContent(F("<br><em><small>Broker Address: "));
        wm.server->sendContent(IPAddress(mqttClient.ip).toString());
        wm.server->sendContent(":");
        wm.server->sendContent(port);
        wm.server->sendContent(F("<br>Node IP: "));
        wm.server->sendContent(IPAddress(mqtt_if->ipaddr).toString());
        wm.server->sendContent(F("<br>Node Name: "));
        wm.server->sendContent(mqttClient.connect_info.client_id);
        wm.server->sendContent(F("</small></em></div>"));

        wm.server->sendContent("");
    });

    wm.server->on("/wake", []() {
        wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
        wm.server->send(200, F("text/html"), F(""));

        String page;
        page += FPSTR(HTTP_HEAD_START);
        page.replace(FPSTR(T_v), "Computers");
        page += FPSTR(HTTP_SCRIPT);
        page += FPSTR(HTTP_STYLE);
        page += FPSTR(HTTP_HEAD_END);
        wm.server->sendContent(page);

        bool isOn = pcf.read(PIN_LED);

        wm.server->sendContent(F("<div class='msg "));
        wm.server->sendContent(isOn ? "S" : "D");
        wm.server->sendContent(F("'><strong>PC 1</strong><br><em><small>Powered "));
        wm.server->sendContent(isOn ? "On" : "Off");
        wm.server->sendContent(F("</small></em><br><br><form action='/wake-"));
        wm.server->sendContent(isOn ? "off" : "on");
        wm.server->sendContent(F("'><button style='background:#"));
        wm.server->sendContent(isOn ? "dc3630" : "5cb85c");
        wm.server->sendContent(F("'>Power "));
        wm.server->sendContent(isOn ? "Off" : "On");
        wm.server->sendContent(F("</button></form><br>"));
        if (isOn) {
            wm.server->sendContent(F("<form action='/wake-suspend'><button style='background:#1fa3ec;'>Suspend</button></form><br>"));
        }
        wm.server->sendContent(F("<form action='/wake-reset'><button style='background:#777;'>Reset</button></form></div>"));

        wm.server->sendContent_P(HTTP_END);
        wm.server->sendContent("");
    });

    wm.server->on("/wake-on", []() {
        pcf.write(PIN_POWER, LOW);
        delay(200);
        pcf.write(PIN_POWER, HIGH);
        wakeRedirect();
    });

    wm.server->on("/wake-suspend", []() {
        pcf.write(PIN_POWER, LOW);
        delay(200);
        pcf.write(PIN_POWER, HIGH);
        wakeRedirect();
    });

    wm.server->on("/wake-off", []() {
        pcf.write(PIN_POWER, LOW);
        delay(5000);
        pcf.write(PIN_POWER, HIGH);
        wakeRedirect();
    });

    wm.server->on("/wake-reset", []() {
        pcf.write(PIN_RESET, LOW);
        delay(200);
        pcf.write(PIN_RESET, HIGH);
        wakeRedirect();
    });
}

void loop()
{
    wm.process();

    // disconnectTimer == 0 before first connection
    // disconnectTimer == 1 when connected
    // disconnectTimer > 1 when disconnected + after first connection *attempt*
    if (disconnectTimer > 1 && millis() - disconnectTimer > 10000 && !mqttConnected()) {
        Serial.println("MQTT Reconnecting");
        disconnectTimer = millis();
        connectionAttempts++;
        mqttDisconnect();
        delay(1000);
        mqttConnect();
    }
    if (disconnectTimer == 1 && !mqttConnected()) {
        // apparently the OnDisconnected event does not fire sometimes (??)
        mqttOnDisconnected(0);
    }
    if (restartTimer && millis() - restartTimer > 1000) {
        restartTimer = 0;
        ESP.reset();
    }
}
