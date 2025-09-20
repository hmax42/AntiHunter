#include "hardware.h"
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <RTClib.h>
#include "esp_wifi.h"

extern Preferences prefs;
extern ScanMode currentScanMode;
extern std::vector<uint8_t> CHANNELS;

// GPS
TinyGPSPlus gps;
HardwareSerial GPS(2);
bool sdAvailable = false;
String lastGPSData = "No GPS data";
float gpsLat = 0.0, gpsLon = 0.0;
bool gpsValid = false;

// RTC
RTC_DS3231 rtc;
bool rtcAvailable = false;
bool rtcSynced = false;
time_t lastRTCSync = 0;
String rtcTimeString = "RTC not initialized";

// Viration Sensor
volatile bool vibrationDetected = false;
unsigned long lastVibrationTime = 0;
unsigned long lastVibrationAlert = 0;
const unsigned long VIBRATION_ALERT_INTERVAL = 5000; 

// Diagnostics
extern volatile bool scanning;
extern volatile int totalHits;
extern volatile uint32_t framesSeen;
extern volatile uint32_t bleFramesSeen;
extern volatile bool trackerMode;
extern std::set<String> uniqueMacs;
extern uint32_t lastScanSecs;
extern bool lastScanForever;
extern String macFmt6(const uint8_t *m);
extern size_t getTargetCount();
extern void getTrackerStatus(uint8_t mac[6], int8_t &rssi, uint32_t &lastSeen, uint32_t &packets);

