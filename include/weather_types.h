#pragma once

#include <Arduino.h>

struct WeatherData {
    String city = "Loading...";
    String description = "WAITING...";
    String iconCode = "";
    float temperatureF = 0.0f;
    float feelsLikeF = 0.0f;
    float minTempF = 0.0f;
    float maxTempF = 0.0f;
    int humidity = 0;
    float windSpeed = 0.0f;
    long long dt = 0;
    long long timezoneSeconds = 0;
};
