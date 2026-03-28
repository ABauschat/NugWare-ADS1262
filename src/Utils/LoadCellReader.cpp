#include "Utils/LoadCellReader.h"

// Define static constants
const int32_t LoadCellReader::GLITCH_THRESHOLD = 200000; // ~200g in ADC counts

LoadCellReader::LoadCellReader(const ADS1262::PinConfig& adcConfig)
    : adc(adcConfig), zeroOffset(0), scaleFactor(1.0f), lastRawValue(0),
      filteredValue(0), currentSpeed(MEDIUM), filterAlpha(0.25f), peakRaw(0) {
}

// ---------------------------------------------------------------------------
// Read Speed control
// ---------------------------------------------------------------------------

void LoadCellReader::setReadSpeed(ReadSpeed speed) {
    currentSpeed = speed;
    switch (speed) {
        case SLOW:
            // 5 SPS – calibration / static loads.
            // Heavy smoothing; useless for transient events.
            adc.setDataRate(0x01);  // MODE0 DR[3:0] = 0x01 → 5 SPS
            filterAlpha = 0.10f;
            printf("[LoadCellReader] Speed: SLOW (5 SPS, alpha=0.10, display 1000ms)\n");
            break;
        case MEDIUM:
            // 100 SPS – general purpose default.
            // Moderate smoothing; good for slowly-varying loads.
            adc.setDataRate(0x07);  // MODE0 DR[3:0] = 0x07 → 100 SPS
            filterAlpha = 0.25f;
            printf("[LoadCellReader] Speed: MEDIUM (100 SPS, alpha=0.25, display 200ms)\n");
            break;
        case FAST:
            // 2400 SPS – tensile / peak-capture mode.
            // Lower alpha means LESS smoothing so sharp peaks are NOT suppressed.
            // Peak tracking is done on the raw sample (before IIR) so the true
            // maximum is always captured regardless of display refresh rate.
            adc.setDataRate(0x0A);  // MODE0 DR[3:0] = 0x0A → 2400 SPS
            filterAlpha = 0.30f;
            printf("[LoadCellReader] Speed: FAST (2400 SPS, alpha=0.30, display 50ms)\n");
            break;
    }
}

uint32_t LoadCellReader::getUpdateIntervalMs() const {
    switch (currentSpeed) {
        case SLOW:   return 1000;
        case MEDIUM: return 200;
        case FAST:   return 50;
        default:     return 200;
    }
}

// ---------------------------------------------------------------------------
// Peak-hold tracking
// ---------------------------------------------------------------------------

float LoadCellReader::getPeakWeight() const {
    return (peakRaw - zeroOffset) * scaleFactor;
}

void LoadCellReader::resetPeak() {
    peakRaw = filteredValue;  // reset to current filtered baseline
    printf("[LoadCellReader] Peak reset (new baseline: %ld counts)\n", (long)peakRaw);
}

bool LoadCellReader::begin() {
    // Mirror debug to printf so it shows up alongside ROM / WiFi logs
    printf("[LoadCellReader] begin() called\n");

    // Initialize ADS1262
    if (!adc.begin()) {
        printf("[LoadCellReader] ADS1262 begin failed!\n");
        return false;
    }

    // Try to load saved scaleFactor from NVS flash.
    // zeroOffset is always re-measured at startup so we never boot with stale offset.
    if (loadCalibration()) {
        printf("[LoadCellReader] Saved scale factor loaded: %.6f g/count\n", scaleFactor);
    } else {
        // Default calibration for 1kg load cell:
        // Scale factor = 1000g / ~134,000 counts ≈ 0.00746 g/count
        scaleFactor = 0.00746f;
        printf("[LoadCellReader] No saved calibration – using default scale: %.6f g/count\n", scaleFactor);
    }

    lastRawValue = 0;

    // Let the ADC fully settle before we take the zero baseline.
    // More time here = more stable zero (eliminates the 0.4 g startup drift).
    printf("[LoadCellReader] Warming up ADC (500 ms)...\n");
    delay(500);

    // Prime the IIR filter with a real ADC reading so the glitch-guard does
    // not fire on the very first call to readRawValue() (which would return 0
    // and produce a spurious non-zero weight reading).
    filteredValue = adc.readData();
    lastRawValue  = filteredValue;

    // Auto-zero: read 20 samples and average them for the zero offset.
    printf("[LoadCellReader] Auto-zeroing (20 samples, assuming 0 g load)...\n");
    int64_t sum = (int64_t)filteredValue;   // include the priming read
    for (int i = 0; i < 19; i++) {
        sum += adc.readData();
        delay(50);
    }
    zeroOffset = (int32_t)(sum / 20);

    // Seed the IIR filter and peak tracker at the zero baseline.
    filteredValue = zeroOffset;
    lastRawValue  = zeroOffset;
    peakRaw       = zeroOffset;   // peak starts at 0 g relative to zero

    printf("[LoadCellReader] Zero offset: %ld counts\n", (long)zeroOffset);
    printf("[LoadCellReader] Scale factor: %.6f g/count\n", scaleFactor);

    // Apply default MEDIUM speed (sets ADC data rate + filterAlpha)
    setReadSpeed(MEDIUM);

    printf("[LoadCellReader] begin() OK\n");
    return true;
}

