#include "Router.h"

namespace NuggetsInc {

String Router::macToString(const uint8_t mac[6]) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

bool Router::stringToMac(const String& s, uint8_t out[6]) {
    int b[6];
    if (sscanf(s.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
        for (int i = 0; i < 6; i++) {
            out[i] = (uint8_t)b[i];
        }
        return true;
    }
    return false;
}

//add a mac (uint8_t*) to a path (const char*)
void Router::addMacToPath(const uint8_t* mac, const char* path, char* out) {
    if (path == nullptr) {
        memcpy(out, mac, 6);
        return;
    }

    //path to mac array
    std::vector<uint8_t*> macAddressList = pathToMacArray(path);

    //add mac to mac array
    macAddressList.push_back(const_cast<uint8_t*>(mac));

    //mac array to string
    String newPath = macArrayToString(macAddressList);

    //string to char*
    memcpy(out, newPath.c_str(), newPath.length() + 1);
}

// add mac (uint8_t*) to a path (const char*) and return the new path (const char*)
String Router::addMacToPath(const uint8_t* mac, const char* path) {
    char out[50];
    addMacToPath(mac, path, out);
    return String(out);
}

//convert a path (const char*) to a list of mac addresses (std::vector<uint8_t*>)
std::vector<uint8_t*> Router::pathToMacArray(const char* path) {
    std::vector<uint8_t*> macAddressList;
    if (path == nullptr) {
        return macAddressList;
    }

    // Create a copy of the path string to avoid modifying the original
    String pathStr = String(path);
    std::vector<String> macAddresses;

    int startIndex = 0;
    int commaIndex = pathStr.indexOf(',', startIndex);

    while (commaIndex != -1) {
        String macAddress = pathStr.substring(startIndex, commaIndex);
        macAddress.trim();
        if (macAddress.length() > 0) {
            macAddresses.push_back(macAddress);
        }
        startIndex = commaIndex + 1;
        commaIndex = pathStr.indexOf(',', startIndex);
    }

    // Add the last MAC address (after the last comma or the only one)
    String lastMacAddress = pathStr.substring(startIndex);
    lastMacAddress.trim();
    if (lastMacAddress.length() > 0) {
        macAddresses.push_back(lastMacAddress);
    }

    macAddressList = stringToMacArray(macAddresses);
    return macAddressList;
}

//convert a path (const char*) to a list of mac address strings (std::vector<String>)
std::vector<String> Router::pathToStringArray(const char* path) {
    std::vector<String> macAddresses;
    if (path == nullptr) {
        return macAddresses;
    }

    // Create a copy of the path string to avoid modifying the original
    String pathStr = String(path);

    int startIndex = 0;
    int commaIndex = pathStr.indexOf(',', startIndex);

    while (commaIndex != -1) {
        String macAddress = pathStr.substring(startIndex, commaIndex);
        macAddress.trim();
        if (macAddress.length() > 0) {
            macAddresses.push_back(macAddress);
        }
        startIndex = commaIndex + 1;
        commaIndex = pathStr.indexOf(',', startIndex);
    }

    // Add the last MAC address (after the last comma or the only one)
    String lastMacAddress = pathStr.substring(startIndex);
    lastMacAddress.trim();
    if (lastMacAddress.length() > 0) {
        macAddresses.push_back(lastMacAddress);
    }

    return macAddresses;
}

uint8_t* Router::getLastMacFromPath(const char* path) {
    if (!path) return nullptr;

    uint8_t* outputMac = new uint8_t[6];

    std::vector<uint8_t*> macAddressList = pathToMacArray(path);

    if (macAddressList.empty()) {
        return nullptr;
    }

    memcpy(outputMac, macAddressList.back(), 6);
    return outputMac;
}

//remove last mac from string
String Router::removeLastMacFromPath(const char* path) {
    if (!path) return "";
    
    std::vector<String> macAddresses = pathToStringArray(path);
    if (macAddresses.empty()) {
        return "";
    }
    macAddresses.pop_back();
    String newPath;
    for (const String& macAddress : macAddresses) {
        if (newPath.length() > 0) {
            newPath += ",";
        }
        newPath += macAddress;
    }

    newPath += '\0';
    return newPath;
}

// Convert macadresses to uint8_t arrays
std::vector<uint8_t*> Router::stringToMacArray(const std::vector<String>& macAddresses) {
    std::vector<uint8_t*> macAddressList;
    for (const String& macAddress : macAddresses) {
        uint8_t* mac = new uint8_t[6];
        if (Router::stringToMac(macAddress, mac)) {
            macAddressList.push_back(mac);
        } else {
            delete[] mac;
        }
    }
    return macAddressList;
}

//convert mac array to string
String Router::macArrayToString(const std::vector<uint8_t*>& macAddressList) {
    String path;
    for (const uint8_t* mac : macAddressList) {
        if (path.length() > 0) {
            path += ",";
        }
        path += macToString(mac);
    }
    return path;
}

// convert const char* to string 
String Router::pathToString(const char* path) {
    return String(path);
}

void Router::printMac(const uint8_t* mac, const char* prefix) {
    Serial.print(prefix);
    for (int i = 0; i < 6; i++) {
        Serial.printf("%02X", mac[i]);
        if (i < 5) Serial.print(":");
    }
    Serial.println();
}

//is valid mac
bool Router::checkValidMac (const uint8_t* mac) {
    if (mac == nullptr) {
        return false;
    }

    // Check if mac is all 0xFF
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0xFF) {
            break;
        }
        if (i == 5) {
            return false;
        }
    }

    // Check if mac is all zeros
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0) {
            return true;
        }
    }
    return false;
}



} // namespace NuggetsInc
