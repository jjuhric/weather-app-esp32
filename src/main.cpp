#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include "secrets.h"
#include "weather_service.h"
#include "weather_types.h"

TFT_eSPI tft = TFT_eSPI();

WeatherData weatherData;
unsigned long lastFetchMs = 0;
constexpr unsigned long kRefreshIntervalMs = 15UL * 60UL * 1000UL;

void setup() {
    Serial.begin(115200);
    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);

    displayStatusMessage("Connecting to WiFi...", TFT_YELLOW);
    if (connectToWiFi(WIFI_SSID, WIFI_PASSWORD)) {
        displayStatusMessage("Connected!", TFT_GREEN);
        delay(1000);
        String statusMessage;
        if (fetchWeatherData(weatherData, statusMessage)) {
            renderWeatherUI(weatherData);
        } else if (!statusMessage.isEmpty()) {
            displayStatusMessage(statusMessage, TFT_RED);
        }
    } else {
        displayStatusMessage("WiFi Connect Failed", TFT_RED);
    }

    lastFetchMs = millis();
}

void loop() {
    if (millis() - lastFetchMs >= kRefreshIntervalMs) {
        if (WiFi.status() == WL_CONNECTED) {
            String statusMessage;
            if (fetchWeatherData(weatherData, statusMessage)) {
                renderWeatherUI(weatherData);
            } else if (!statusMessage.isEmpty()) {
                displayStatusMessage(statusMessage, TFT_RED);
            }
        }
        lastFetchMs = millis();
    }
}
