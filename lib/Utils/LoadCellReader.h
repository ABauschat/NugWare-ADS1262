#ifndef LOADCELL_READER_H
#define LOADCELL_READER_H

#include "Utils/ADS1262.h"

class LoadCellReader {
public:
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

    // Filtering: median + IIR low-pass + glitch guard
    int32_t filteredValue;                    // IIR low-pass state
    static const float FILTER_ALPHA;          // IIR smoothing factor (0.1 = heavy smoothing)
    static const int32_t GLITCH_THRESHOLD;    // max allowed jump per sample (~50g in counts)

    // Helper: median of 3 values (kills single outliers)
    static int32_t median3(int32_t a, int32_t b, int32_t c);
};

#endif // LOADCELL_READER_H

