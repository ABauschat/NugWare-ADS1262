#ifndef ROUTER_H
#define ROUTER_H

#include <Arduino.h>
#include <vector>

namespace NuggetsInc {

// Minimal router - only MAC utility functions for direct communication
class Router {
public:
    Router() {}
    static String macToString(const uint8_t mac[6]);
    static bool stringToMac(const String& s, uint8_t out[6]);
    static std::vector<uint8_t*> stringToMacArray(const std::vector<String>& macAddresses);
    static void addMacToPath(const uint8_t* mac, const char* path, char* out);
    static String addMacToPath(const uint8_t* mac, const char* path);
    static std::vector<uint8_t*> pathToMacArray(const char* path);
    static std::vector<String> pathToStringArray(const char* path);
    static String removeLastMacFromPath(const char* path);
    static String macArrayToString(const std::vector<uint8_t*>& macAddressList);
    static String pathToString(const char* path);
    static uint8_t* getLastMacFromPath(const char* path);
    static void printMac(const uint8_t* mac, const char* prefix = "");
    static bool checkValidMac (const uint8_t* mac);
    
};

} // namespace NuggetsInc

#endif // ROUTER_H
