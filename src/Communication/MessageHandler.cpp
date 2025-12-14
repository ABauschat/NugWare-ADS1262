#include "MessageHandler.h"
#include "Router.h"
#include "NodeService.h"
#include "Node.h"
#include "HandleEvents.h"
#include "Applications/RelayState.h"
#include "MessageTypes.h"
#include "Utils/TimeUtils.h"
#include <WiFi.h>
#include <esp_now.h>
#include <cstring>
#include "MacAddressStorage.h"


namespace NuggetsInc {
using namespace NuggetsInc; // For TimeUtils functions

MessageHandler::MessageHandler(Router* router, NodeService* nodeService, Node* node)
    : router_(router), nodeService_(nodeService), node_(node) {}
    
void MessageHandler::processReceivedMessage(const uint8_t* senderMac, const struct_message& msg) {
    struct_message receivedMessage = msg;
    receivedMessage.messageType[sizeof(receivedMessage.messageType) - 1] = '\0';
    receivedMessage.data[sizeof(receivedMessage.data) - 1] = '\0';
    receivedMessage.path[sizeof(receivedMessage.path) - 1] = '\0';

    
    if (!isDestinationForSelf(receivedMessage)) {
        processForwardedMessage(receivedMessage);
        return;
    }

    if (strcmp(receivedMessage.messageType, "ack") == 0) {
        processAckMessage(senderMac, receivedMessage);
    }
    
    else if (strcmp(receivedMessage.messageType, "cmd") == 0) {
        processCommandMessage(senderMac, receivedMessage);
    }
}

void MessageHandler::processAckMessage(const uint8_t* senderMac, const struct_message& ackMsg) {
    if (node_) {
        node_->notifyAckReceived(ackMsg.messageID);
    }
}

void MessageHandler::processRelayConnectionMessage(const uint8_t* senderMac, const struct_message& cmdMsg) {
    MacAddressStorage& macStorage = MacAddressStorage::getInstance();
    std::vector<String> macAddresses = macStorage.getAllMacAddresses();
    std::vector<uint8_t*> macList = Router::stringToMacArray(macAddresses);
    std::vector<uint8_t*> pathList = Router::stringToMacArray(macAddresses);

    // if mac address is in both lists, remove it from macAddresses
    for (auto it = macList.begin(); it != macList.end(); ) {
        bool found = false;
        for (const uint8_t* pathMac : pathList) {
            if (memcmp(*it, pathMac, 6) == 0) {
                found = true;
                break;
            }
        }
        if (found) {
            delete[] *it;  
            it = macList.erase(it);
        } else {
            ++it;
        }
    }

    // Remove SenderMac from macList
    for (auto it = macList.begin(); it != macList.end(); ) {
        if (memcmp(*it, cmdMsg.SenderMac, 6) == 0) {
            delete[] *it;  
            it = macList.erase(it);
        } else {
            ++it;
        }
    }

    // add destinationMac to macList
    uint8_t* destMacCopy = new uint8_t[6];
    memcpy(destMacCopy, cmdMsg.destinationMac, 6);
    macList.push_back(destMacCopy);
   
    for (const uint8_t* macAddress : macList) {
        struct_message msgCopy = cmdMsg;
        String path = String(msgCopy.path);
        Serial.println("Sending Before: " + String(msgCopy.path));
        Router::addMacToPath(macAddress, msgCopy.path, msgCopy.path);
        Serial.println("Sending After: " + String(msgCopy.path));
        node_->sendDataNonBlocking(msgCopy);
    }

    // Clean up allocated memory
    for (uint8_t* mac : macList) {
        delete[] mac;
    }
    for (uint8_t* mac : pathList) {
        delete[] mac;
    }
}

void MessageHandler::processForwardedMessage(const struct_message& cmdMsg) {
    if (cmdMsg.commandID == CMD_RELAY_CONNECTION) {
        processRelayConnectionMessage(cmdMsg.SenderMac, cmdMsg);
        return;
    }

    Serial.println("Forwarding message: " + String(cmdMsg.messageType));

    struct_message msgCopy = cmdMsg;
    String path = Router::removeLastMacFromPath(cmdMsg.path);
    memcpy(msgCopy.path, path.c_str(), sizeof(msgCopy.path));

    node_->sendDataNonBlocking(msgCopy);
}


void MessageHandler::processCommandMessage(const uint8_t* senderMac, const struct_message& cmdMsg) {
    if (isDuplicateMessage(cmdMsg.SenderMac, cmdMsg.messageID)) {
        return;
    }

    //nodeService_->sendAck(cmdMsg.messageID, cmdMsg.path);
    if(cmdMsg.commandID != CMD_SYNC_NODES && cmdMsg.commandID != CMD_RELAY_ESTABLISH) {
        HandleEvents::getInstance().executeCommand(cmdMsg.commandID, cmdMsg.data);
        return;
    } 

    if(cmdMsg.commandID == CMD_RELAY_ESTABLISH) {
        Node* node = Node::getActiveInstance();
        node->RelayEstablished(cmdMsg.commandID, cmdMsg.path);
        return;
    }

    if (RelayState::getActiveInstance()) {
        RelayState::getActiveInstance()->handleSyncNodes(cmdMsg.data);
    }
}

bool MessageHandler::isDuplicateMessage(const uint8_t src[6], uint32_t messageID) {
    msec32 nowMs = now_ms();
    const msec32 window = 2000;
    MsgKey key;
    memcpy(key.mac, src, 6);
    key.id = messageID;
    
    auto found = recentMsgCache_.find(key);
    if (found != recentMsgCache_.end()) {
        return true;
    }
    
    if (recentMsgCache_.size() > 50) {
        auto it = recentMsgCache_.begin();
        while (it != recentMsgCache_.end()) {
            if (!within_window(it->second, window)) {
                it = recentMsgCache_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    recentMsgCache_[key] = nowMs;
    return false;
}

bool MessageHandler::isDestinationForSelf(const struct_message& msg) {
    Serial.println("Checking destination: " + macToString(msg.destinationMac));
    if (router_->checkValidMac(msg.destinationMac)) {
        return memcmp(msg.destinationMac, selfMAC_, 6) == 0;
    }

    return true;
}

void MessageHandler::setSelfMac(uint8_t mac[6]) {
    memcpy(selfMAC_, mac, 6);
}

String MessageHandler::macToString(const uint8_t mac[6]) {
    return Router::macToString(mac);
}

bool MessageHandler::stringToMac(const String& s, uint8_t out[6]) {
    return Router::stringToMac(s, out);
}

} // namespace NuggetsInc
