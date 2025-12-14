// Node.cpp - Simplified ESP-NOW communication node
#include "NodeService.h"
#include "Node.h"
#include "Router.h"
#include "MessageHandler.h"
#include "MacAddressStorage.h"
#include "HandleEvents.h"
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <algorithm>

namespace NuggetsInc
{
    Node *Node::activeInstance = nullptr;

    Node::Node() : peerIntialized(false), InitializeMenu(false)
    {
        memset(peerMAC, 0xFF, sizeof(peerMAC));
        memset(selfMAC_, 0, sizeof(selfMAC_));

        outgoingQueue = xQueueCreate(10, sizeof(struct_message));
        if (outgoingQueue == nullptr) {
            Serial.println("Failed to create outgoing message queue");
        }

        mapMutex = xSemaphoreCreateMutex();
        if (mapMutex == nullptr) {
            Serial.println("Failed to create mutex for ackSemaphores");
        }

        // DO NOT call WiFi functions here - WiFi is not initialized yet!
        // setSelfMac will be called in begin() instead
        router_ = new Router();
        nodeService = new NodeService(this);
        messageHandler_ = new MessageHandler(router_, nodeService, this);
        // messageHandler_->setSelfMac will be called in begin() after WiFi is ready
        routeMode_ = RouteMode::AUTO;
    }

    Node::~Node()
    {
        if (outgoingQueue != nullptr)
        {
            vQueueDelete(outgoingQueue);
        }
        if (mapMutex != nullptr)
        {
            vSemaphoreDelete(mapMutex);
        }
        delete router_;
        delete messageHandler_;
    }

    void Node::begin()
    {
        printf("Begin Node Initialization\n");

        // Initialize MAC address now that WiFi is ready
        printf("[Node] Initializing self MAC address...\n");
        setSelfMac(selfMAC_);
        messageHandler_->setSelfMac(selfMAC_);
        printf("[Node] Self MAC address initialized\n");

        printf("[Node] Getting MacAddressStorage instance...\n");
        MacAddressStorage &macStorage = MacAddressStorage::getInstance();

        printf("[Node] Initializing MAC address storage...\n");
        if (!macStorage.init()) {
            printf("Warning: Failed to initialize MAC address storage\n");
        }
        printf("[Node] MAC storage initialized\n");

        activeInstance = this;
        printf("[Node] Setting WiFi mode to STA...\n");
        WiFi.mode(WIFI_STA);
        WiFi.disconnect();
        WiFi.setSleep(false);
        esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
        printf("%s\n", WiFi.macAddress().c_str());

        printf("[Node] Initializing ESP-NOW...\n");
        if (esp_now_init() != ESP_OK) {
            printf("ESP-NOW initialization failed.\n");
            return;
        }
        printf("[Node] ESP-NOW initialized\n");

        printf("[Node] Registering callbacks...\n");
        esp_now_register_send_cb(onDataSentCallback);
        esp_now_register_recv_cb(onDataRecvCallback);
        printf("[Node] Callbacks registered\n");

        printf("[Node] Creating outgoing queue task...\n");
        xTaskCreate(processOutgoingQueueTask, "OutgoingQueue", 4096, this, 1, NULL);
        printf("[Node] Node initialization complete\n");
    }

    void Node::probeDirect(struct_message msg) {
        nodeService->createPathWithMac(msg, peerMAC);
        for (int i = 0; i < probes; i++) {
            nodeService->setMessageID(msg, now_ms());
            sendDataNonBlocking(msg);
            Serial.println("Probe " + String(i) + " sent to: " + Router::pathToString(msg.path));
            vTaskDelay(pdMS_TO_TICKS(random(50, 60)));
        }
    }

    void Node::probeMesh() {
        struct_message msg;
        nodeService->buildProbeMessage(msg, peerMAC, selfMAC_);
        MacAddressStorage &macStorage = MacAddressStorage::getInstance();
        std::vector<String> macAddresses = macStorage.getAllMacAddresses();
        std::vector<uint8_t*> macAddressList = Router::stringToMacArray(macAddresses);
        pathCommandMap.clear();

        if (macAddresses.empty()) {
            Serial.println("No MAC addresses to probe");
            return;
        }

        probeDirect(msg);
        vTaskDelay(pdMS_TO_TICKS(100));

        for (uint8_t* mac : macAddressList) {
            if (memcmp(mac, selfMAC_, 6) == 0) { continue; }
            if (memcmp(mac, peerMAC, 6) == 0) { continue; }

            nodeService->createPathWithMac(msg, mac);

            for (int i = 0; i < probes; i++) {
                nodeService->setMessageID(msg, now_ms());
                sendDataNonBlocking(msg);
                Serial.println("Probe " + String(i) + " sent to: " + Router::pathToString(msg.path));
                vTaskDelay(pdMS_TO_TICKS(random(50, 60)));
            }
        }

        xTaskCreate(RelayEstablishedTask, "RelayEstablishedTask", 4096, this, 1, NULL);
    }

