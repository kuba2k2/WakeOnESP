#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <FS.h>
#include <ArduinoJson.h>

WiFiManager wm;
WiFiManagerParameter username("username", "HTTP Username", NULL, 16);
WiFiManagerParameter password("password", "HTTP Password", NULL, 32);

char config_file[] = "/config.json";

void printConfig()
{
    Serial.print("HTTP username = ");
    Serial.println(username.getValue());
    Serial.print("HTTP password = ");
    Serial.println(password.getValue());
}

void readConfig()
{
    Serial.print("Reading config... ");
    if (SPIFFS.exists(config_file)) {
        File file = SPIFFS.open(config_file, "r");

        if (file) {
            size_t size = file.size();
            std::unique_ptr<char[]> buf(new char[size]);
            file.readBytes(buf.get(), size);
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

            printConfig();
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
    wm.startWebPortal();

    wm.server->on("/hello", []() {
        wm.server->send(200, "text/plain", "Hello world");
    });
}

void loop()
{
    wm.process();
}
