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
    : pins(config), spiSettings(1000000, MSBFIRST, SPI_MODE1) {
    // Init debug buffer
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
    return true;
}



int32_t ADS1262::readData() {
    // CRITICAL: Wait for DRDY to go LOW (data ready)
    uint32_t t0 = micros();
    bool drdyLow = false;
    uint32_t waitTime = 0;

    while (true) {
        int d6 = digitalRead(pins.dready);   // dedicated DRDY
        int d5 = digitalRead(pins.dout);     // DOUT/DRDY (MISO) also asserts DRDY low on many boards
        if (d6 == LOW || d5 == LOW) { drdyLow = true; break; }

        waitTime = micros() - t0;
        if (waitTime > 100000) { // 100 ms safety timeout
            break;
        }
        delayMicroseconds(10);
    }

    if (!drdyLow) {
        static uint8_t warn = 0;
        if (warn < 3) {
            printf("[ADS1262] DRDY timeout after %lu us (D6=%d, D5=%d), trying read anyway\n",
                   waitTime, digitalRead(pins.dready), digitalRead(pins.dout));
            warn++;
        }
        // fall-through to attempt read to help diagnose wiring
    }

    // Use RDATA1 command (0x12) to read ADC1 conversion data
    // This is the proper way to read data in continuous conversion mode
    SPI.beginTransaction(spiSettings);
    digitalWrite(pins.cs, LOW);
    delayMicroseconds(2);  // t(CSSC) = CS low to first SCLK

    // Send RDATA1 command
    spiTransfer(0x12);  // RDATA1 command
    delayMicroseconds(1);  // Small delay after command

    // ADS1262 outputs 24-bit (3 bytes) conversion data for ADC1
    // (STATUS and CRC are disabled in INTERFACE register, so only 3 bytes)
    uint8_t b[3];
    for (int i = 0; i < 3; ++i) {
        b[i] = spiTransfer(0x00);  // Clock out data bytes
    }

    digitalWrite(pins.cs, HIGH);
    SPI.endTransaction();

    // Combine 3 bytes into 24-bit signed value
    int32_t raw24 = ((int32_t)b[0] << 16) | ((int32_t)b[1] << 8) | b[2];

    // Sign extend from 24-bit to 32-bit
    if (raw24 & 0x800000) {
        raw24 |= 0xFF000000;
    }

    // Calculate voltage for diagnostics
    // V = (ADC_code / 2^23) * (Vref / Gain)
    // Vref = excitation voltage (AIN2-AIN3), Gain = 8x, 2^23 = 8388608
    // For ratiometric measurement, we assume Vref = excitation voltage (typically 3.3V or 5V)
    // Using 3.3V as typical excitation voltage for now
    float voltage_mV = ((float)raw24 / 8388608.0f) * (3300.0f / 8.0f);

    // Debug output - EVERY READ for diagnosis
    static uint8_t dbg_count = 0;
    if (dbg_count < 100) {
        printf("[ADS1262] Raw24:%10ld (%+7.3f mV) Bytes:%02X %02X %02X DRDY:%lu us",
               (long)raw24, voltage_mV, b[0], b[1], b[2], waitTime);

        // Check for saturation (24-bit full scale is ±2^23 = ±8388608)
        if (raw24 >= 8388600) {
            printf(" **POS_SAT**");
        } else if (raw24 <= -8388600) {
            printf(" **NEG_SAT**");
        }

        // Check for suspicious patterns
        if (b[0] == 0xFF && b[1] == 0xFF && b[2] == 0xFF) {
            printf(" **ALL_FF**");
        } else if (b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x00) {
            printf(" **ALL_00**");
        }

        printf("\n");
        dbg_count++;
    }

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
