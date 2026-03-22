#include "Utils/ADS1262.h"

// This file is only compiled when NOT using ProtoCentral
#ifndef USE_PROTOCENTRAL_ADS1262

#ifdef USE_PROTOCENTRAL_ADS1262
#include "ProtoCentralADS1262/proto_ads1262.h"
static ads1262 PC_ADS1262;
#endif

#ifdef USE_HX711
#include "HX711.h"
static HX711 hx711;
#endif


ADS1262::ADS1262(const PinConfig& config)
    : pins(config), spiSettings(1000000, MSBFIRST, SPI_MODE1),
      initialized(false), lastValidReading(0), consecutiveDrdyTimeouts(0),
      filterInit(false), filteredValue(0.0f) {
    for (int i = 0; i < 6; ++i) lastBytes[i] = 0;
}

bool ADS1262::begin() {
    // Use printf so messages go through the same channel as ROM / WiFi logs
    printf("[ADS1262] begin() called\n");

    // Configure pins
    printf("[ADS1262] Configuring pins: DIN=%d DOUT=%d DRDY=%d SCLK=%d CS=%d START=%d\n",
           pins.din, pins.dout, pins.dready, pins.sclk, pins.cs, pins.start);
    pinMode(pins.cs, OUTPUT);
    pinMode(pins.dready, INPUT_PULLUP);
    pinMode(pins.start, OUTPUT);
    digitalWrite(pins.cs, HIGH);
    digitalWrite(pins.start, HIGH);  // START=HIGH for continuous conversion mode (CRITICAL!)
    printf("[ADS1262] Pins configured\n");

    printf("[ADS1262] SPI: SCLK=%d, MISO(DOUT)=%d, MOSI(DIN)=%d, CS=%d, DRDY=%d, START=%d\n",
           pins.sclk, pins.dout, pins.din, pins.cs, pins.dready, pins.start);

    // Initialize SPI
    printf("[ADS1262] Calling SPI.begin()...\n");
    SPI.begin(pins.sclk, pins.dout, pins.din, pins.cs);
    printf("[ADS1262] SPI initialized\n");
    delay(10);

    // Reset (don't check ID - just assume it works)
    printf("[ADS1262] Calling reset()\n");
    reset();
    printf("[ADS1262] Reset complete\n");
    delay(200);

    // PROVEN STABLE CONFIGURATION FOR LOAD CELLS
    // NOTE: Register values verified against ADS1262 datasheet
    printf("[ADS1262] Writing configuration registers\n");

    writeReg(0x01, 0x11);  // POWER - normal operation
    printf("[ADS1262] REG 0x01 written\n");
    delay(10);

    writeReg(0x02, 0x00);  // INTERFACE - pure 3-byte data (no STATUS/CRC)
    printf("[ADS1262] REG 0x02 written\n");
    delay(10);

    writeReg(0x03, 0x0E);  // MODE0 - 5 SPS (very stable for load cells)
    printf("[ADS1262] REG 0x03 written\n");
    delay(10);

    // MODE1: 0x86 = 10000110
    // Bit 7: FILTER[1] = 1
    // Bit 6: FILTER[0] = 0  → FILTER=10 = SINC4 filter (best noise rejection)
    // Bit 5: SBADC = 0 (sensor bias ADC disabled)
    // Bit 4: SBPOL = 0
    // Bit 3: SBMAG = 0
    // Bit 2: CHOP[1] = 1
    // Bit 1: CHOP[0] = 1  → CHOP=11 = Input and IDAC rotation enabled (best offset cancellation)
    // Bit 0: DELAY[2] = 0
    writeReg(0x04, 0x86);  // MODE1 - SINC4 filter, CHOP ENABLED
    printf("[ADS1262] REG 0x04 written (SINC4 filter, CHOP ENABLED)\n");
    delay(10);

    // MODE2: GAIN[2:0] in bits 6:4. 0x30 = 011 = 8x gain (better resolution for load cell)
    writeReg(0x05, 0x30);  // MODE2 - 8x gain (bits 6:4 = 011) for load cell
    printf("[ADS1262] REG 0x05 written (8x gain)\n");
    delay(10);

    // INPMUX: 0x01 = AIN0 vs AIN1 (load cell differential signal)
    // Bits 7-4: MUXP[3:0] = 0000 = AIN0 (positive input)
    // Bits 3-0: MUXN[3:0] = 0001 = AIN1 (negative input)
    writeReg(0x06, 0x01);  // INPMUX - AIN0 vs AIN1 (load cell signal)
    printf("[ADS1262] REG 0x06 written (AIN0 vs AIN1 - load cell)\n");
    delay(10);

    // REFMUX: 0x52 = External reference on AIN2/AIN3 (excitation voltage)
    // Bits 7-5: RMUXP[2:0] = 010 = AIN2 (positive reference - excitation+)
    // Bits 4-2: RMUXN[2:0] = 010 = AIN3 (negative reference - excitation-)
    // This gives ratiometric measurement - ADC reading is independent of excitation voltage!
    writeReg(0x0F, 0x52);  // REFMUX - External reference AIN2/AIN3 (excitation voltage)
    printf("[ADS1262] REG 0x0F written (External reference AIN2/AIN3 - excitation voltage)\n");
    delay(10);

    // Start conversions
    printf("[ADS1262] Starting conversions\n");
    digitalWrite(pins.start, HIGH);
    delay(100);

    printf("[ADS1262] begin() complete - SUCCESS\n");
    initialized = true;
    consecutiveDrdyTimeouts = 0;
    return true;
}

