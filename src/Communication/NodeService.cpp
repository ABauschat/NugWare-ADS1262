#include "NodeService.h"
#include "MessageTypes.h"
#include "Router.h"
#include <cstring>
#include <WiFi.h>

namespace NuggetsInc {

NodeService::NodeService(Node* node) : node_(node), router_(new Router()) {}

void NodeService::buildCommandMessage(struct_message& out,
                                      uint8_t commandID,
                                      const char* data) {
    
    memset(&out, 0, sizeof(out));
    strcpy(out.messageType, "cmd");
    out.commandID = commandID;
    out.path[0] = '\0';
    out.destinationMac[0] = '\0';

    if (data) {
        strncpy(out.data, data, sizeof(out.data)-1);
        out.data[sizeof(out.data)-1] = '\0';
    } else {
        out.data[0] = '\0';
    }
}

void NodeService::buildProbeMessage(struct_message& out, uint8_t* destinationMac, uint8_t* senderMac) {
    memset(&out, 0, sizeof(out));
    strcpy(out.messageType, "cmd");
    out.commandID = CMD_RELAY_CONNECTION;
    memcpy(out.destinationMac, destinationMac, 6);
    memcpy(out.SenderMac, senderMac, 6);
    out.data[0] = '\0';
    out.path[0] = '\0';
}

void NodeService::createPathWithMac(struct_message& out, uint8_t* mac) {
    const char* path = router_->macToString(mac).c_str();

    if (path) {
        strncpy(out.path, path, sizeof(out.path)-1);
        out.path[sizeof(out.path)-1] = '\0';
    } else {
        out.path[0] = '\0';
    }
}

void NodeService::setMessageID(struct_message& out, uint32_t messageID) {
    out.messageID = messageID;
}

void NodeService::setPath(struct_message& out, const char* path) {
    if (path) {
        strncpy(out.path, path, sizeof(out.path)-1);
        out.path[sizeof(out.path)-1] = '\0';
    } else {
        out.path[0] = '\0';
    }
}

bool NodeService::sendCommandBlocking(uint8_t commandID,
                                      const char* data,
                                      uint32_t ackTimeoutMs,
                                      int maxRetries,
                                      Node::RouteMode routeMode,
                                      bool frameFirst) {
    if (!node_) return false;
    struct_message m;
    buildCommandMessage(m, commandID, data);
    return node_->sendDataBlocking(m, ackTimeoutMs, maxRetries);
}

bool NodeService::sendCommandNonBlocking(uint8_t commandID,
                                         const char* data) {
    if (!node_) return false;
    struct_message m; buildCommandMessage(m, commandID, data);
    return node_->sendDataNonBlocking(m);
}

bool NodeService::sendRawBlocking(const struct_message& msg,
                                  uint32_t ackTimeoutMs,
                                  int maxRetries,
                                  Node::RouteMode routeMode,
                                  bool frameFirst) {
    if (!node_) return false;
    return node_->sendDataBlocking(msg, ackTimeoutMs, maxRetries);
}

void NodeService::buildAckMessage(struct_message& out,
                                  uint32_t originalMessageID,
                                  const char* originalPath) {
    memset(&out, 0, sizeof(out));
    out.messageID = originalMessageID;
    strcpy(out.messageType, "ack");
    out.commandID = 0;
    out.data[0] = '\0';
    out.path[0] = '\0'; // No path needed for direct communication
}

bool NodeService::sendBoop(const char* macData) {
    return sendCommandBlocking(CMD_BOOOP, macData);
}

bool NodeService::sendSync(const char* macData) {
    return sendCommandBlocking(CMD_SYNC_NODES, macData);
}

bool NodeService::sendAck(uint32_t originalMessageID, const char* originalPath) {
    if (!node_) return false;
    struct_message ack;
    buildAckMessage(ack, originalMessageID, originalPath);
    return node_->sendDataNonBlocking(ack);
}

} // namespace NuggetsInc

