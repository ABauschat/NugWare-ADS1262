#include "SerialPort.h"
#include "Device.h"

namespace NuggetsInc {

void SerialPort::begin() {
	// Ensure Serial is using the hardware UART connected to the USB-UART bridge
	// (UART0: RX=GPIO44, TX=GPIO43). This keeps SerialPort output on the same
	// pins as the ROM bootloader so we see all messages on the same COM port.
	Serial.begin(115200, SERIAL_8N1, 44, 43);

    version();

    Serial.println("Serial Port Communication");
    Serial.println("=====================================");
    Serial.println("Serial Communicaion Ready at 115200 baud");
    Serial.println("Type -h or help for help");
}

String SerialPort::fetchMacAdress() {
    return WiFi.macAddress();
}

void SerialPort::handleIncommingSerialData() {
    if (Serial.available() > 0) { 
        String command = Serial.readStringUntil('\n'); 
        command.trim();

        if (command.equalsIgnoreCase("GET_MAC")) {
            Serial.println(fetchMacAdress()); 
        } else if (command.equalsIgnoreCase("-h") || command.equalsIgnoreCase("help")) {
            help();
        }  else if (command.equalsIgnoreCase("-v") || command.equalsIgnoreCase("version")) {
            version();
        } 
    }
}

void SerialPort::help() {
    Serial.println("Available Commands:");
    Serial.println("GET_MAC - Get the MAC address");
    Serial.println("-v or version - Show the version");
}

void SerialPort::version() {
    Serial.println("Device Name: " DeviceName);
    Serial.println("Version: " DeviceVersion);
}

} // namespace NuggetsInc