// ---------------------------------------------------------------------------
// reconfigure() – re-write all ADC registers without re-initialising SPI.
// Call this after a lock-up to bring the ADC back to a known state.
// ---------------------------------------------------------------------------
bool ADS1262::reconfigure() {
    printf("[ADS1262] reconfigure() called – re-writing registers\n");

    // Ensure CS is deasserted and START is held HIGH before touching the bus
    digitalWrite(pins.cs, HIGH);
    digitalWrite(pins.start, HIGH);
    delay(5);

    // Hardware reset via SPI command
    SPI.beginTransaction(spiSettings);
    digitalWrite(pins.cs, LOW);
    spiTransfer(CMD_RESET);
    digitalWrite(pins.cs, HIGH);
    SPI.endTransaction();
    delay(100);  // ADS1262 needs ~50 ms after reset

    // Re-apply the same register config as begin()
    writeReg(0x01, 0x11);  // POWER - normal
    delay(5);
    writeReg(0x02, 0x00);  // INTERFACE - 3-byte data, no STATUS/CRC
    delay(5);
    writeReg(0x03, 0x07);  // MODE0 - 100 SPS default (MEDIUM); setDataRate() overrides
    delay(5);
    writeReg(0x04, 0x86);  // MODE1 - SINC4, CHOP enabled
    delay(5);
    writeReg(0x05, 0x30);  // MODE2 - 8x gain
    delay(5);
    writeReg(0x06, 0x01);  // INPMUX - AIN0 vs AIN1
    delay(5);
    writeReg(0x0F, 0x52);  // REFMUX - External ref AIN2/AIN3
    delay(5);

    // Restart conversions
    digitalWrite(pins.start, HIGH);
    delay(50);

    consecutiveDrdyTimeouts = 0;
    // DO NOT reset lastValidReading here: if the filter is at 100 g when we
    // reconfigure, the next DRDY timeout (during ADC restart) should still
    // return the last good 100 g reading rather than a jarring 0.
    printf("[ADS1262] reconfigure() complete\n");
    return true;
}

