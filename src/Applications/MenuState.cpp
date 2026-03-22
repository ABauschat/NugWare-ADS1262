#include "MenuState.h"
#include "Communication/HandleEvents.h"
#include "Application.h"
#include "StateFactory.h"
#include "Utils/Device.h"
#include "Utils/MenuService.h"
#include <Arduino.h>
#include "Communication/NodeService.h"
#include "Communication/Node.h"
#include "Utils/LoadCellReader.h"
#include "Utils/ADS1262.h"
#include <Wire.h>

namespace NuggetsInc
{

MenuState* MenuState::activeInstance = nullptr;

    MenuState::MenuState()
        : menuService(new MenuService()), loadCell(nullptr), lastWeightUpdate(0)
    {
        setupMenu();
    }

    MenuState::~MenuState()
    {
        delete menuService;
        if (loadCell) { delete loadCell; loadCell = nullptr; }
    }

    void MenuState::setupMenu()
    {
        // Configure menu to leave space at the top for live weight
        menuService->createMenu("Load Cell");
        auto &cfg = menuService->getConfig();
        cfg.startY = 60;        // start items lower to make room for live weight
        cfg.itemTextSize = 2;   // make items readable
        cfg.titleTextSize = 2;

        // Calibration options
        menuService->addMenuItem("Zero Calibration", [this]() {
            if (loadCell) { loadCell->calibrateZero(); }
            menuService->drawMenu("Zeroed");
        });

        menuService->addMenuItem("Calibrate to 5g", [this]() {
            if (loadCell) { loadCell->calibrateSpan(5.0f); }
            menuService->drawMenu("Calibrated 5g");
        });

        menuService->addMenuItem("Save Calibration", [this]() {
            if (loadCell) {
                bool ok = loadCell->saveCalibration();
                menuService->drawMenu(ok ? "Saved to Flash!" : "Save Failed!");
            }
        });

        menuService->addMenuItem("Reset Calibration", [this]() {
            if (loadCell) { loadCell->setCalibrationValues(0, 1.0f); }
            menuService->drawMenu("Calibration Reset");
        });

        // Read speed presets
        menuService->addMenuItem("Speed: Slow", [this]() {
            if (loadCell) { loadCell->setReadSpeed(LoadCellReader::SLOW); }
            menuService->drawMenu("Speed: Slow");
        });

        menuService->addMenuItem("Speed: Medium", [this]() {
            if (loadCell) { loadCell->setReadSpeed(LoadCellReader::MEDIUM); }
            menuService->drawMenu("Speed: Medium");
        });

        menuService->addMenuItem("Speed: Fast", [this]() {
            if (loadCell) { loadCell->setReadSpeed(LoadCellReader::FAST); }
            menuService->drawMenu("Speed: Fast");
        });

        // Peak hold
        menuService->addMenuItem("Reset Peak", [this]() {
            if (loadCell) { loadCell->resetPeak(); }
            menuService->drawMenu("Peak Reset");
        });
    }

    void MenuState::onEnter()
    {
        activeInstance = this;
        printf("[MenuState] onEnter() called\n");

        menuService->drawMenu();
        printf("[MenuState] Menu drawn\n");

        // Initialize load cell
        if (!loadCell) {
            printf("[MenuState] Initializing LoadCell...\n");

            // IMPORTANT: GPIO 6 is reserved for SPI flash! Changed DRDY to GPIO 12
            ADS1262::PinConfig pins{4, 5, 12, 7, 8, 9}; // DIN, DOUT, DRDY, SCLK, CS, START
            loadCell = new LoadCellReader(pins);

            printf("[MenuState] LoadCellReader created, calling begin()...\n");

            if (!loadCell->begin()) {
                printf("[MenuState] ERROR: LoadCell begin() failed!\n");
            } else {
                printf("[MenuState] LoadCell initialized successfully\n");

                // Print ADS1262 register diagnostics to verify configuration
                printf("[MenuState] Reading back ADS1262 registers for verification...\n");
                loadCell->getADC().printDiagnostics();
            }
        }

        lastWeightUpdate = 0; // force immediate update
        printf("[MenuState] Calling updateWeightDisplay()...\n");
        updateWeightDisplay(); // draw immediately so user sees a value right away
        printf("[MenuState] onEnter() complete\n");
    }

    void MenuState::onExit()
    {
        activeInstance = nullptr;
        menuService->clearScreen();
    }

    void MenuState::update()
    {
        // Handle input
        Event event;
        if (xQueueReceive(HandleEvents::getInstance().getEventQueue(), &event, 0) == pdPASS)
        {
            switch (event.type)
            {
            case EventType::MOVE_UP:
                menuService->moveUp();
                break;
            case EventType::MOVE_DOWN:
                menuService->moveDown();
                break;
            case EventType::SELECT:
                executeSelection();
                break;
            case EventType::BACK:
                onExitSelection();
                break;
            case EventType::BOOOP:
                onEnter();
                break;
            }
        }

        // Periodic live weight display (interval depends on read speed)
        updateWeightDisplay();
    }

    void MenuState::executeSelection()
    {
        menuService->selectCurrent();
    }

    void MenuState::onExitSelection()
    {
        menuService->drawMenu();
        Node* connector = Node::getActiveInstance();
        if (connector) {
            connector->setRouteMode(Node::RouteMode::AUTO);
        }
    }

    void MenuState::updateWeightDisplay()
    {
        if (!loadCell) return;
        unsigned long now = millis();
        if (now - lastWeightUpdate < loadCell->getUpdateIntervalMs()) return;

        // readWeight() drives the IIR filter internally and updates peakRaw as a
        // side-effect, so we only need a single call here.
        float weight = loadCell->readWeight();
        float peak   = loadCell->getPeakWeight();

        // Compose display strings
        char line1[32];
        snprintf(line1, sizeof(line1), "W: %+.2f g", weight);
        char line2[32];
        snprintf(line2, sizeof(line2), "Pk:%+.2f g", peak);

        printf("[MenuState] W: %.2f g  Pk: %.2f g\n", weight, peak);

        // Draw header (y=0..57, menu starts at y=60)
        DisplayUtils* d = menuService->getDisplayUtils();
        if (d) {
            d->fillRect(0, 0, SCREEN_WIDTH, 58, COLOR_BLACK);

            // Live weight – green, textSize 2 (~16 px tall)
            d->setCursor(4, 4);
            d->setTextSize(2);
            d->setTextColor(COLOR_GREEN);
            d->println(String(line1));

            // Peak hold – orange, textSize 2
            d->setCursor(4, 28);
            d->setTextSize(2);
            d->setTextColor(COLOR_ORANGE);
            d->println(String(line2));
        }

        lastWeightUpdate = now;
    }

    MenuState* MenuState::getActiveInstance() {
        return activeInstance;
    }

} // namespace NuggetsInc
