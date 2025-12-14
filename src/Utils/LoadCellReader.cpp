#include "Utils/LoadCellReader.h"

// Define static constants
const float LoadCellReader::FILTER_ALPHA = 0.4f;        // IIR smoothing: 0.4 = faster response (was 0.15)
const int32_t LoadCellReader::GLITCH_THRESHOLD = 200000; // ~200g in ADC counts (was 100k, increased to allow larger jumps)

LoadCellReader::LoadCellReader(const ADS1262::PinConfig& adcConfig)
    : adc(adcConfig), zeroOffset(0), scaleFactor(1.0f), lastRawValue(0), filteredValue(0) {
    // Note: Custom ADS1262 implementation
}

bool LoadCellReader::begin() {
    // Mirror debug to printf so it shows up alongside ROM / WiFi logs
    printf("[LoadCellReader] begin() called\n");

    // Initialize ADS1262
    if (!adc.begin()) {
        printf("[LoadCellReader] ADS1262 begin failed!\n");
        return false;
    }

    // Default calibration for 1kg load cell:
    // - Zero offset will be set by auto-calibration below
    // - Scale factor assumes typical 2mV/V load cell at 3.3V excitation with 8x gain
    //   Full scale = 2mV/V × 3.3V × 8 = 52.8mV differential
    //   With ratiometric measurement (Vref = 3.3V): full scale ≈ 134,000 counts for 1000g
    //   Scale factor = 1000g / 134,000 counts ≈ 0.00746 g/count
    scaleFactor = 0.00746f;  // g/count for 1kg load cell
    lastRawValue = 0;

    // Auto-calibrate zero offset (assume no load at startup)
    printf("[LoadCellReader] Auto-calibrating zero offset (assuming 0g load)...\n");
    delay(100);  // Let ADC settle

    // Read 10 samples and average for zero offset
    int32_t sum = 0;
    for (int i = 0; i < 10; i++) {
        sum += adc.readData();
        delay(50);
    }
    zeroOffset = sum / 10;

    printf("[LoadCellReader] Zero offset auto-calibrated: %ld counts\n", (long)zeroOffset);
    printf("[LoadCellReader] Scale factor: %.6f g/count (1kg load cell)\n", scaleFactor);
    printf("[LoadCellReader] begin() OK\n");
    return true;
}

void LoadCellReader::calibrateZero() {
    printf("[LoadCell] Calibrating zero offset...\n");
    printf("[LoadCell] Reading 20 samples for zero calibration...\n");

    // Read 20 samples and average them
    int32_t sum = 0;
    for (int i = 0; i < 20; i++) {
        sum += adc.readData();
        delay(50);
    }
    zeroOffset = sum / 20;

    printf("[LoadCell] Zero offset calibrated: %ld\n", (long)zeroOffset);
}

void LoadCellReader::calibrateSpan(float knownWeight) {
    printf("[LoadCell] Calibrating span (gain)...\n");
    printf("[LoadCell] IMPORTANT: Apply %.2fg to the load cell\n", knownWeight);
    printf("[LoadCell] Reading 5 samples for span calibration...\n");

    // Read 5 samples and average them
    int32_t sum = 0;
    for (int i = 0; i < 5; i++) {
        sum += adc.readData();
        delay(50);
    }
    int32_t spanValue = sum / 5;
    int32_t spanDifference = spanValue - zeroOffset;

    if (spanDifference != 0) {
        scaleFactor = knownWeight / spanDifference;
        printf("[LoadCell] Scale factor: %.6f\n", scaleFactor);
    } else {
        printf("[LoadCell] ERROR: Span difference is zero!\n");
    }
}

float LoadCellReader::readWeight() {
    int32_t rawValue = readRawValue();
    return (rawValue - zeroOffset) * scaleFactor;
}

int32_t LoadCellReader::median3(int32_t a, int32_t b, int32_t c) {
    // Branchless median of 3 values
    if ((a > b) != (a > c)) return a;
    if ((b > a) != (b > c)) return b;
    return c;
}

int32_t LoadCellReader::readRawValue() {
    int32_t rawSample = adc.readData();

    // Step 1: Glitch guard - if jump is too large, ignore and keep previous value
    int32_t delta = rawSample - filteredValue;
    if (labs(delta) > GLITCH_THRESHOLD) {
        // Spike detected, don't update filtered value
        printf("[LoadCell] Glitch detected: delta=%ld (threshold=%ld), ignoring\n",
               (long)delta, (long)GLITCH_THRESHOLD);
        lastRawValue = filteredValue;
        return lastRawValue;
    }

    // Step 2: IIR low-pass filter for smooth output
    // filteredValue = filteredValue + alpha * (rawSample - filteredValue)
    int32_t correction = (int32_t)((rawSample - filteredValue) * FILTER_ALPHA);
    filteredValue = filteredValue + correction;

    lastRawValue = filteredValue;
    return lastRawValue;
}

int32_t LoadCellReader::readAveragedRawValue() {
    int64_t sum = 0;
    for (uint8_t i = 0; i < AVERAGE_SAMPLES; i++) {
        sum += readRawValue();
        delay(50);
    }
    return sum / AVERAGE_SAMPLES;
}

void LoadCellReader::setCalibrationValues(int32_t offset, float factor) {
    zeroOffset = offset;
    scaleFactor = factor;
}

void LoadCellReader::getCalibrationValues(int32_t& offset, float& factor) const {
    offset = zeroOffset;
    factor = scaleFactor;
}
