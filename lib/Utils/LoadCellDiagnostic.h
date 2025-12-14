#ifndef LOADCELL_DIAGNOSTIC_H
#define LOADCELL_DIAGNOSTIC_H

#include "Utils/ADS1262.h"

class LoadCellDiagnostic {
public:
    LoadCellDiagnostic(const ADS1262::PinConfig& adcConfig);
    
    // Run comprehensive diagnostic
    void runFullDiagnostic();
    
private:
    ADS1262 adc;
};

#endif // LOADCELL_DIAGNOSTIC_H

