#pragma once
#include "../include/ConnectionHandler.h"
#include "../include/event.h"
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <mutex>


// TODO: ensure you have json.hpp or the event parser included correctly in event.h

class StompProtocol {
private:
    std::mutex stateMutex;

    int subscriptionIdCounter;
    int receiptIdCounter;
    
    // Maps for internal state management
    std::map<std::string, int> topicToSubId; // GameName -> SubID
    std::map<int, std::string> receiptIdToAction; // ReceiptID -> Action Description

    bool shouldTerminate;
    bool isConnected;
    
    std::string activeUser; 
    
    // Data structure for history: Game Name -> User Name -> List of Events
    std::map<std::string, std::map<std::string, std::vector<Event>>> totalEvents;

public:
    StompProtocol();
    bool processKeyboardCommand(const std::string& line, ConnectionHandler& handler);
    bool processServerResponse(std::string& answer);
    bool shouldLogout();
    void setTerminate(bool val);

    // Internal helper functions
private:
    void saveEvent(const std::string& gameName, const std::string& sender, const Event& event);
    Event parseEventBody(const std::string& body);
};