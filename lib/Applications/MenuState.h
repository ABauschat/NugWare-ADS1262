#ifndef MENUSTATE_H
#define MENUSTATE_H

#include "State.h"
#include "DisplayUtils.h"
#include "HandleEvents.h"
#include "Utils/MenuService.h"
#include <Arduino.h>
#include "HX711.h"   // Include the HX711 library
#include "Utils/LoadCellReader.h"
#include <Adafruit_INA219.h>

namespace NuggetsInc {

class MenuState : public AppState {
public:
    MenuState();
    ~MenuState();

    void onEnter() override;
    void onExit() override;
    void update() override;

    static MenuState* getActiveInstance();

private:
    void executeSelection();
    void onExitSelection();
    void setupMenu();
    void updateWeightDisplay();

    // Motor control (TB6612FNG)
    static constexpr uint8_t MOTOR_AIN1 = 17;
    static constexpr uint8_t MOTOR_AIN2 = 15;
    static constexpr uint8_t MOTOR_PWM  = 16;
    static constexpr uint8_t MOTOR_STBY =  3;
    bool motorRunning;

    void motorForward();
    void motorBackward();
    void motorOff();

    // INA219 power monitor (I2C: SDA=13, SCL=12)
    static constexpr uint8_t INA_SDA = 13;
    static constexpr uint8_t INA_SCL = 12;
    Adafruit_INA219 ina219;
    bool ina219Ready;

    MenuService* menuService;
    LoadCellReader* loadCell;
    unsigned long lastWeightUpdate;
    static MenuState* activeInstance;
};

} // namespace NuggetsInc

#endif // MENUSTATE_H