void initializeHardware()
{
    Serial.println("Loading preferences...");
    prefs.begin("ouispy", false);

    String nodeId = prefs.getString("nodeId", "");
    if (nodeId.length() == 0)
    {
        uint64_t chipid = ESP.getEfuseMac();
        nodeId = "NODE_" + String((uint32_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
        prefs.putString("nodeId", nodeId);
    }
    setNodeId(nodeId);
    Serial.println("[NODE_ID] " + nodeId);
    Serial.printf("Hardware initialized: nodeID=%s\n", nodeId);
}

void saveConfiguration()
{
  // TODO save wifi channels and other granular stuff
}

String getDiagnostics() {
    static unsigned long lastDiagTime = 0;
    static unsigned long lastSDTime = 0;
    static String cachedDiag = "";
    static String cachedSDInfo = "";
    
    if (millis() - lastDiagTime < 5000 && cachedDiag.length() > 0) {
        return cachedDiag;
    }
    lastDiagTime = millis();
    
    String s;
    String modeStr = (currentScanMode == SCAN_WIFI) ? "WiFi" : 
                     (currentScanMode == SCAN_BLE) ? "BLE" : "WiFi+BLE";

    uint32_t uptime_total_seconds = millis() / 1000;
    uint32_t uptime_hours = uptime_total_seconds / 3600;
    uint32_t uptime_minutes = (uptime_total_seconds % 3600) / 60;
    uint32_t uptime_seconds = uptime_total_seconds % 60;

    char uptimeBuffer[10];
    snprintf(uptimeBuffer, sizeof(uptimeBuffer), "%02lu:%02lu:%02lu", uptime_hours, uptime_minutes, uptime_seconds);
    s += "Up:" + String(uptimeBuffer) + "\n";

    s += "Scan Mode: " + modeStr + "\n";
    s += String("Scanning: ") + (scanning ? "yes" : "no") + "\n";
    s += "WiFi Frames seen: " + String((unsigned)framesSeen) + "\n";
    s += "BLE Frames seen: " + String((unsigned)bleFramesSeen) + "\n";
    s += "Total hits: " + String(totalHits) + "\n";
    s += "Current channel: " + String(WiFi.channel()) + "\n";
    s += "AP IP: " + WiFi.softAPIP().toString() + "\n";
    s += "Unique devices: " + String((int)uniqueMacs.size()) + "\n";
    s += "Targets: " + String(getTargetCount()) + "\n";
    s += "Mesh Node ID: " + getNodeId() + "\n";
    s += "Vibration sensor: " + String(lastVibrationTime > 0 ? "Active" : "Standby") + "\n";
    if (lastVibrationTime > 0) {
        unsigned long vibrationTime = lastVibrationTime;
        unsigned long seconds = vibrationTime / 1000;
        unsigned long minutes = seconds / 60;
        unsigned long hours = minutes / 60;
        
        seconds = seconds % 60;
        minutes = minutes % 60;
        hours = hours % 24;
        
        char timeStr[12];
        snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", hours, minutes, seconds);
        
        unsigned long agoSeconds = (millis() - lastVibrationTime) / 1000;
        
        s += "Last Movement: " + String(timeStr) + " (" + String(agoSeconds) + "s ago)\n";
    }

    s += "SD Card: " + String(sdAvailable ? "Available" : "Not available") + "\n";
    if (sdAvailable) {
        if (millis() - lastSDTime > 30000 || cachedSDInfo.length() == 0) {
            lastSDTime = millis();
            cachedSDInfo = "";
            
            uint64_t cardSize = SD.cardSize() / (1024 * 1024);
            uint64_t totalBytes = SD.totalBytes();
            uint64_t usedBytes = SD.usedBytes();
            uint64_t freeBytes = totalBytes - usedBytes;

            uint8_t cardType = SD.cardType();
            String cardTypeStr = (cardType == CARD_MMC) ? "MMC" :
                                (cardType == CARD_SD) ? "SDSC" :
                                (cardType == CARD_SDHC) ? "SDHC" : "UNKNOWN";
            cachedSDInfo += "SD Card Type: " + cardTypeStr + "\n";
            cachedSDInfo += "SD Free Space: " + String(freeBytes / (1024 * 1024)) + "MB\n";
        }
        s += cachedSDInfo;
    }

    s += "GPS: ";
    if (gpsValid) {
        s += "Locked\n";
    } else {
        s += "Waiting for data\n";
    }
    s += "RTC: ";
    if (rtcAvailable) {
        s += rtcSynced ? "Synced" : "Not synced";
        s += " Time: " + getRTCTimeString() + "\n";
        if (lastRTCSync > 0) {
            s += "Last sync: " + String((millis() - lastRTCSync) / 1000) + "s ago\n";
        }
    } else {
        s += "Not available\n";
    }

    if (trackerMode) {
        uint8_t trackerMac[6];
        int8_t trackerRssi;
        uint32_t trackerLastSeen, trackerPackets;
        getTrackerStatus(trackerMac, trackerRssi, trackerLastSeen, trackerPackets);

        s += "Tracker: target=" + macFmt6(trackerMac) + " lastRSSI=" + String((int)trackerRssi) + "dBm";
        s += "  lastSeen(ms ago)=" + String((unsigned)(millis() - trackerLastSeen));
        s += " pkts=" + String((unsigned)trackerPackets) + "\n";
    }

    s += "Last scan secs: " + String((unsigned)lastScanSecs) + (lastScanForever ? " (forever)" : "") + "\n";

    float temp_c = temperatureRead();
    float temp_f = (temp_c * 9.0 / 5.0) + 32.0;
    s += "ESP32 Temp: " + String(temp_c, 1) + "°C / " + String(temp_f, 1) + "°F\n";
    
    s += "WiFi Channels: ";
    for (auto c : CHANNELS) {
        s += String((int)c) + " ";
    }
    s += "\n";

    cachedDiag = s;
    return s;
}

void initializeSD()
{
    Serial.println("Initializing SD card...");
    Serial.printf("[SD] Pins SCK=%d MISO=%d MOSI=%d CS=%d\n", SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);

    SPI.end();
    SPI.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN);
    delay(100);

    const uint32_t tryFreqs[] = {1000000, 4000000, 8000000, 10000000};
    for (uint32_t f : tryFreqs)
    {
        Serial.printf("[SD] Trying frequency: %lu Hz\n", f);
        if (SD.begin(SD_CS_PIN, SPI, f))
        {
            Serial.println("SD card initialized successfully");
            sdAvailable = true;

            uint8_t cardType = SD.cardType();
            Serial.print("SD Card Type: ");
            if (cardType == CARD_MMC)
            {
                Serial.println("MMC");
            }
            else if (cardType == CARD_SD)
            {
                Serial.println("SDSC");
            }
            else if (cardType == CARD_SDHC)
            {
                Serial.println("SDHC");
            }
            else
            {
                Serial.println("UNKNOWN");
            }

            uint64_t cardSize = SD.cardSize() / (1024 * 1024);
            Serial.printf("SD Card Size: %lluMB\n", cardSize);
            return;
        }
        delay(100);
    }
    Serial.println("SD card initialization failed");
}

void initializeGPS() {
    Serial.println("Initializing GPS…");

    // Grow buffer and start UART
    GPS.setRxBufferSize(2048);
    GPS.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

    // Give it a moment to start spitting characters
    delay(120);
    unsigned long start = millis();
    bool sawSentence = false;
    while (millis() - start < 2000) {
        if (GPS.available()) {
            char c = GPS.read();
            if (gps.encode(c)) {
                sawSentence = true;
                break;
            }
        }
    }

    if (sawSentence) {
        Serial.println("[GPS] GPS module responding (NMEA detected)");
    } else {
        Serial.println("[GPS] No NMEA data – check wiring or allow cold-start time");
        Serial.println("[GPS] First fix can take 5–15 minutes outdoors");
    }

    // Send startup GPS status to server
    sendStartupStatus();

    Serial.printf("[GPS] UART on RX:%d TX:%d\n", GPS_RX_PIN, GPS_TX_PIN);
}

void sendStartupStatus() {
    float temp_c = temperatureRead();
    float temp_f = (temp_c * 9.0 / 5.0) + 32.0;

    String startupMsg = getNodeId() + ": STARTUP: System initialized";
    startupMsg += " GPS:";
    startupMsg += (gpsValid ? "LOCKED " : "SEARCHING ");
    startupMsg += "TEMP: " + String(temp_c, 1) + "°C / " + String(temp_f, 1) + "°F\n";
    startupMsg += " SD:";
    startupMsg += (sdAvailable ? "OK" : "FAIL");
    startupMsg += " Status:ONLINE";
    
    Serial.printf("[STARTUP] %s\n", startupMsg.c_str());
    
    if (Serial1.availableForWrite() >= startupMsg.length()) {
        Serial1.println(startupMsg);
        Serial1.flush();
    }
    
    logToSD(startupMsg);
}

void sendGPSLockStatus(bool locked) {
    String gpsMsg = getNodeId() + ": GPS: ";
    gpsMsg += (locked ? "LOCKED" : "LOST");
    if (locked) {
        gpsMsg += " Location:" + String(gpsLat, 6) + "," + String(gpsLon, 6);
        gpsMsg += " Satellites:" + String(gps.satellites.value());
        gpsMsg += " HDOP:" + String(gps.hdop.hdop(), 2);
    }
    
    Serial.printf("[GPS] %s\n", gpsMsg.c_str());
    
    if (Serial1.availableForWrite() >= gpsMsg.length()) {
        Serial1.println(gpsMsg);
        Serial1.flush();
    }
    
    logToSD("GPS Status: " + gpsMsg);
}

void updateGPSLocation() {
    static unsigned long lastDataTime = 0;
    static bool wasLocked = false;

    while (GPS.available() > 0) {
        char c = GPS.read();
        if (gps.encode(c)) {
            lastDataTime = millis();

            bool nowLocked = gps.location.isValid();
            
            if (nowLocked) {
                gpsLat = gps.location.lat();
                gpsLon = gps.location.lng();
                gpsValid = true;
                lastGPSData = "Lat: " + String(gpsLat, 6)
                            + ", Lon: " + String(gpsLon, 6)
                            + " (" + String((millis() - lastDataTime) / 1000) 
                            + "s ago)";
                
                if (!wasLocked && nowLocked) {
                    sendGPSLockStatus(true);
                }
            } else {
                gpsValid = false;
                lastGPSData = "No valid GPS fix (" 
                            + String((millis() - lastDataTime) / 1000)
                            + "s ago)";
                
                if (wasLocked && !nowLocked) {
                    sendGPSLockStatus(false);
                }
            }
            
            wasLocked = nowLocked;
        }
    }

    if (lastDataTime > 0 && millis() - lastDataTime > 30000) {
        if (gpsValid) {
            gpsValid = false;
            sendGPSLockStatus(false);
        }
        lastGPSData = "No data for " 
                    + String((millis() - lastDataTime) / 1000)
                    + "s";
    }
}


void logToSD(const String &data) {
    if (!sdAvailable) return;
    
    static uint32_t totalWrites = 0;
    static File logFile;
    
    if (!SD.exists("/")) {
        SD.mkdir("/");
    }

    if (!logFile || totalWrites % 50 == 0) {
        if (logFile) {
            logFile.close();
        }
        logFile = SD.open("/antihunter.log", FILE_APPEND);
        if (!logFile) {
            logFile = SD.open("/antihunter.log", FILE_WRITE);
            if (!logFile) {
                Serial.println("[SD] Failed to open log file");
                return;
            }
        }
    }
    
    // Use RTC time if available, otherwise fall back to millis
    String timestamp = getFormattedTimestamp();
    
    logFile.printf("[%s] %s\n", timestamp.c_str(), data.c_str());
    
    // Batch flush every 10 writes 
    if (++totalWrites % 10 == 0) {
        logFile.flush();
    }
    
    static unsigned long lastSizeCheck = 0;
    if (millis() - lastSizeCheck > 10000) {
        File checkFile = SD.open("/antihunter.log", FILE_READ);
        if (checkFile) {
            Serial.printf("[SD] Log file size: %lu bytes\n", checkFile.size());
            checkFile.close();
        }
        lastSizeCheck = millis();
    }
}
void logVibrationEvent(int sensorValue) {
    String event = String(sensorValue ? "Motion" : "Impact") + " detected";
    if (gpsValid) {
        event += " @" + String(gpsLat, 4) + "," + String(gpsLon, 4);
    }
    logToSD(event);
    Serial.printf("[MOTION] %s\n", event.c_str());
}

String getGPSData()
{
    return lastGPSData;
}

// Vibration Sensor
void IRAM_ATTR vibrationISR() {
    vibrationDetected = true;
    lastVibrationTime = millis();
}

void initializeVibrationSensor() {
    pinMode(VIBRATION_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(VIBRATION_PIN), vibrationISR, RISING);
    Serial.println("[VIBRATION] Sensor initialized on GPIO1");
}

void checkAndSendVibrationAlert() {
    if (vibrationDetected) {
        vibrationDetected = false;
        
        // Only send alert if enough time has passed since last alert
        if (millis() - lastVibrationAlert > VIBRATION_ALERT_INTERVAL) {
            lastVibrationAlert = millis();
            
            // Format timestamp as HH:MM:SS
            unsigned long currentTime = lastVibrationTime;
            unsigned long seconds = currentTime / 1000;
            unsigned long minutes = seconds / 60;
            unsigned long hours = minutes / 60;
            
            seconds = seconds % 60;
            minutes = minutes % 60;
            hours = hours % 24;
            
            char timeStr[12];
            snprintf(timeStr, sizeof(timeStr), "%02lu:%02lu:%02lu", hours, minutes, seconds);
            int sensorValue = digitalRead(VIBRATION_PIN);
            
            String vibrationMsg = getNodeId() + ": VIBRATION: Movement detected at " + String(timeStr);
            
            // Add GPS if we have it
            if (gpsValid) {
                vibrationMsg += " GPS:" + String(gpsLat, 6) + "," + String(gpsLon, 6);
            }
            
            Serial.printf("[VIBRATION] Sending mesh alert: %s\n", vibrationMsg.c_str());
            
            if (Serial1.availableForWrite() >= vibrationMsg.length()) {
                Serial1.println(vibrationMsg);
                Serial1.flush();
            }
            
            logVibrationEvent(sensorValue);
            
        } else {
            Serial.printf("[VIBRATION] Alert rate limited - %lums since last alert\n", millis() - lastVibrationAlert);
        }
    }
}

// RTC functions
void initializeRTC() {
    Serial.println("Initializing RTC...");
    Serial.printf("[RTC] Using SDA:%d SCL:%d\n", RTC_SDA_PIN, RTC_SCL_PIN);

    // Initialize I2C with correct pins and speed once
    Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN, 100000); // Start at 100kHz
    delay(100);
    
    // Scan for I2C devices
    Serial.println("[RTC] Scanning I2C bus...");
    byte error, address;
    int nDevices = 0;
    
    for(address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        
        if (error == 0) {
            Serial.printf("[RTC] I2C device found at address 0x%02X\n", address);
            nDevices++;
        }
    }
    
    if (nDevices == 0) {
        Serial.println("[RTC] No I2C devices found!");
        Serial.println("[RTC] Check wiring: SDA->GPIO3, SCL->GPIO5, VCC->3.3V, GND->GND");
        rtcAvailable = false;
        return;
    }
    
    // Now try to initialize the RTC
    if (!rtc.begin()) {
        Serial.println("[RTC] DS3231 not found at 0x68!");
        rtcAvailable = false;
        return;
    }
    
    rtcAvailable = true;
    Serial.println("[RTC] DS3231 initialized successfully!");
    delay(100); // Give it a moment

    
    // Check if RTC lost power
    if (rtc.lostPower()) {
        Serial.println("[RTC] RTC lost power, setting to compile time...");
        rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        rtcSynced = false;
    } else {
        DateTime now = rtc.now();
        rtcSynced = true;
        Serial.printf("[RTC] Current time: %04d-%02d-%02d %02d:%02d:%02d\n", 
                      now.year(), now.month(), now.day(),
                      now.hour(), now.minute(), now.second());
    }
    
    rtc.disable32K();
    Serial.printf("[RTC] Successfully initialized on SDA:%d SCL:%d\n", 
                  RTC_SDA_PIN, RTC_SCL_PIN);
}