    void Node::RelayEstablished(uint8_t commandID, const char* path) {
        msec32 responseTime = now_ms() - commandID;
        String pathString = Router::pathToString(path);
        pathCommandMap[pathString].insert(responseTime);
        Serial.println("Relay established for path: " + pathString);
    }

    void Node::RelayEstablishedTask(void* pvParameters) {
        Node* node = static_cast<Node*>(pvParameters);
        if (!node) {
            Serial.println("Invalid node instance in RelayEstablishedTask");
            vTaskDelete(NULL); 
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(3000));

        // Convert map to vector for processing
        std::vector<std::pair<String, std::set<uint8_t>>> pathCommandVector;
        for (const auto& pair : node->pathCommandMap) {
            pathCommandVector.push_back(pair);
        }

        // Remove all that appear in the dataset at a lower frqeuncy then 0.8 to the probes ammount
        pathCommandVector.erase(std::remove_if(pathCommandVector.begin(), pathCommandVector.end(),
            [&](const std::pair<String, std::set<uint8_t>>& pair) {
                return pair.second.size() < 0.8 * node->probes;
            }), pathCommandVector.end());

        // Sort by response time (smallest set = most consistent)
        std::sort(pathCommandVector.begin(), pathCommandVector.end(),
            [](const std::pair<String, std::set<uint8_t>>& a, const std::pair<String, std::set<uint8_t>>& b) {
                return a.second.size() < b.second.size();
            });

        // Print results or do whatever processing you need
        Serial.printf("Found %d paths after filtering\n", pathCommandVector.size());

        //show all paths
        for (const auto& pair : pathCommandVector) {
            Serial.println("Path: " + pair.first);
            Serial.printf("Path stability: %d/%d responses\n",
                         pair.second.size(), node->probes);
        }

        Serial.println("--------------------------------------------------");

        if (!pathCommandVector.empty()) {
            Serial.println("Most stable path: " + pathCommandVector.back().first);
            Serial.printf("Path stability: %d/%d responses\n",
                         pathCommandVector.back().second.size(), node->probes);
        } else {
            Serial.println("No stable path found");
            Serial.printf("Total paths before filtering: %d\n", node->pathCommandMap.size());
            Serial.printf("Required stability threshold: %.1f responses\n", 0.8 * node->probes);
        }

        // Task completed, delete itself
        vTaskDelete(NULL);
    }

    bool Node::sendDataBlocking(const struct_message &msg, uint32_t timeout_ms, int maxRetries)
    {
        if (!peerIntialized) {
            Serial.println("Cannot send data: Peer not initialized");
            return false;
        }

        // Prepare message for sending
        struct_message messageToSend = msg;
        prepareMessage(messageToSend, true);

        // Create ACK semaphore
        SemaphoreHandle_t ackSemaphore = xSemaphoreCreateBinary();
        if (ackSemaphore == nullptr) {
            Serial.println("Failed to create semaphore for ack");
            return false;
        }

        // Register semaphore for ACK
        xSemaphoreTake(mapMutex, portMAX_DELAY);
        ackSemaphores[messageToSend.messageID] = ackSemaphore;
        xSemaphoreGive(mapMutex);

        // Send message
        bool success = false;
        if (xQueueSend(outgoingQueue, &messageToSend, pdMS_TO_TICKS(10)) == pdPASS) {
            success = (xSemaphoreTake(ackSemaphore, pdMS_TO_TICKS(timeout_ms)) == pdPASS);
        }

        // Cleanup
        xSemaphoreTake(mapMutex, portMAX_DELAY);
        ackSemaphores.erase(messageToSend.messageID);
        xSemaphoreGive(mapMutex);
        vSemaphoreDelete(ackSemaphore);

        return success;
    }

    bool Node::sendDataNonBlocking(const struct_message &msg)
    {
        if (!peerIntialized) {
            Serial.println("Cannot send data: Peer not initialized");
            return false;
        }

        struct_message messageToSend = msg;

        if (messageToSend.destinationMac == 0) {
            prepareMessage(messageToSend, false);
        }

        if (xQueueSend(outgoingQueue, &messageToSend, pdMS_TO_TICKS(10)) != pdPASS) {
            Serial.println("Failed to queue outgoing message (non-blocking)");
            return false;
        }

        return true;
    }
    

    bool Node::isPeerIntialized()
    {
        return peerIntialized;
    }

    Node *Node::getActiveInstance()
    {
        return activeInstance;
    }

    void Node::onDataSentCallback(const uint8_t *mac_addr, esp_now_send_status_t status)
    {
        if (activeInstance)
        {
            activeInstance->handleOnDataSent(mac_addr, status);
        }
    }

