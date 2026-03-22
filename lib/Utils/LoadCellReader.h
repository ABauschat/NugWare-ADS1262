#ifndef LOADCELL_READER_H
#define LOADCELL_READER_H

#include "Utils/ADS1262.h"
#include <Preferences.h>

class LoadCellReader {
public:
    // Read-speed presets – controls ADC data rate, IIR smoothing and display interval.
    // Default on every boot is MEDIUM.
    enum ReadSpeed {
        SLOW,    //   5 SPS  – calibration / static loads.  α=0.10, display 1000 ms
        MEDIUM,  // 100 SPS  – general use default.          α=0.25, display  200 ms
        FAST     // 2400 SPS – tensile / peak capture.       α=0.30, display   50 ms
    };

    LoadCellReader(const ADS1262::PinConfig& adcConfig);

    // Initialization
    bool begin();

    // Calibration
    void calibrateZero();
    void calibrateSpan(float knownWeight);

    // Reading
    float readWeight();
    int32_t readRawValue();
    int32_t getLastRawValue() const { return lastRawValue; }

    // Debug
    void getLastSampleBytes(uint8_t out[6]) const {
        #ifndef USE_PROTOCENTRAL_ADS1262
        adc.getLastBytes(out);
        #endif
    }

    // Configuration
    void setCalibrationValues(int32_t zeroOffset, float scaleFactor);
    void getCalibrationValues(int32_t& zeroOffset, float& scaleFactor) const;

    // NVS persistence
    bool saveCalibration();
    bool loadCalibration();

    // Read speed
    void      setReadSpeed(ReadSpeed speed);
    ReadSpeed getReadSpeed() const { return currentSpeed; }
    uint32_t  getUpdateIntervalMs() const;

    // Peak-hold tracking
    // peakRaw is updated on every raw ADC sample (before IIR) so filtering
    // cannot suppress the true maximum.  Call resetPeak() to start a new test.
    float getPeakWeight() const;
    void  resetPeak();

    // Access to underlying ADC for diagnostics
    ADS1262& getADC() { return adc; }

private:
    ADS1262 adc;
    int32_t zeroOffset;
    float scaleFactor;
    int32_t lastRawValue;

    // Averaging for stable readings
    static const uint8_t AVERAGE_SAMPLES = 10;
    int32_t readAveragedRawValue();

    // Read speed
    ReadSpeed currentSpeed;

    // Filtering: IIR low-pass + glitch guard
    int32_t filteredValue;                    // IIR low-pass state
    float   filterAlpha;                      // IIR smoothing factor (set by setReadSpeed)
    static const int32_t GLITCH_THRESHOLD;    // max allowed jump per sample (~200g in counts)

    // Peak tracking (raw ADC counts, updated before IIR filter)
    int32_t peakRaw;

    // Helper: median of 3 values (kills single outliers)
    static int32_t median3(int32_t a, int32_t b, int32_t c);
};

#endif // LOADCELL_READER_H

