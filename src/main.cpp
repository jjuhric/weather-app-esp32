#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include "weather_service.h"
#include "weather_types.h"

TFT_eSPI tft = TFT_eSPI();

WeatherData weatherData;
unsigned long lastFetchMs = 0;
constexpr unsigned long kRefreshIntervalMs = 15UL * 60UL * 1000UL;
constexpr int kMaxMessageLines = 10;
constexpr int kMaxMessageChars = kMaxMessageLines * 24;
constexpr int kMessageStartX = 15;
constexpr int kMessageStartY = 15;
constexpr int kMessageLineHeight = 21;
constexpr int kMessageMaxY = 215;
constexpr unsigned long kMessageHoldMs = 5000UL;
constexpr unsigned long kMessageFlashMs = 250UL;

WiFiServer messageServer(80);
String pendingMessage;
unsigned long lastMessagePollMs = 0;
unsigned long messageDisplayStartedMs = 0;
unsigned long messagePhaseStartedMs = 0;
int messagePhase = 0;
enum DisplayMode {
    DisplayModeWeather,
    DisplayModeMessage
};
DisplayMode displayMode = DisplayModeWeather;
bool weatherRendered = false;
bool weatherDataReady = false;
bool messageScreenRendered = false;

void drawMessageToScreen(const String& message) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextDatum(TL_DATUM);

    int currentIndex = 0;
    int lineIndex = 0;
    while (currentIndex < static_cast<int>(message.length()) && lineIndex < kMaxMessageLines) {
        int nextIndex = currentIndex + 24;
        if (nextIndex > static_cast<int>(message.length())) {
            nextIndex = static_cast<int>(message.length());
        }
        String line = message.substring(currentIndex, nextIndex);
        int y = kMessageStartY + (lineIndex * kMessageLineHeight);
        if (y <= kMessageMaxY) {
            tft.drawString(line, kMessageStartX, y);
        }
        currentIndex = nextIndex;
        ++lineIndex;
    }
}

void resetMessageDisplay() {
    messageDisplayStartedMs = millis();
    messagePhaseStartedMs = millis();
    messagePhase = 0;
}

void drawMessageOverlay(const String& message) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextDatum(TL_DATUM);

    int currentIndex = 0;
    int lineIndex = 0;
    while (currentIndex < static_cast<int>(message.length()) && lineIndex < kMaxMessageLines) {
        int nextIndex = currentIndex + 24;
        if (nextIndex > static_cast<int>(message.length())) {
            nextIndex = static_cast<int>(message.length());
        }
        String line = message.substring(currentIndex, nextIndex);
        int y = kMessageStartY + (lineIndex * kMessageLineHeight);
        if (y <= kMessageMaxY) {
            tft.drawString(line, kMessageStartX, y);
        }
        currentIndex = nextIndex;
        ++lineIndex;
    }
}

bool handleMessageRequest() {
    if (WiFi.status() != WL_CONNECTED) {
        return false;
    }

    WiFiClient client = messageServer.available();
    if (!client || !client.connected()) {
        return false;
    }

    client.setTimeout(2000);

    String requestLine = client.readStringUntil('\n');
    requestLine.trim();

    if (!requestLine.startsWith("POST /message")) {
        client.println("HTTP/1.1 404 Not Found");
        client.println("Content-Type: text/plain");
        client.println("Connection: close");
        client.println();
        client.println("Not Found");
        client.stop();
        return true;
    }

    int contentLength = 0;
    while (client.connected() && client.available()) {
        String headerLine = client.readStringUntil('\n');
        if (headerLine == "\r" || headerLine == "\n" || headerLine.length() == 0) {
            break;
        }
        headerLine.trim();
        if (headerLine.startsWith("Content-Length:")) {
            contentLength = headerLine.substring(15).toInt();
        }
    }

    String body;
    if (contentLength > 0) {
        char* buffer = new char[contentLength + 1];
        size_t bytesRead = client.readBytes(buffer, contentLength);
        buffer[bytesRead] = '\0';
        body = String(buffer);
        delete[] buffer;
    }

    body.trim();

    String messageValue;
    if (body.startsWith("{")) {
        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, body);
        if (!error && doc["message"].is<String>()) {
            messageValue = doc["message"].as<String>();
        }
    } else if (body.startsWith("message=")) {
        messageValue = body.substring(8);
    } else {
        messageValue = body;
    }

    messageValue.trim();

    if (messageValue.length() > kMaxMessageChars) {
        client.println("HTTP/1.1 413 Payload Too Large");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.println();
        client.println("{\"ok\":false,\"error\":\"message exceeds max length\"}");
        client.stop();
        return true;
    }

    pendingMessage = messageValue;
    displayMode = DisplayModeMessage;
    messageScreenRendered = false;
    resetMessageDisplay();

    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println("{\"ok\":true}");
    client.stop();
    return true;
}

