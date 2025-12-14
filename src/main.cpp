// main.cpp
#include <Arduino.h>
#include <WiFi.h>
#include "Application.h"
#include "Utils/SerialPort.h"
#include "Communication/Node.h"
#include "Communication/HandleEvents.h"
#include "Utils/DisplayUtils.h"
#include "StateFactory.h"
#include "Applications/MenuState.h"

using namespace NuggetsInc;
Node* node = nullptr;  // Lazy initialization in setup()

// Disable WiFi auto-start to prevent blocking during setup
void setup_wifi_disabled() {
    WiFi.mode(WIFI_OFF);
    WiFi.disconnect(true);  // Turn off WiFi and disable auto-connect
}

void setup() {
    // Disable watchdog immediately
    disableCore0WDT();
    disableCore1WDT();

    // Disable WiFi auto-start BEFORE Serial.begin() to prevent blocking
    setup_wifi_disabled();

    // Initialize Serial on UART0 (RX=GPIO44, TX=GPIO43) which is connected to the USB-UART on the custom PCB
    // On ESP32-S3 the default "Serial" can be mapped to USB-CDC instead of the external UART.
    // For this board we want all Serial.println() output to go to the standard UART0 pins.
    Serial.begin(115200, SERIAL_8N1, 44, 43);
    delay(2000); // Give extra time for serial to stabilize

    // Use both printf (goes through ESP-IDF logging) and Serial.println for comparison
    printf("\n\n[APP] === ESP32 STARTUP DEBUG (printf) ===\n");
    printf("[APP] Serial.begin completed (printf)\n");

    Serial.println("\n\n=== ESP32 STARTUP DEBUG (Serial) ===");
    Serial.println("Serial initialized successfully (Serial)");
    Serial.flush();

    try {
        // Create Node instance now that we're in setup()
        Serial.println("Creating Node instance...");
        Serial.flush();
        node = new Node();
        Serial.println("Node instance created");
        Serial.flush();

        // Initialize Serial Communication
        Serial.println("Calling SerialPort::begin()...");
        Serial.flush();
        NuggetsInc::SerialPort::begin();
        Serial.println("SerialPort::begin() completed");
        Serial.flush();

        // Initilize Node Networking
        Serial.println("Calling node->begin()...");
        Serial.flush();
        node->begin();
        Serial.println("node->begin() completed");
        Serial.flush();

        // Initialize The Application
        Serial.println("Calling Application::getInstance().init()...");
        Serial.flush();
        Application::getInstance().init();
        Serial.println("Application init completed");
        Serial.flush();

        Serial.println("Creating MENU_STATE...");
        Serial.flush();
        Application::getInstance().changeState(StateFactory::createState(StateType::MENU_STATE));
        Serial.println("=== SETUP COMPLETE ===");
        Serial.flush();
    } catch (...) {
        Serial.println("EXCEPTION CAUGHT IN SETUP!");
        Serial.flush();
    }
}

void loop() {
    static unsigned long lastHeartbeat = 0;
    static bool firstLoop = true;

    if (firstLoop) {
        Serial.println("=== ENTERING MAIN LOOP ===");
        Serial.flush();
        firstLoop = false;
    }

    // Handle incoming serial data
    if (Serial.available() > 0)
    {
        Serial.println("Serial data received");
        NuggetsInc::SerialPort::handleIncommingSerialData();
    }

    // Initial Connection Made, Reset The Menu
    if (node && node->InitializeMenu) {
        node->InitializeMenu = false;
        Serial.println("Switching to MENU_STATE");

        Application::getInstance().changeState(StateFactory::createState(StateType::MENU_STATE));
    }

    // Handle Application Events
    try {
        Application::getInstance().run();
    } catch (...) {
        Serial.println("Exception in Application::run()!");
    }

    // Yield to allow other tasks to run
    // This is important in FreeRTOS to prevent blocking
    yield();
}