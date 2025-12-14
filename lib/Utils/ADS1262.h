#ifndef ADS1262_H
#define ADS1262_H

#include <Arduino.h>
#include <SPI.h>

class ADS1262 {
public:
    struct PinConfig {
        uint8_t din;      // MOSI
        uint8_t dout;     // MISO
        uint8_t dready;   // DRDY
        uint8_t sclk;     // SCK
        uint8_t cs;       // CS
        uint8_t start;    // START
    };

    ADS1262(const PinConfig& config);

    bool begin();          // init + configure ADS1262
    int32_t readData();    // blocking read, returns filtered 32-bit value

    void getLastBytes(uint8_t out[6]) const;
    void printDiagnostics();  // Print register values for debugging

private:
    PinConfig    pins;
    SPISettings  spiSettings;
    uint8_t      lastBytes[6];
    bool         initialized;
    int32_t      lastReading;

    // Simple IIR low-pass state
    bool         filterInit;
    float        filteredValue;

    // Commands (ADS1262)
    static const uint8_t CMD_RESET = 0x06;
    static const uint8_t CMD_START = 0x08;
    static const uint8_t CMD_STOP  = 0x0A;
    static const uint8_t CMD_RDATA = 0x12;
    static const uint8_t CMD_WREG  = 0x40;
    static const uint8_t CMD_RREG  = 0x20;

    // ADS1262 register map (verified against official datasheet)
    // Register addresses (from ADS1262 datasheet Table 9-1)
    static const uint8_t REG_ID        = 0x00;
    static const uint8_t REG_POWER     = 0x01;
    static const uint8_t REG_INTERFACE = 0x02;
    static const uint8_t REG_MODE0     = 0x03;
    static const uint8_t REG_MODE1     = 0x04;
    static const uint8_t REG_MODE2     = 0x05;
    static const uint8_t REG_INPMUX    = 0x06;
    static const uint8_t REG_OFCAL0    = 0x07;
    static const uint8_t REG_OFCAL1    = 0x08;
    static const uint8_t REG_OFCAL2    = 0x09;
    static const uint8_t REG_FSCAL0    = 0x0A;
    static const uint8_t REG_FSCAL1    = 0x0B;
    static const uint8_t REG_FSCAL2    = 0x0C;
    static const uint8_t REG_IDACMUX   = 0x0D;
    static const uint8_t REG_IDACMAG   = 0x0E;
    static const uint8_t REG_REFMUX    = 0x0F;

    // MODE2 Register (0x05) - PGA Gain Configuration
    // GAIN[2:0] in bits 6:4 (from datasheet Table 9-26)
    // 000 = 1x,   001 = 2x,   010 = 4x,   011 = 8x
    // 100 = 16x,  101 = 32x,  110 = 64x,  111 = 128x
    static const uint8_t GAIN_1X   = 0x00;  // bits 6:4 = 000
    static const uint8_t GAIN_2X   = 0x10;  // bits 6:4 = 001
    static const uint8_t GAIN_4X   = 0x20;  // bits 6:4 = 010
    static const uint8_t GAIN_8X   = 0x30;  // bits 6:4 = 011
    static const uint8_t GAIN_16X  = 0x40;  // bits 6:4 = 100
    static const uint8_t GAIN_32X  = 0x50;  // bits 6:4 = 101
    static const uint8_t GAIN_64X  = 0x60;  // bits 6:4 = 110
    static const uint8_t GAIN_128X = 0x70;  // bits 6:4 = 111

    void   reset();
    void   writeReg(uint8_t reg, uint8_t value);
    uint8_t readReg(uint8_t reg);
    uint8_t spiTransfer(uint8_t data);
    bool   isDataReady() const;
    int32_t applyFilter(int32_t raw);   // IIR low-pass

    // Additional configuration functions
    void   enableInternalReference();
    void   setInputMux(uint8_t posInput, uint8_t negInput);
    void   setDataRate(uint8_t dataRate);
    void   startConversion();
};

#endif // ADS1262_H
