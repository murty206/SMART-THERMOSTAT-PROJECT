#pragma once
#include <Arduino.h>

// ESP-NOW üzerinden taşınacak veri paketi (Payload)
typedef struct {
    uint8_t messageType;        // 1: Termostat kontrolü, 2: Manuel mod (röle köprü, kombi kendi panelinden)
    float currentTemperature;   // BME280'den okunan güncel sıcaklık
    float targetTemperature;    // Kullanıcının belirlediği hedef sıcaklık
    bool isRelayActive;         // Rölenin olması gereken durumu (true = ON, false = OFF)
} DataPacket;