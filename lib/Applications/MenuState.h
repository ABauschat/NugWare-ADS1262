#ifndef MENUSTATE_H
#define MENUSTATE_H

#include "State.h"
#include "DisplayUtils.h"
#include "HandleEvents.h"
#include "Utils/MenuService.h"
#include <Arduino.h>
#include "HX711.h"   // Include the HX711 library
#include "Utils/LoadCellReader.h"

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

    MenuService* menuService;
    LoadCellReader* loadCell;
    unsigned long lastWeightUpdate;
    static MenuState* activeInstance;
};

} // namespace NuggetsInc

#endif // MENUSTATE_H
