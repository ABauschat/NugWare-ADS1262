#include "LoadCellDiagnostic.h"
#include <Arduino.h>

LoadCellDiagnostic::LoadCellDiagnostic(const ADS1262::PinConfig& adcConfig)
    : adc(adcConfig) {
}

void LoadCellDiagnostic::runFullDiagnostic() {
    Serial.println("\n=== LOADCELL DIAGNOSTIC START ===\n");
    
    // Test 1: Pin configuration
    Serial.println("[TEST 1] Pin Configuration:");
    Serial.printf("  DIN (MOSI):  GPIO %d\n", 4);
    Serial.printf("  DOUT (MISO): GPIO %d\n", 5);
    Serial.printf("  DRDY:        GPIO %d\n", 6);
    Serial.printf("  SCLK:        GPIO %d\n", 7);
    Serial.printf("  CS:          GPIO %d\n", 8);
    Serial.printf("  START:       GPIO %d\n", 9);
    
    // Test 2: Pin states before initialization
    Serial.println("\n[TEST 2] Pin States (before init):");
    Serial.printf("  DRDY (GPIO 6):  %d (should be HIGH)\n", digitalRead(6));
    Serial.printf("  DOUT (GPIO 5):  %d (should be HIGH)\n", digitalRead(5));
    Serial.printf("  CS (GPIO 8):    %d (should be HIGH)\n", digitalRead(8));
    Serial.printf("  START (GPIO 9): %d (should be LOW)\n", digitalRead(9));
    
    // Test 3: Initialize ADS1262
    Serial.println("\n[TEST 3] Initializing ADS1262...");
    if (!adc.begin()) {
        Serial.println("  ERROR: ADS1262 initialization failed!");
        return;
    }
    Serial.println("  SUCCESS: ADS1262 initialized");
    
    // Test 4: Pin states after initialization
    Serial.println("\n[TEST 4] Pin States (after init):");
    Serial.printf("  DRDY (GPIO 6):  %d\n", digitalRead(6));
    Serial.printf("  DOUT (GPIO 5):  %d\n", digitalRead(5));
    Serial.printf("  CS (GPIO 8):    %d\n", digitalRead(8));
    Serial.printf("  START (GPIO 9): %d (should be HIGH)\n", digitalRead(9));
    
    // Test 5: Read raw data 10 times
    Serial.println("\n[TEST 5] Reading raw ADC data (10 samples):");
    for (int i = 0; i < 10; i++) {
        int32_t raw = adc.readData();
        uint8_t bytes[6];
        adc.getLastBytes(bytes);
        Serial.printf("  Sample %d: 0x%08lX (bytes: %02X %02X %02X %02X %02X %02X)\n",
                      i, (unsigned long)raw, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5]);
        delay(100);
    }
    
    Serial.println("\n=== DIAGNOSTIC COMPLETE ===\n");
}

