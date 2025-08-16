#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include "html_index.h"   // generated from html/index.html
#include "html_config.h"  // generated from html/config.html

// ================== Globals ==================
TFT_eSPI tft = TFT_eSPI();
Preferences prefs;
AsyncWebServer server(80);

String ssid, password, openai_key;
bool staConnected = false;

#define POT_PIN 36
#define SCREEN_ROTATION 3

bool debug = false;

// ================== WiFi Symbol ==================
void DrawWiFiSymbol(int32_t x, int32_t y, int32_t r, uint32_t fg_color, uint32_t bg_color)
{
    int32_t segment = r / 5;
    tft.drawSmoothArc(x, y, segment, 0, 150, 210, fg_color, bg_color);
    tft.drawSmoothArc(x, y, 3 * segment, 2 * segment, 150, 210, fg_color, bg_color);
    tft.drawSmoothArc(x, y, 5 * segment, 4 * segment, 150, 210, fg_color, bg_color);
}

// ================== Filters ==================
auto isAPClient = [](AsyncWebServerRequest *req) -> bool
{
    // AP clients use 192.168.4.x by default
    return req->client()->remoteIP().toString().startsWith("192.168.4.");
};

auto isSTAClient = [&](AsyncWebServerRequest *req) -> bool
{
    return !isAPClient(req);
};

// ================== Update TFT ==================
void updateDisplay()
{
    tft.fillScreen(TFT_BLACK);
    //tft.fillRect(0, 0, 240, 40, TFT_BLACK);
    IPAddress ip;
    uint32_t color;

    if (staConnected) {
        ip = WiFi.localIP();
        color = TFT_GREEN;
    }
    else if (WiFi.getMode() & WIFI_AP) {
        ip = WiFi.softAPIP();
        color = TFT_BLUE;
    } else {
        ip = IPAddress(0, 0, 0, 0);
        color = TFT_RED;
    }

    tft.setCursor(10, 10);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print(ip);

    DrawWiFiSymbol(165, 25, 20, color, TFT_BLACK);
}

// ================== NVS Config ==================
void saveConfig(String s, String p, String k)
{
    prefs.begin("wifi", false);
    prefs.putString("ssid", s);
    prefs.putString("pass", p);
    prefs.putString("apikey", k);
    prefs.end();
}

bool loadConfig()
{
    prefs.begin("wifi", true);
    ssid = prefs.getString("ssid", "");
    password = prefs.getString("pass", "");
    openai_key = prefs.getString("apikey", "");
    prefs.end();
    return (ssid.length() && password.length() && openai_key.length());
}

// ================== Streaming Chat Handler ==================
struct ChatStreamCtx {
    WiFiClientSecure client;
    bool done = false;
    String pending = "";     // leftover bytes that didn’t fit in last chunk
};