void syncRTCFromGPS() {
    if (!rtcAvailable || !gpsValid) return;
    
    // Only sync if we have a good GPS fix with valid date/time
    if (!gps.date.isValid() || !gps.time.isValid()) return;
    
    // Sync once per hour max
    if (lastRTCSync > 0 && (millis() - lastRTCSync) < 3600000) return;
    
    int year = gps.date.year();
    int month = gps.date.month();
    int day = gps.date.day();
    int hour = gps.time.hour();
    int minute = gps.time.minute();
    int second = gps.time.second();
    
    // Validate GPS time
    if (year < 2020 || year > 2050) return;
    if (month < 1 || month > 12) return;
    if (day < 1 || day > 31) return;
    if (hour > 23 || minute > 59 || second > 59) return;
    
    DateTime gpsTime(year, month, day, hour, minute, second);
    DateTime rtcTime = rtc.now();
    
    // Only sync if difference is more than 2 seconds
    int timeDiff = abs((int)(gpsTime.unixtime() - rtcTime.unixtime()));
    if (timeDiff > 2) {
        rtc.adjust(gpsTime);
        rtcSynced = true;
        lastRTCSync = millis();
        
        Serial.printf("[RTC] Synced from GPS: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                      year, month, day, hour, minute, second);
        
        // Log the sync event
        String syncMsg = "RTC synced from GPS: " + String(year) + "-" + 
                        String(month) + "-" + String(day) + " " +
                        String(hour) + ":" + String(minute) + ":" + String(second);
        logToSD(syncMsg);
        
        // Send sync status over mesh
        if (Serial1.availableForWrite() >= 100) {
            String meshMsg = getNodeId() + ": RTC_SYNC: " + syncMsg;
            Serial1.println(meshMsg);
        }
    }
}

void updateRTCTime() {
    if (!rtcAvailable) {
        rtcTimeString = "RTC not available";
        return;
    }
    
    DateTime now = rtc.now();
    
    char buffer[30];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    
    rtcTimeString = String(buffer);
    
    // Auto-sync from GPS if available
    if (gpsValid && !rtcSynced) {
        syncRTCFromGPS();
    }
    
    // Periodic re-sync (every hour if GPS available)
    if (gpsValid && rtcSynced && (millis() - lastRTCSync) > 3600000) {
        syncRTCFromGPS();
    }
}

String getRTCTimeString() {
    updateRTCTime();
    return rtcTimeString;
}

String getFormattedTimestamp() {
    if (!rtcAvailable) {
        // Fallback to millis-based timestamp
        uint32_t ts = millis();
        uint8_t hours = (ts / 3600000) % 24;
        uint8_t mins = (ts / 60000) % 60;
        uint8_t secs = (ts / 1000) % 60;
        
        char buffer[12];
        snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, mins, secs);
        return String(buffer);
    }
    
    DateTime now = rtc.now();
    char buffer[30];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             now.year(), now.month(), now.day(),
             now.hour(), now.minute(), now.second());
    
    return String(buffer);
}

time_t getRTCEpoch() {
    if (!rtcAvailable) return 0;
    
    DateTime now = rtc.now();
    return now.unixtime();
}

bool setRTCTime(int year, int month, int day, int hour, int minute, int second) {
    if (!rtcAvailable) return false;
    
    DateTime newTime(year, month, day, hour, minute, second);
    rtc.adjust(newTime);
    rtcSynced = true;
    
    Serial.printf("[RTC] Manually set to: %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, minute, second);
    
    return true;
}