// ---------------------------------------------------------------------------
// NVS persistence (ESP32 Preferences)
// ---------------------------------------------------------------------------

bool LoadCellReader::saveCalibration() {
    Preferences prefs;
    if (!prefs.begin("loadcell", false)) {   // false = read-write
        printf("[LoadCellReader] ERROR: Could not open NVS namespace for writing\n");
        return false;
    }
    prefs.putLong("zeroOffset",  (long)zeroOffset);
    prefs.putFloat("scaleFactor", scaleFactor);
    prefs.end();
    printf("[LoadCellReader] Calibration saved to flash: offset=%ld, scale=%.6f\n",
           (long)zeroOffset, scaleFactor);
    return true;
}

bool LoadCellReader::loadCalibration() {
    Preferences prefs;
    if (!prefs.begin("loadcell", true)) {   // true = read-only
        return false;
    }
    bool hasData = prefs.isKey("scaleFactor");
    if (hasData) {
        scaleFactor = prefs.getFloat("scaleFactor", 0.00746f);
        // zeroOffset is intentionally NOT restored – it is always re-measured
        // at startup so the display starts at exactly 0 g.
        printf("[LoadCellReader] Loaded from flash: scale=%.6f g/count\n", scaleFactor);
    }
    prefs.end();
    return hasData;
}

void LoadCellReader::calibrateZero() {
    printf("[LoadCell] Calibrating zero offset...\n");

    // Re-seed the IIR filter from a direct ADC read before averaging.
    // If the filter is still "winding down" from a previous load (e.g. right
    // after a span calibration weight was removed), the 20-sample average
    // would include transitional high values and produce a biased zeroOffset.
    // By reading the ADC directly and setting filteredValue to that result,
    // all 20 averaging samples start from the true current baseline.
    for (int i = 0; i < 5; i++) { adc.readData(); delay(10); } // flush ADC pipeline
    int32_t seed = adc.readData();
    if (seed != 0) {   // guard against post-reset zeros
        filteredValue = seed;
        lastRawValue  = seed;
        printf("[LoadCell] Filter re-seeded at %ld counts\n", (long)seed);
    }

    printf("[LoadCell] Reading 20 filtered samples for zero calibration...\n");
    int64_t sum = 0;
    for (int i = 0; i < 20; i++) {
        sum += readRawValue();
        delay(50);
    }
    zeroOffset = (int32_t)(sum / 20);

    // Re-seed the filter and peak tracker at the new baseline.
    filteredValue = zeroOffset;
    lastRawValue  = zeroOffset;
    peakRaw       = zeroOffset;   // baseline changed → peak must reset too

    printf("[LoadCell] Zero offset calibrated: %ld\n", (long)zeroOffset);
}

void LoadCellReader::calibrateSpan(float knownWeight) {
    printf("[LoadCell] Calibrating span (gain)...\n");
    printf("[LoadCell] IMPORTANT: Apply %.2fg to the load cell\n", knownWeight);
    printf("[LoadCell] Reading 20 filtered samples for span calibration...\n");

    // Use readRawValue() (IIR-filtered path) so spanValue is on the same scale
    // as what readWeight() returns — this eliminates the raw-vs-filtered mismatch
    // that caused a small constant error (e.g. 4.98 g instead of 5.00 g).
    // Also use 20 samples (up from 5) for a more accurate span measurement.
    int64_t sum = 0;
    for (int i = 0; i < 20; i++) {
        sum += readRawValue();
        delay(50);
    }
    int32_t spanValue = (int32_t)(sum / 20);
    int32_t spanDifference = spanValue - zeroOffset;

    if (spanDifference != 0) {
        scaleFactor = knownWeight / (float)spanDifference;
        printf("[LoadCell] Span value: %ld  Zero offset: %ld  Difference: %ld\n",
               (long)spanValue, (long)zeroOffset, (long)spanDifference);
        printf("[LoadCell] Scale factor: %.6f g/count\n", scaleFactor);
    } else {
        printf("[LoadCell] ERROR: Span difference is zero – check load cell connection!\n");
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

    // Step 1: Glitch guard – reject impossible single-sample jumps (hardware noise).
    // Real tensile events rise over multiple samples and will pass this guard.
    int32_t delta = rawSample - filteredValue;
    if (labs(delta) > GLITCH_THRESHOLD) {
        printf("[LoadCell] Glitch detected: delta=%ld (threshold=%ld), ignoring\n",
               (long)delta, (long)GLITCH_THRESHOLD);
        lastRawValue = filteredValue;
        return lastRawValue;
    }

    // Step 2: Peak tracking on the RAW sample, BEFORE the IIR filter.
    // The IIR filter always lags behind and would underestimate the true maximum.
    // By tracking peakRaw here we capture the actual highest ADC count seen
    // regardless of the display refresh rate.
    if (rawSample > peakRaw) {
        peakRaw = rawSample;
    }

    // Step 3: IIR low-pass filter for smooth live display output.
    int32_t correction = (int32_t)((rawSample - filteredValue) * filterAlpha);
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
