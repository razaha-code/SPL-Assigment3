#include "../include/StompProtocol.h"
#include <iostream>
#include <sstream>
#include <vector>

StompProtocol::StompProtocol() : 
    subscriptionIdCounter(0), 
    receiptIdCounter(0), 
    topicToSubId(), 
    receiptIdToAction(), 
    shouldTerminate(false), 
    isConnected(false) {}

bool StompProtocol::processKeyboardCommand(const std::string& line, ConnectionHandler& handler) {
    std::stringstream ss(line);
    std::string command;
    ss >> command;

    if (command == "login") {
        // פורמט: login {host} {port} {username} {password}
        // הערה: הטיפול בחיבור הפיזי נעשה ב-Main, כאן רק בונים את הפריים
        std::string host, portStr, username, password;
        ss >> host >> portStr >> username >> password;

        if (isConnected) {
            std::cout << "The client is already logged in, log out before trying again" << std::endl;
            return true; 
        }

        std::string frame = "CONNECT\n";
        frame += "accept-version:1.2\n";
        frame += "host:stomp.cs.bgu.ac.il\n";
        frame += "login:" + username + "\n";
        frame += "passcode:" + password + "\n";
        frame += "\n";
        frame += '\0';

        if (!handler.sendFrameAscii(frame, '\0')) return false;
        isConnected = true; 
        return true;
    }

    if (!isConnected) {
        std::cout << "Please login first" << std::endl;
        return true;
    }

    if (command == "join") {
        // join {game_name} -> SUBSCRIBE
        std::string gameName;
        ss >> gameName;

        int id = subscriptionIdCounter++;
        int receipt = receiptIdCounter++;
        
        topicToSubId[gameName] = id;
        receiptIdToAction[receipt] = "joined " + gameName;

        std::string frame = "SUBSCRIBE\n";
        frame += "destination:/" + gameName + "\n";
        frame += "id:" + std::to_string(id) + "\n";
        frame += "receipt:" + std::to_string(receipt) + "\n";
        frame += "\n";
        frame += '\0';

        handler.sendFrameAscii(frame, '\0');
    }
    else if (command == "exit") {
        // exit {game_name} -> UNSUBSCRIBE
        std::string gameName;
        ss >> gameName;

        if (topicToSubId.find(gameName) == topicToSubId.end()) {
            std::cout << "You are not subscribed to " << gameName << std::endl;
            return true;
        }

        int id = topicToSubId[gameName];
        int receipt = receiptIdCounter++;
        receiptIdToAction[receipt] = "exited " + gameName;

        std::string frame = "UNSUBSCRIBE\n";
        frame += "id:" + std::to_string(id) + "\n";
        frame += "receipt:" + std::to_string(receipt) + "\n";
        frame += "\n";
        frame += '\0';

        handler.sendFrameAscii(frame, '\0');
        // נמחק מהמפה רק כשנקבל אישור, או אופטימית עכשיו
        topicToSubId.erase(gameName);
    }
    else if (command == "add") {
        // add {game_name} {message} -> SEND
        std::string gameName, messageBody;
        ss >> gameName;
        std::getline(ss, messageBody); // קורא את שאר השורה
        
        if (!messageBody.empty() && messageBody[0] == ' ') {
            messageBody = messageBody.substr(1); // מחיקת רווח מיותר בהתחלה
        }

        std::string frame = "SEND\n";
        frame += "destination:/" + gameName + "\n";
        frame += "\n";
        frame += "user " + messageBody + "\n"; // הפורמט יכול להשתנות בהתאם לדרישות
        frame += '\0';

        handler.sendFrameAscii(frame, '\0');
    }
    else if (command == "logout") {
        // logout -> DISCONNECT
        int receipt = receiptIdCounter++;
        receiptIdToAction[receipt] = "disconnect";

        std::string frame = "DISCONNECT\n";
        frame += "receipt:" + std::to_string(receipt) + "\n";
        frame += "\n";
        frame += '\0';

        handler.sendFrameAscii(frame, '\0');
        // לא מתנתקים מיד! מחכים ל-RECEIPT
    }
    else if (command == "report") {
        // כאן תצטרך להוסיף את הלוגיקה של קריאת קובץ JSON
        // זה חלק גדול נפרד שמערב את ה-names_and_events שקיבלתם
        std::cout << "Report logic unimplemented in this snippet" << std::endl;
    }

    return true;
}

bool StompProtocol::processServerResponse(std::string& answer) {
    // 1. קודם כל מדפיסים הכל
    std::cout << answer << std::endl;

    // 2. בדיקה האם קיבלנו אישור לצאת
    std::stringstream ss(answer);
    std::string command;
    std::getline(ss, command); // קורא את השורה הראשונה (למשל RECEIPT)

    if (command == "RECEIPT") {
        std::string line;
        while(std::getline(ss, line) && line != "") {
            if (line.find("receipt-id:") == 0) {
                int id = std::stoi(line.substr(11));
                if (receiptIdToAction.count(id)) {
                    std::string action = receiptIdToAction[id];
                    if (action == "disconnect") {
                        isConnected = false;
                        return false; // סימן ל-Client לסגור את ה-SocketListener
                    }
                    // אפשר להדפיס הודעות משתמש יפות כמו "Joined game X"
                }
            }
        }
    }
    else if (command == "ERROR") {
        // במקרה של שגיאה השרת בדרך כלל סוגר את החיבור
        isConnected = false;
        return false;
    }
    
    return true;
}

bool StompProtocol::shouldLogout() {
    return !isConnected;
}

void StompProtocol::setTerminate(bool val) {
    shouldTerminate = val;
}