// ---------------------------------------------------------------------------
// readData() – wait for DRDY then clock out 3 bytes.
//
// DRDY detection strategy for the ADS1262:
//   The ADS1262 DOUT pin is dual-purpose:
//     • During an SPI read (CS LOW)  → carries conversion data
//     • While idle        (CS HIGH)  → mirrors the DRDY signal (goes LOW when
//                                       a new result is ready)
//   Because CS is HIGH here, the ESP32 SPI peripheral is idle and is NOT
//   driving MISO.  The ADS1262 is driving DOUT/MISO LOW as a DRDY signal,
//   and digitalRead() on the MISO pin can safely detect it.
//
//   If the board also has a separate dedicated DRDY wire (pins.dready), we
//   check that too.  Either pin going LOW is sufficient.
// ---------------------------------------------------------------------------
int32_t ADS1262::readData() {
    uint32_t t0 = micros();
    bool drdyLow = false;
    uint32_t waitTime = 0;

    while (true) {
        // Check dedicated DRDY pin AND DOUT/MISO (ADS1262 drives both when idle).
        // CS is HIGH here so the SPI peripheral is NOT driving MISO – only the
        // ADS1262 can, and it does so to signal data ready.
        if (digitalRead(pins.dready) == LOW || digitalRead(pins.dout) == LOW) {
            drdyLow = true;
            break;
        }
        waitTime = micros() - t0;
        if (waitTime > 100000) break;   // 100 ms hard timeout
        delayMicroseconds(10);
    }

    if (!drdyLow) {
        consecutiveDrdyTimeouts++;
        printf("[ADS1262] DRDY timeout #%u after %lu us – returning last good value\n",
               consecutiveDrdyTimeouts, waitTime);

        if (consecutiveDrdyTimeouts >= ADC_REINIT_THRESHOLD) {
            printf("[ADS1262] %u consecutive timeouts – triggering reconfigure()\n",
                   consecutiveDrdyTimeouts);
            reconfigure();
        }
        // Return the last confirmed-good reading to keep the filter stable.
        return lastValidReading;
    }

    // DRDY fired – reset the timeout counter.
    consecutiveDrdyTimeouts = 0;

    // Clock out the conversion result via RDATA1 command.
    SPI.beginTransaction(spiSettings);
    digitalWrite(pins.cs, LOW);
    delayMicroseconds(2);  // t(CSSC)

    spiTransfer(0x12);     // RDATA1 command
    delayMicroseconds(1);

    uint8_t b[3];
    for (int i = 0; i < 3; ++i) b[i] = spiTransfer(0x00);

    digitalWrite(pins.cs, HIGH);
    SPI.endTransaction();

    // Combine 3 bytes → signed 24-bit
    int32_t raw24 = ((int32_t)b[0] << 16) | ((int32_t)b[1] << 8) | b[2];
    if (raw24 & 0x800000) raw24 |= 0xFF000000;  // sign extend

    // Periodic debug (every 500 reads) so we keep seeing raw ADC health without
    // flooding the log.  Still prints anomalies (saturation, all-FF, all-00) always.
    static uint32_t dbg_count = 0;
    ++dbg_count;
    bool allZero = (b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x00);
    bool allFF   = (b[0] == 0xFF && b[1] == 0xFF && b[2] == 0xFF);
    bool anomaly = (raw24 >= 8388600) || (raw24 <= -8388600) || allFF || allZero;
    if (anomaly || (dbg_count % 500 == 1)) {
        float voltage_mV = ((float)raw24 / 8388608.0f) * (3300.0f / 8.0f);
        printf("[ADS1262] #%lu Raw24:%10ld (%+7.3f mV) Bytes:%02X %02X %02X DRDY:%lu us%s%s%s\n",
               (unsigned long)dbg_count, (long)raw24, voltage_mV,
               b[0], b[1], b[2], waitTime,
               (raw24 >= 8388600)  ? " **POS_SAT**" : "",
               (raw24 <= -8388600) ? " **NEG_SAT**" : "",
               anomaly && !(raw24 >= 8388600) && !(raw24 <= -8388600) ? " **BAD_BYTES**" : "");
    }

    // Reject all-zero bytes: the ADS1262 outputs 0x00 0x00 0x00 for the first
    // few conversions after a reset/reconfigure while its pipeline refills.
    // A real perfectly-balanced bridge reading 0 counts is virtually impossible.
    // Return the last good value so the IIR filter is not dragged toward zero.
    if (allZero) {
        printf("[ADS1262] Post-reset zero detected – returning last good value (%ld)\n",
               (long)lastValidReading);
        return lastValidReading;
    }

    lastValidReading = raw24;
    return raw24;
}

void ADS1262::reset() {
    digitalWrite(pins.cs, HIGH);
    delay(10);

    SPI.beginTransaction(spiSettings);
    digitalWrite(pins.cs, LOW);
    spiTransfer(CMD_RESET);
    digitalWrite(pins.cs, HIGH);
    SPI.endTransaction();

    delay(50);
}

void ADS1262::enableInternalReference() {
    // Disabled for now to avoid incorrect register writes without full map
    // Configure external or default reference used by your hardware
}

