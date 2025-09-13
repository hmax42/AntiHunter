#pragma once
#include "scanner.h"
#include "network.h"
#include "main.h"

#ifndef COUNTRY
#define COUNTRY "NO"
#endif
#ifndef BUZZER_PIN
#define BUZZER_PIN 3    // BUZZER +         (GPIO3)
#define BUZZER_PIN 26 //m5atom
#define BUZZER_PIN 8 //seeed
#endif
#ifndef BUZZER_IS_PASSIVE
#define BUZZER_IS_PASSIVE 1
#endif
#ifndef MESH_RX_PIN
#define MESH_RX_PIN 4   // MESH PIN 20      (GPIO4)
#endif
#ifndef MESH_TX_PIN
#define MESH_TX_PIN 5    // MESH PIN 19     (GPIO5)
#endif
#ifndef VIBRATION_PIN
#define VIBRATION_PIN 1  // SW-420   (GPIO1)
#endif

#define TEMP_SENSOR_PIN 6  // DS18B20       (GPIO6)

// SD Card (SPI)
#define SD_CS_PIN   15    // CS on D1        (GPIO2)
#define SD_CLK_PIN  23    // CLK (SCK)       (GPIO7)
#define SD_MISO_PIN 33    // MISO on D9      (GPIO8)
#define SD_MOSI_PIN 19    // MOSI on D10     (GPIO9)

// GPS (UART)
#define GPS_RX_PIN 22   // GPS RX          (GPIO 44)
#define GPS_TX_PIN -1   // GPS TX          (GPIO 43)

extern bool sdAvailable;
extern bool gpsValid;
extern float gpsLat, gpsLon;
extern String lastGPSData;
extern HardwareSerial GPS;
extern volatile bool vibrationDetected;
extern unsigned long lastVibrationTime;
extern unsigned long lastVibrationAlert;

void initializeHardware();
void initializeVibrationSensor();
void initializeSD();
void initializeGPS();
void checkAndSendVibrationAlert();
void beepOnce(uint32_t freq = 3200, uint32_t ms = 80);
void beepPattern(int count, int gap_ms);
void saveConfiguration();
String getDiagnostics();
int getBeepsPerHit();
int getGapMs();
void logToSD(const String &data);
String getGPSData();
void updateGPSLocation();
extern float ambientTemp;
extern bool tempSensorAvailable;
void updateTemperature();
void sendStartupStatus();
void sendGPSLockStatus(bool locked);
