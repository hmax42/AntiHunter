#pragma once
#include "scanner.h"
#include "network.h"
#include "main.h"

#ifndef COUNTRY
#define COUNTRY "NO"
#endif
#ifndef MESH_RX_PIN
#define MESH_RX_PIN 4   // MESH PIN 20      (GPIO 04)
#endif
#ifndef MESH_TX_PIN
#define MESH_TX_PIN 5    // MESH PIN 19     (GPIO 05)
#endif
#ifndef VIBRATION_PIN
#define VIBRATION_PIN 1  // SW-420          (GPIO 01)
#endif

// SD Card (SPI)
#define SD_CS_PIN   15    // CS on D1        (GPIO2)
#define SD_CLK_PIN  23    // CLK (SCK)       (GPIO7)
#define SD_MISO_PIN 33    // MISO on D9      (GPIO8)
#define SD_MOSI_PIN 19    // MOSI on D10     (GPIO9)

// GPS (UART)
#define GPS_RX_PIN 22   // GPS RX          (GPIO 44)
#define GPS_TX_PIN -1   // GPS TX          (GPIO 43)

// RTC (I2C)
#define RTC_SDA_PIN 6    // RTC SDA on       (GPIO 05)
#define RTC_SCL_PIN 3    // RTC SCL on       (GPIO 02)

// RTC Status
extern bool rtcAvailable;
extern bool rtcSynced;
extern time_t lastRTCSync;
extern String rtcTimeString;

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
void saveConfiguration();
String getDiagnostics();
void logToSD(const String &data);
String getGPSData();
void updateGPSLocation();
void sendStartupStatus();
void sendGPSLockStatus(bool locked);

// RTC Functions
void initializeRTC();
void syncRTCFromGPS();
void updateRTCTime();
String getRTCTimeString();
String getFormattedTimestamp();
time_t getRTCEpoch();
bool setRTCTime(int year, int month, int day, int hour, int minute, int second);