void ADS1262::printDiagnostics() {
    printf("\n=== ADS1262 REGISTER DIAGNOSTICS ===\n");

    uint8_t reg01 = readReg(0x01);
    uint8_t reg02 = readReg(0x02);
    uint8_t reg03 = readReg(0x03);
    uint8_t reg04 = readReg(0x04);
    uint8_t reg05 = readReg(0x05);
    uint8_t reg06 = readReg(0x06);
    uint8_t reg0F = readReg(0x0F);

    printf("REG_POWER (0x01):     0x%02X (expect 0x11)\n", reg01);
    printf("REG_INTERFACE (0x02): 0x%02X (expect 0x00)\n", reg02);
    printf("REG_MODE0 (0x03):     0x%02X (expect 0x0E for 5 SPS)\n", reg03);
    printf("REG_MODE1 (0x04):     0x%02X (expect 0x86 for SINC4+CHOP)\n", reg04);
    printf("REG_MODE2 (0x05):     0x%02X (expect 0x30 for 8x gain)\n", reg05);
    printf("REG_INPMUX (0x06):    0x%02X (expect 0x01 for AIN0 vs AIN1)\n", reg06);
    printf("REG_REFMUX (0x0F):    0x%02X (expect 0x52 for external ref AIN2/AIN3)\n", reg0F);

    // Read offset calibration registers (0x07-0x09)
    uint8_t ofcal0 = readReg(0x07);
    uint8_t ofcal1 = readReg(0x08);
    uint8_t ofcal2 = readReg(0x09);
    int32_t ofcal = ((int32_t)ofcal0 << 16) | ((int32_t)ofcal1 << 8) | ofcal2;
    // Sign extend from 24-bit
    if (ofcal & 0x800000) {
        ofcal |= 0xFF000000;
    }
    printf("OFCAL (0x07-09):      0x%02X%02X%02X = %ld (offset calibration)\n", ofcal0, ofcal1, ofcal2, (long)ofcal);

    // Extract and display MODE2 gain
    uint8_t gain_bits = (reg05 >> 4) & 0x07;
    const char* gain_str[] = {"1x", "2x", "4x", "8x", "16x", "32x", "64x", "128x"};
    printf("MODE2 GAIN[2:0]:      %s (bits 6:4 = %02Xb)\n", gain_str[gain_bits], gain_bits);

    printf("=====================================\n\n");
}

void ADS1262::setInputMux(uint8_t posInput, uint8_t negInput) {
    uint8_t muxValue = (posInput << 4) | (negInput & 0x0F);
    writeReg(0x06, muxValue); // INPMUX register on ADS1262
}

void ADS1262::setDataRate(uint8_t dataRate) {
    writeReg(0x03, dataRate); // MODE0 register holds data rate on ADS1262
}

void ADS1262::startConversion() {
    // START pin should ALWAYS be HIGH for continuous conversion mode
    // This function is rarely needed since START is held HIGH during init
    // But if called, ensure START stays HIGH
    digitalWrite(pins.start, HIGH);

    // Optionally send START command via SPI (not strictly necessary if START pin is HIGH)
    // Commenting out to avoid unnecessary SPI traffic
    // SPI.beginTransaction(spiSettings);
    // digitalWrite(pins.cs, LOW);sheet i 
    // spiTransfer(CMD_START);
    // digitalWrite(pins.cs, HIGH);
    // SPI.endTransaction();
}

void ADS1262::writeReg(uint8_t reg, uint8_t value) {
    SPI.beginTransaction(spiSettings);
    digitalWrite(pins.cs, LOW);
    spiTransfer(CMD_WREG | reg);
    spiTransfer(0x00);  // Number of registers - 1
    spiTransfer(value);
    digitalWrite(pins.cs, HIGH);
    SPI.endTransaction();
}

uint8_t ADS1262::readReg(uint8_t reg) {
    SPI.beginTransaction(spiSettings);
    digitalWrite(pins.cs, LOW);
    spiTransfer(CMD_RREG | reg);
    spiTransfer(0x00);  // Number of registers - 1
    uint8_t value = spiTransfer(0x00);
    digitalWrite(pins.cs, HIGH);
    SPI.endTransaction();
    return value;
}

void ADS1262::getLastBytes(uint8_t out[6]) const {
    for (int i = 0; i < 6; ++i) out[i] = lastBytes[i];
}

uint8_t ADS1262::spiTransfer(uint8_t data) {
    return SPI.transfer(data);
}

#endif // !USE_PROTOCENTRAL_ADS1262