void setup() {
    Serial.begin(115200);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    displayStatusMessage("Connecting to WiFi...", TFT_YELLOW);
    if (connectToWiFi(WIFI_SSID, WIFI_PASSWORD)) {
        messageServer.begin();
        displayStatusMessage("Connected!", TFT_GREEN);
        delay(1000);
        String statusMessage;
        if (fetchWeatherData(weatherData, statusMessage)) {
            weatherDataReady = true;
            renderWeatherUI(weatherData);
            weatherRendered = true;
        } else if (!statusMessage.isEmpty()) {
            displayStatusMessage(statusMessage, TFT_RED);
        }
    } else {
        displayStatusMessage("WiFi Connect Failed", TFT_RED);
    }

    lastFetchMs = millis();
}

void loop() {
    if (millis() - lastMessagePollMs >= 250 && WiFi.status() == WL_CONNECTED) {
        handleMessageRequest();
        lastMessagePollMs = millis();
    }

    if (displayMode == DisplayModeMessage) {
        if (!messageScreenRendered) {
            resetMessageDisplay();
            messageScreenRendered = true;
        }

        if (messagePhase == 0) {
            if (millis() - messagePhaseStartedMs < kMessageFlashMs) {
                tft.fillScreen(TFT_RED);
            } else if (millis() - messagePhaseStartedMs < kMessageFlashMs * 2) {
                tft.fillScreen(TFT_WHITE);
            } else if (millis() - messagePhaseStartedMs < kMessageFlashMs * 3) {
                tft.fillScreen(TFT_BLUE);
            } else if (millis() - messagePhaseStartedMs < kMessageFlashMs * 4) {
                tft.fillScreen(TFT_RED);
            } else if (millis() - messagePhaseStartedMs < kMessageFlashMs * 5) {
                tft.fillScreen(TFT_WHITE);
            } else if (millis() - messagePhaseStartedMs < kMessageFlashMs * 6) {
                tft.fillScreen(TFT_BLUE);
            } else {
                messagePhase = 1;
                messagePhaseStartedMs = millis();
                drawMessageOverlay(pendingMessage);
            }
        } else if (messagePhase == 1) {
            if (millis() - messagePhaseStartedMs >= kMessageHoldMs) {
                messagePhase = 2;
                messagePhaseStartedMs = millis();
                tft.fillScreen(TFT_RED);
            }
        } else if (messagePhase == 2) {
            if (millis() - messagePhaseStartedMs < kMessageFlashMs) {
                tft.fillScreen(TFT_RED);
            } else if (millis() - messagePhaseStartedMs < kMessageFlashMs * 2) {
                tft.fillScreen(TFT_WHITE);
            } else if (millis() - messagePhaseStartedMs < kMessageFlashMs * 3) {
                tft.fillScreen(TFT_BLUE);
            } else {
                displayMode = DisplayModeWeather;
                messageScreenRendered = false;
                if (weatherDataReady) {
                    renderWeatherUI(weatherData);
                }
                return;
            }
        }

        uint16_t touchX = 0;
        uint16_t touchY = 0;
        if (tft.getTouch(&touchX, &touchY, 600)) {
            displayMode = DisplayModeWeather;
            messageScreenRendered = false;
            if (weatherDataReady) {
                renderWeatherUI(weatherData);
            }
        }
        return;
    }

    if (!weatherRendered && weatherDataReady) {
        renderWeatherUI(weatherData);
        weatherRendered = true;
    }

    if (millis() - lastFetchMs >= kRefreshIntervalMs) {
        if (WiFi.status() == WL_CONNECTED) {
            String statusMessage;
            if (fetchWeatherData(weatherData, statusMessage)) {
                weatherDataReady = true;
                renderWeatherUI(weatherData);
                weatherRendered = true;
            } else if (!statusMessage.isEmpty()) {
                displayStatusMessage(statusMessage, TFT_RED);
            }
        }
        lastFetchMs = millis();
    }
}
