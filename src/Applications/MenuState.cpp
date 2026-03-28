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
        : menuService(new MenuService()), loadCell(nullptr), lastWeightUpdate(0),
          motorRunning(false), ina219Ready(false)
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
            if (!loadCell) return;
            loadCell->calibrateSpan(5.0f);
            // Re-zero after the span weight is removed so ADC drift accumulated
            // since boot does not show as a non-zero baseline.
            menuService->drawMenu("Remove weight...");
            delay(3000);
            loadCell->calibrateZero();
            menuService->drawMenu("Done! Save cal.");
        });

        menuService->addMenuItem("Calibrate 100g", [this]() {
            if (!loadCell) return;
            loadCell->calibrateSpan(100.0f);
            // Re-zero after the span weight is removed so ADC drift accumulated
            // since boot does not show as a non-zero baseline.
            menuService->drawMenu("Remove weight...");
            delay(3000);
            loadCell->calibrateZero();
            menuService->drawMenu("Done! Save cal.");
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

        // Motor control (TB6612FNG: AIN1=IO17, AIN2=IO15, PWM=IO16)
        menuService->addMenuItem("Motor Forward", [this]() {
            motorForward();
            menuService->drawMenu("Motor FWD");
        });

        menuService->addMenuItem("Motor Backward", [this]() {
            motorBackward();
            menuService->drawMenu("Motor BWD");
        });

        menuService->addMenuItem("Motor Off", [this]() {
            motorOff();
            menuService->drawMenu("Motor OFF");
        });
    }

    void MenuState::motorForward() {
        printf("[Motor] FORWARD (STBY=H AIN1=H AIN2=L PWM=H)\n");
        digitalWrite(MOTOR_STBY, HIGH);   // enable driver first
        digitalWrite(MOTOR_AIN1, HIGH);
        digitalWrite(MOTOR_AIN2, LOW);
        digitalWrite(MOTOR_PWM,  HIGH);
        motorRunning = true;
    }

    void MenuState::motorBackward() {
        printf("[Motor] BACKWARD (STBY=H AIN1=L AIN2=H PWM=H)\n");
        digitalWrite(MOTOR_STBY, HIGH);   // enable driver first
        digitalWrite(MOTOR_AIN1, LOW);
        digitalWrite(MOTOR_AIN2, HIGH);
        digitalWrite(MOTOR_PWM,  HIGH);
        motorRunning = true;
    }

    void MenuState::motorOff() {
        printf("[Motor] OFF (AIN1=L AIN2=L PWM=L STBY=L)\n");
        digitalWrite(MOTOR_AIN1, LOW);
        digitalWrite(MOTOR_AIN2, LOW);
        digitalWrite(MOTOR_PWM,  LOW);
        digitalWrite(MOTOR_STBY, LOW);    // disable driver last
        motorRunning = false;
    }

    void MenuState::onEnter()
    {
        activeInstance = this;
        printf("[MenuState] onEnter() called\n");

        // Motor driver pins (TB6612FNG)
        pinMode(MOTOR_AIN1, OUTPUT);
        pinMode(MOTOR_AIN2, OUTPUT);
        pinMode(MOTOR_PWM,  OUTPUT);
        pinMode(MOTOR_STBY, OUTPUT);
        motorOff();   // start with motor stopped (STBY LOW = driver disabled)

        // INA219 power monitor (SDA=13, SCL=12)
        if (!ina219Ready) {
            Wire.begin(INA_SDA, INA_SCL);
            ina219Ready = ina219.begin();
            if (ina219Ready) {
                printf("[MenuState] INA219 initialized OK\n");
            } else {
                printf("[MenuState] WARNING: INA219 not found on SDA=%d SCL=%d\n", INA_SDA, INA_SCL);
            }
        }

        menuService->drawMenu();
        printf("[MenuState] Menu drawn\n");

        // Initialize load cell
        if (!loadCell) {
            printf("[MenuState] Initializing LoadCell...\n");

            // GPIO 6 is free on ESP32-S3 (flash is on GPIO 26-32, not 6-11 like original ESP32)
            ADS1262::PinConfig pins{4, 5, 6, 7, 8, 9}; // DIN, DOUT, DRDY, SCLK, CS, START
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

        // Read INA219
        float busV_V     = 0.0f;
        float current_mA = 0.0f;
        if (ina219Ready) {
            busV_V     = ina219.getBusVoltage_V();
            current_mA = ina219.getCurrent_mA();
        }

        // Compose display strings
        char line1[32];
        snprintf(line1, sizeof(line1), "W: %+.2f g", weight);
        char line2[32];
        snprintf(line2, sizeof(line2), "Pk:%+.2f g", peak);
        char line3[32];
        if (ina219Ready) {
            snprintf(line3, sizeof(line3), "%.2fV  %.1fmA", busV_V, current_mA);
        } else {
            snprintf(line3, sizeof(line3), "PWR: --");
        }

        printf("[MenuState] W: %.2f g  Pk: %.2f g  V: %.2f  I: %.1f mA\n",
               weight, peak, busV_V, current_mA);

        // Draw header (y=0..57, menu starts at y=60)
        DisplayUtils* d = menuService->getDisplayUtils();
        if (d) {
            d->fillRect(0, 0, SCREEN_WIDTH, 58, COLOR_BLACK);

            // Live weight – green, textSize 2 (~16 px tall)
            d->setCursor(4, 2);
            d->setTextSize(2);
            d->setTextColor(COLOR_GREEN);
            d->println(String(line1));

            // Peak hold – orange, textSize 2
            d->setCursor(4, 22);
            d->setTextSize(2);
            d->setTextColor(COLOR_ORANGE);
            d->println(String(line2));

            // Power – cyan, textSize 1 (fits in y=42..57)
            d->setCursor(4, 44);
            d->setTextSize(1);
            d->setTextColor(0x07FF); // cyan
            d->println(String(line3));
        }

        lastWeightUpdate = now;
    }

    MenuState* MenuState::getActiveInstance() {
        return activeInstance;
    }

} // namespace NuggetsInc
