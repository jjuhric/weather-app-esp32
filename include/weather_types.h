#pragma once

#include <Arduino.h>

struct WeatherData {
    String city = "Loading...";
    String description = "WAITING...";
    String iconCode = "";
    float temperatureF = 0.0f;
    float feelsLikeF = 0.0f;
    int humidity = 0;
    float windSpeed = 0.0f;
};