void handleChat(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t, size_t)
{
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, data, len) != DeserializationError::Ok) {
        request->send(400, "text/plain", "Bad JSON");
        return;
    }

    String userMsg = doc["message"] | "";
    if (!userMsg.length()) {
        request->send(400, "text/plain", "Empty message");
        return;
    }

    if (debug) {
        Serial.print("Request: ");
        Serial.println(userMsg);
    }

    // Create per-request context (concurrency-safe)
    auto ctx = std::make_shared<ChatStreamCtx>();

    // Connect and send request immediately
    ctx->client.setInsecure();
    if (!ctx->client.connect("api.openai.com", 443)) {
        request->send(502, "text/plain", "Upstream connect failed");
        return;
    }

    String safe = userMsg;
    safe.replace("\\", "\\\\");
    safe.replace("\"", "\\\"");
    String payload =
        "{\"model\":\"gpt-4o-mini\",\"stream\":true,"
        "\"messages\":[{\"role\":\"user\",\"content\":\"" + safe + "\"}]}";

    String req =
        String("POST /v1/chat/completions HTTP/1.1\r\n") +
        "Host: api.openai.com\r\n" +
        "Authorization: Bearer " + openai_key + "\r\n" +
        "Content-Type: application/json\r\n" +
        "Accept: text/event-stream\r\n" +
        "Connection: keep-alive\r\n" +
        "Content-Length: " + String(payload.length()) + "\r\n\r\n" +
        payload;

    ctx->client.print(req);

    // Skip HTTP headers before streaming
    while (ctx->client.connected()) {
        String line = ctx->client.readStringUntil('\n');
        //Serial.print("headers:");
        //Serial.println(line);
        if (line == "\r") break;
    }

    // Start chunked streaming
    AsyncWebServerResponse *response = request->beginChunkedResponse(
        "text/plain",
        [ctx](uint8_t *buffer, size_t maxLen, size_t /*index*/) -> size_t {
            if (ctx->done) return 0;

            size_t written = 0;

            // If leftover text from last time, send it first
            if (!ctx->pending.isEmpty()) {
                size_t toCopy = ctx->pending.length();
                if (toCopy > maxLen)
                    toCopy = maxLen;
                memcpy(buffer, ctx->pending.c_str(), toCopy);
                ctx->pending.remove(0, toCopy);
                return toCopy;
            }

            // Process available SSE lines
            while (ctx->client.connected() && ctx->client.available() && written < maxLen) {
                String line = ctx->client.readStringUntil('\n');
                line.trim();
                if (!line.length()) continue;                   // ignore blank keep-alives
                if (!line.startsWith("data: ")) continue;       // ignore non-data lines

                String data = line.substring(6);
                data.trim();
                if (data == "[DONE]") {
                    ctx->done = true;
                    break;
                }

                DynamicJsonDocument tok(1024);
                if (deserializeJson(tok, data) == DeserializationError::Ok) {
                    const char *delta = tok["choices"][0]["delta"]["content"];
                    if (delta && *delta) {
                        if (delta && *delta) {
                            const size_t deltaLen = strlen(delta);
                            size_t spaceLeft = maxLen - written;

                            if (deltaLen <= spaceLeft) {
                                memcpy(buffer + written, delta, deltaLen);
                                written += deltaLen;
                            } else {
                                // Fill this chunk and keep the remainder for next call
                                memcpy(buffer + written, delta, spaceLeft);
                                ctx->pending = String(delta + spaceLeft);
                                written += spaceLeft;
                                break; // this chunk is full
                            }
                        }
                    }
                }
            }

            // End if no more data and connection closed
            if (!ctx->client.connected() && ctx->pending.isEmpty()) {
                ctx->done = true;
            }

            if (written == 0 && !ctx->done) {  // handle case where stream is lagging behind but there is still more to come.
                buffer[0] = ' ';
                written = 1;
            }

            if (debug) {
                Serial.print("(Done, Written, maxLen, pending) = (");
                Serial.print(ctx->done);                Serial.print(", ");
                Serial.print(written);                  Serial.print(", ");
                Serial.print(maxLen);                   Serial.print(", ");
                Serial.print(ctx->pending.length());    Serial.println(")");
            }
            return written;
        });

    request->send(response);
}

// ================== Setup ==================
void setup()
{
    Serial.begin(115200);

    // TFT init
    tft.init();
    tft.setRotation(SCREEN_ROTATION);
    tft.fillScreen(TFT_BLACK);
    tft.setTextSize(2);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("ESP32-Config");    // AP always available

    // Try STA if we have creds
    if (loadConfig()) {
        WiFi.begin(ssid.c_str(), password.c_str());
        unsigned long startAttempt = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000)
            delay(500);
        staConnected = WiFi.status() == WL_CONNECTED;
    } else {
        staConnected = false;
    } 

    updateDisplay();

    // Routes
    // STA clients get the chat UI at "/"
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", index_html); 
    }).setFilter(isSTAClient);

    // AP clients get redirected to /config at "/"
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->redirect("/config"); 
    }).setFilter(isAPClient);

    // Config page (AP only)
    server.on("/config", HTTP_GET, [](AsyncWebServerRequest *req) {
        req->send(200, "text/html", config_html); 
    }).setFilter(isAPClient);

    // Config POST (AP only)
    server.on("/config", HTTP_POST, [](AsyncWebServerRequest *req) {
        if (req->hasParam("ssid", true) &&
            req->hasParam("pass", true) &&
            req->hasParam("apikey", true)) {

                String s = req->getParam("ssid", true)->value();
                String p = req->getParam("pass", true)->value();
                String k = req->getParam("apikey", true)->value();

            if (s.length() && p.length() && k.length()) {
                saveConfig(s, p, k);
                req->send(200, "text/plain", "Saved. Rebooting...");
                delay(1000);
                ESP.restart();
                return;
            }
        }
        req->send(400, "text/plain", "Missing parameters"); 
    }).setFilter(isAPClient);

    // Chat API (STA clients only)
    server.on("/api/chat", HTTP_POST, [](AsyncWebServerRequest *req){}, nullptr, handleChat).setFilter(isSTAClient);

    server.begin();
}

// ================== Loop ==================
void loop()
{
    static unsigned long last = 0;
    unsigned long currentT = millis();

    if (currentT - last > 500) {
        // Track STA connection status & refresh TFT
        bool now = (WiFi.status() == WL_CONNECTED);
        if (now != staConnected) {
            staConnected = now;
        }

        updateDisplay();
        int potValue = analogRead(POT_PIN);
        float voltage = potValue * (3.3 / 4095);

        tft.setCursor(10, 40);
        tft.print("Pot Value: ");
        tft.print(potValue);

        tft.setCursor(10, 70);
        tft.print("Voltage: ");
        tft.print(voltage, 2);
        tft.print("V");

        last = millis();
    }
}
