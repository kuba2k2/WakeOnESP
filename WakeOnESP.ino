#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <FS.h>
#include <ArduinoJson.h>

WiFiManager wm;
WiFiManagerParameter username("username", "HTTP Username", NULL, 16);
WiFiManagerParameter password("password", "HTTP Password", NULL, 32);
WiFiManagerParameter mqtt_broker("mqtt_broker", "MQTT Broker Address", NULL, 48);
WiFiManagerParameter mqtt_username("mqtt_username", "MQTT Username", NULL, 16);
WiFiManagerParameter mqtt_password("mqtt_password", "MQTT Password", NULL, 32);
WiFiManagerParameter mqtt_ip("mqtt_ip", "MQTT VPN IP", NULL, 15);
WiFiManagerParameter mqtt_secret("mqtt_secret", "MQTT Encryption Password", NULL, 16);

char config_file[] = "/config.json";

void readConfig()
{
    Serial.print("Reading config... ");
    if (SPIFFS.exists(config_file)) {
        File file = SPIFFS.open(config_file, "r");

        if (file) {
            size_t size = file.size();
            std::unique_ptr<char[]> buf(new char[size]);
            file.readBytes(buf.get(), size);
            Serial.println(buf.get());

            DynamicJsonDocument json(1024);
            DeserializationError error = deserializeJson(json, buf.get());
            if (error) {
                Serial.println("invalid JSON.");
                return;
            }

            Serial.println("JSON valid.");
            const char *tmp;

            tmp = json["username"];
            username.setValue(tmp, strlen(tmp));
            tmp = json["password"];
            password.setValue(tmp, strlen(tmp));
            tmp = json["mqtt_broker"];
            mqtt_broker.setValue(tmp, strlen(tmp));
            tmp = json["mqtt_username"];
            mqtt_username.setValue(tmp, strlen(tmp));
            tmp = json["mqtt_password"];
            mqtt_password.setValue(tmp, strlen(tmp));
            tmp = json["mqtt_ip"];
            mqtt_ip.setValue(tmp, strlen(tmp));
            tmp = json["mqtt_secret"];
            mqtt_secret.setValue(tmp, strlen(tmp));
        }

        file.close();
    }
    else {
        Serial.println("not found.");
    }
}

void saveConfig()
{
    Serial.println("Updating config:");

    DynamicJsonDocument json(1024);
    json["username"] = username.getValue();
    json["password"] = password.getValue();
    json["mqtt_broker"] = mqtt_broker.getValue();
    json["mqtt_username"] = mqtt_username.getValue();
    json["mqtt_password"] = mqtt_password.getValue();
    json["mqtt_ip"] = mqtt_ip.getValue();
    json["mqtt_secret"] = mqtt_secret.getValue();

    File file = SPIFFS.open(config_file, "w");
    if (!file) {
        Serial.println("Failed.");
        return;
    }

    serializeJsonPretty(json, Serial);
    serializeJson(json, file);
    Serial.println();
    file.close();
}

class AuthHandler : public RequestHandler {
    public:
        AuthHandler() {}

        bool handle(ESP8266WebServer& server, HTTPMethod requestMethod, const String& requestUri) override {
            if (!server.authenticate(username.getValue(), password.getValue())) {
                server.requestAuthentication();
                return true;
            }

            RequestHandler<WiFiServer>* handler;
            for (handler = next(); handler; handler = handler->next()) {
                if (handler->canHandle(requestMethod, requestUri))
                    return handler->handle(server, requestMethod, requestUri);
            }
            return false;
        }

        bool canHandle(HTTPMethod method, const String& uri) override {
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
    wm.addParameter(&mqtt_username);
    wm.addParameter(&mqtt_password);
    wm.addParameter(&mqtt_ip);
    wm.addParameter(&mqtt_secret);
    wm.setSaveParamsCallback(saveConfig);

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

    wm.setWebServerCallback(setupAuth);
    wm.setParamsPage(true);
    wm.setCustomHeadElement("<script>fetch('/mqttinfo').then((r) => r.text()).then((t) => document.getElementsByClassName('wrap')[0].innerHTML += t);</script>");
    wm.startWebPortal();

    wm.server->on("/hello", []() {
        wm.server->send(200, "text/plain", "Hello world");
    });

    wm.server->on("/mqttinfo", []() {
        wm.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
        wm.server->send(200, "text/html", "<div class='msg ");
        if (strlen(mqtt_broker.getValue()) == 0) {
            wm.server->sendContent("P'>MQTT Disabled</div>");
            return;
        }

        bool mqttConnected = true;
        if (mqttConnected) {
            wm.server->sendContent("S'><strong>MQTT Connected: </strong>");
        }
        else {
            wm.server->sendContent("D'><strong>MQTT Disconnected: </strong>");
        }

        wm.server->sendContent(mqtt_broker.getValue());
        wm.server->sendContent("<br/><em><small>Node IP: ");
        wm.server->sendContent(mqtt_ip.getValue());
        wm.server->sendContent("</small></em></div>");

        wm.server->sendContent("");
    });
}

void loop()
{
    wm.process();
}
