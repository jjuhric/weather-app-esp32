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
constexpr unsigned long kMessageBaseHoldMs = 3000UL;  // Base hold time
constexpr unsigned long kMessageHoldMsPerLine = 1000UL;  // Additional time per line
constexpr unsigned long kMessageFlashMs = 250UL;
unsigned long messageHoldMs = 5000UL;  // Will be calculated per message

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

// Calculate how many lines a message will take when word-wrapped (24 chars per line, 10 lines max)
int calculateMessageLineCount(const String& message) {
    if (message.isEmpty()) {
        return 0;
    }

    int currentLineLength = 0;
    int lineCount = 1;
    int i = 0;
    
    while (i < static_cast<int>(message.length())) {
        // Skip leading spaces
        while (i < static_cast<int>(message.length()) && message[i] == ' ') {
            i++;
        }
        
        if (i >= static_cast<int>(message.length())) {
            break;
        }
        
        // Find end of word
        int wordStart = i;
        while (i < static_cast<int>(message.length()) && message[i] != ' ') {
            i++;
        }
        
        int wordLength = i - wordStart;
        
        // Try to fit word on current line
        if (currentLineLength == 0) {
            // First word on line
            currentLineLength = wordLength;
        } else if (currentLineLength + 1 + wordLength <= 24) {
            // Word fits with a space
            currentLineLength += 1 + wordLength;
        } else {
            // Word doesn't fit, start new line
            lineCount++;
            currentLineLength = wordLength;
        }
    }
    
    return lineCount;
}

// Validate message can be word-wrapped within max lines (24 chars per line, 10 lines max)
// Returns true if message fits; false if it would exceed kMaxMessageLines
bool validateMessageWrapping(const String& message) {
    return calculateMessageLineCount(message) <= kMaxMessageLines;
}

void resetMessageDisplay() {
    messageDisplayStartedMs = millis();
    messagePhaseStartedMs = millis();
    messagePhase = 0;
}

// Display message with proper word wrapping (24 chars per line, 10 lines max)
void drawMessageOverlay(const String& message) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setTextDatum(TL_DATUM);

    int lineIndex = 0;
    int i = 0;
    String currentLine;
    
    while (i < static_cast<int>(message.length()) && lineIndex < kMaxMessageLines) {
        // Skip leading spaces
        while (i < static_cast<int>(message.length()) && message[i] == ' ') {
            i++;
        }
        
        if (i >= static_cast<int>(message.length())) {
            break;
        }
        
        // Find end of word
        int wordStart = i;
        while (i < static_cast<int>(message.length()) && message[i] != ' ') {
            i++;
        }
        
        String word = message.substring(wordStart, i);
        
        // Try to fit word on current line
        if (currentLine.isEmpty()) {
            currentLine = word;
        } else if (currentLine.length() + 1 + word.length() <= 24) {
            currentLine += " " + word;
        } else {
            // Draw current line and start new one
            int y = kMessageStartY + (lineIndex * kMessageLineHeight);
            if (y <= kMessageMaxY) {
                tft.drawString(currentLine, kMessageStartX, y);
            }
            lineIndex++;
            currentLine = word;
        }
    }
    
    // Draw final line if there's content
    if (!currentLine.isEmpty() && lineIndex < kMaxMessageLines) {
        int y = kMessageStartY + (lineIndex * kMessageLineHeight);
        if (y <= kMessageMaxY) {
            tft.drawString(currentLine, kMessageStartX, y);
        }
    }
}

void sendJsonResponse(WiFiClient& client, const String& statusLine, const String& body) {
    client.println(statusLine);
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();
    client.println(body);
}

bool handleHttpRequest() {
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

    int firstSpace = requestLine.indexOf(' ');
    int secondSpace = requestLine.indexOf(' ', firstSpace + 1);
    if (firstSpace < 0 || secondSpace < 0) {
        sendJsonResponse(client, "HTTP/1.1 400 Bad Request", "{\"ok\":false,\"error\":\"invalid request line\"}");
        client.stop();
        return true;
    }

    String method = requestLine.substring(0, firstSpace);
    String path = requestLine.substring(firstSpace + 1, secondSpace);

    int contentLength = 0;
    while (client.connected()) {
        String headerLine = client.readStringUntil('\n');
        if (headerLine == "\r" || headerLine == "\n" || headerLine.length() == 0) {
            break;
        }
        headerLine.trim();
        if (headerLine.startsWith("Content-Length:")) {
            contentLength = headerLine.substring(15).toInt();
        }
    }

    if (method == "GET" && path == "/health") {
        bool wifiConnected = WiFi.status() == WL_CONNECTED;
        bool internetConnected = isInternetReachable();
        String ipAddress = wifiConnected ? WiFi.localIP().toString() : String("0.0.0.0");
        int httpStatusCode = (wifiConnected && internetConnected) ? 200 : 503;

        String body =
            String("{\"ok\":") + ((wifiConnected && internetConnected) ? "true" : "false") +
            String(",\"wifiConnected\":") + (wifiConnected ? "true" : "false") +
            String(",\"internetConnected\":") + (internetConnected ? "true" : "false") +
            String(",\"deviceReachable\":true") +
            String(",\"ip\":\"") + ipAddress + String("\"") +
            String(",\"uptimeMs\":") + String(millis()) +
            String("}");

        sendJsonResponse(client, httpStatusCode == 200 ? "HTTP/1.1 200 OK" : "HTTP/1.1 503 Service Unavailable", body);
        client.stop();
        return true;
    }

    if (!(method == "POST" && path == "/message")) {
        sendJsonResponse(client, "HTTP/1.1 404 Not Found", "{\"ok\":false,\"error\":\"not found\"}");
        client.stop();
        return true;
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

    // Validate raw message length first
    if (messageValue.length() > kMaxMessageChars) {
        sendJsonResponse(client, "HTTP/1.1 413 Payload Too Large", "{\"ok\":false,\"error\":\"message exceeds max length " + String(kMaxMessageChars) + " by " + String(messageValue.length() - kMaxMessageChars) + " characters\"}");
        client.stop();
        return true;
    }
    
    // Validate message can be word-wrapped within max lines
    if (!validateMessageWrapping(messageValue)) {
        sendJsonResponse(client, "HTTP/1.1 413 Payload Too Large", "{\"ok\":false,\"error\":\"message exceeds max lines when word-wrapped\"}");
        client.stop();
        return true;
    }

    // Calculate hold time: 3 seconds base + 1 second per line
    int lineCount = calculateMessageLineCount(messageValue);
    messageHoldMs = kMessageBaseHoldMs + (lineCount * kMessageHoldMsPerLine);

    pendingMessage = messageValue;
    displayMode = DisplayModeMessage;
    messageScreenRendered = false;
    resetMessageDisplay();

    sendJsonResponse(client, "HTTP/1.1 200 OK", "{\"ok\":true}");
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
        handleHttpRequest();
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
            if (millis() - messagePhaseStartedMs >= messageHoldMs) {
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
