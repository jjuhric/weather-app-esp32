#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include "weather_types.h"

extern TFT_eSPI tft;

bool connectToWiFi(const char* ssid, const char* password, uint8_t maxAttempts = 20);
bool isInternetReachable(uint16_t timeoutMs = 1500);
bool fetchWeatherData(WeatherData& data, String& statusMessage);
void renderWeatherUI(const WeatherData& data);
void displayStatusMessage(const String& message, uint16_t color);