    void Node::onDataRecvCallback(const uint8_t *mac_addr, const uint8_t *incomingData, int len)
    {
        if (activeInstance)
        {
            activeInstance->handleOnDataRecv(mac_addr, incomingData, len);
        }
    }

    void Node::handleOnDataRecv(const uint8_t *mac_addr, const uint8_t *incomingData, int len) { 
        if (activeInstance)
        {
            if (len >= sizeof(struct_message)) {
                struct_message receivedMessage;
                memcpy(&receivedMessage, incomingData, sizeof(struct_message));

                receivedMessage.messageType[sizeof(receivedMessage.messageType) - 1] = '\0';
                receivedMessage.data[sizeof(receivedMessage.data) - 1] = '\0';
                receivedMessage.path[sizeof(receivedMessage.path) - 1] = '\0';

                if (!activeInstance->peerIntialized)
                {
                    memcpy(activeInstance->peerMAC, mac_addr, 6);
                    Router::printMac(mac_addr, "Stored peer MAC: ");
                    activeInstance->addPeer(mac_addr);
                }

                if (activeInstance->messageHandler_) {
                    activeInstance->messageHandler_->processReceivedMessage(mac_addr, receivedMessage);
                }
            }
        }
    }

    void Node::handleOnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
    {
        if (status != ESP_NOW_SEND_SUCCESS)
        {
            Router::printMac(mac_addr, "Failed to send data to: ");
        }
    }

    void Node::notifyAckReceived(uint32_t messageID)
    {
        xSemaphoreTake(mapMutex, portMAX_DELAY);
        auto it = ackSemaphores.find(messageID);
        if (it != ackSemaphores.end())
        {
            xSemaphoreGive(it->second);
        }
        xSemaphoreGive(mapMutex);
    }

    void Node::setSelfMac(uint8_t out[6])
    {
        String macStr = WiFi.macAddress();
        Router::stringToMac(macStr, out);
    }

    void Node::addPeer(const uint8_t *mac_addr)
    {
        if (!esp_now_is_peer_exist(mac_addr)) {
            esp_now_peer_info_t peerInfo = {};
            memcpy(peerInfo.peer_addr, mac_addr, 6);
            peerInfo.channel = 1;
            peerInfo.encrypt = false;

            esp_err_t result = esp_now_add_peer(&peerInfo);
            if (result == ESP_OK) {
                peerIntialized = true;
                Router::printMac(mac_addr, "Successfully added peer: ");
            } else {
                Router::printMac(mac_addr, "Failed to add peer: ");
                Serial.printf("Error: %s (%d)\n", esp_err_to_name(result), result);
            }
        } else {
            peerIntialized = true;
            Router::printMac(mac_addr, "Peer already exists: ");
        }
    }

    void Node::prepareMessage(struct_message &msg, bool isBlocking)
    {
        static uint32_t blockingCounter = 0;
        static uint32_t nonBlockingCounter = 0x80000000u;

        msg.messageID = isBlocking ? blockingCounter++ : nonBlockingCounter++;
        memcpy(msg.SenderMac, selfMAC_, 6);
        memset(msg.destinationMac, 0, sizeof(msg.destinationMac));
        msg.path[0] = '\0';
    }

    void Node::processOutgoingQueueTask(void *pvParameters)
    {
        Node *instance = static_cast<Node *>(pvParameters);
        struct_message message;

        for (;;)
        {
            if (xQueueReceive(instance->outgoingQueue, &message, portMAX_DELAY) == pdPASS)
            {
                uint8_t* sendTo = nullptr;

                if (!Router::checkValidMac(message.destinationMac)) {
                    sendTo = instance->peerMAC;
                } else {
                    uint8_t* pathMac = Router::getLastMacFromPath(message.path);
                    Router::printMac(pathMac, "Sending to: ");

                    if (pathMac) {
                        sendTo = pathMac;
                    } 
                }

                if (sendTo == nullptr) {
                    Serial.println("ERROR: SENDTO is null!");
                    continue;
                }

                if (!esp_now_is_peer_exist(sendTo)) {
                    Router::printMac(sendTo, "Peer not registered: ");
                    instance->addPeer(sendTo);

                    if (!esp_now_is_peer_exist(sendTo)) {
                        continue;
                    }
                }

                esp_err_t result = esp_now_send(sendTo, reinterpret_cast<uint8_t *>(&message), sizeof(message));

                if (result != ESP_OK)
                {
                    Serial.printf("Failed to send data via ESP-NOW: %s (%d)\n", esp_err_to_name(result), result);
                    Serial.printf("Message size: %d bytes\n", sizeof(message));
                } 
            }
        }
    }

} // namespace NuggetsInc
