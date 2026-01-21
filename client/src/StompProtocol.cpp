    #include "../include/StompProtocol.h"
    #include "../include/event.h"
    #include <iostream>
    #include <sstream>
    #include <algorithm> 
    #include <fstream>
    #include <vector>
    #include <map>
    #include <mutex>

    static inline void trimCR(std::string& s) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
    }
        static bool isBeforeHalftime(const Event& e) {
        const auto& gu = e.get_game_updates();
        auto it = gu.find("before halftime");
        if (it == gu.end()) return true; // default סביר (אפשר לשנות אם רוצים)
        return it->second == "true";
    }


    static inline std::string hostOnlyFromHostPort(const std::string& hostPort) {
        size_t colon = hostPort.find(':');
        if (colon == std::string::npos) return hostPort;
        return hostPort.substr(0, colon);
    }

    StompProtocol::StompProtocol() : 
        subscriptionIdCounter(0), 
        receiptIdCounter(0), 
        topicToSubId(), 
        receiptIdToAction(), 
        shouldTerminate(false), 
        isConnected(false),
        activeUser(""),
        totalEvents() {}

    bool StompProtocol::processKeyboardCommand(const std::string& line, ConnectionHandler& handler) {
        std::lock_guard<std::mutex> lock(stateMutex);
        std::stringstream ss(line);
        
        std::string command;
        ss >> command;

        // --- LOGIN ---
        if (command == "login") {
            std::string hostPort, username, password;
            ss >> hostPort >> username >> password;

            if (isConnected) {
                std::cout << "The client is already logged in, log out before trying again" << std::endl;
                return true;
            }

            activeUser = username; // נשמר זמנית

            std::string hostHeader = hostOnlyFromHostPort(hostPort);

            std::string frame = "CONNECT\n";
            frame += "accept-version:1.2\n";
            frame += "host:stomp.cs.bgu.ac.il\n";
            frame += "login:" + username + "\n";
            frame += "passcode:" + password + "\n";
            frame += "\n";
            if (!handler.sendFrameAscii(frame, '\0')) return false;
            return true;
        }

        if (!isConnected) {
            std::cout << "Please login first" << std::endl;
            return true;
        }

        // --- JOIN ---
        if (command == "join") {
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

            handler.sendFrameAscii(frame, '\0');
        }

        // --- EXIT ---
        else if (command == "exit") {
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

            handler.sendFrameAscii(frame, '\0');
            topicToSubId.erase(gameName);

        }

        // --- REPORT ---
        else if (command == "report") {
            std::string file;
            ss >> file;
            
            names_and_events parsedData;
            try {
                parsedData = parseEventsFile(file);
            } catch (...) {
                std::cout << "Error parsing file: " << file << std::endl;
                return true;
            }

            std::string gameName = parsedData.team_a_name + "_" + parsedData.team_b_name;
           std::stable_sort(parsedData.events.begin(), parsedData.events.end(),
            [](const Event& a, const Event& b) {
                bool aBefore = isBeforeHalftime(a);
                bool bBefore = isBeforeHalftime(b);

                if (aBefore != bBefore) return aBefore;  // true (before halftime) קודם
                return a.get_time() < b.get_time();      // בתוך אותה מחצית לפי זמן
            });

            for (const Event& event : parsedData.events) {
                saveEvent(gameName, activeUser, event);

                std::string frame = "SEND\n";
                frame += "destination:/" + gameName + "\n";
                frame += "\n";
                frame += "user:" + activeUser + "\n";
                frame += "team a:" + parsedData.team_a_name + "\n";
                frame += "team b:" + parsedData.team_b_name + "\n";
                frame += "event name:" + event.get_name() + "\n";
                frame += "time:" + std::to_string(event.get_time()) + "\n";
                frame += "general game updates:\n";
                for (auto const& [key, val] : event.get_game_updates()) {
                    frame += key + ":" + val + "\n";
                }
                frame += "team a updates:\n";
                for (auto const& [key, val] : event.get_team_a_updates()) {
                    frame += key + ":" + val + "\n";
                }
                frame += "team b updates:\n";
                for (auto const& [key, val] : event.get_team_b_updates()) {
                    frame += key + ":" + val + "\n";
                }
                frame += "description:\n" + event.get_discription() + "\n";

                handler.sendFrameAscii(frame, '\0');
            }
        }

        // --- SUMMARY ---
        else if (command == "summary") {
            std::string gameName, user, file;
            ss >> gameName >> user >> file;

            // בדיקה שהמשחק והמשתמש קיימים בזיכרון
            if (totalEvents.find(gameName) == totalEvents.end() || 
                totalEvents[gameName].find(user) == totalEvents[gameName].end()) {
                std::cout << "No events found for user " << user << " in game " << gameName << std::endl;
                return true;
            }

            const std::vector<Event>& events = totalEvents[gameName][user];
            
            std::ofstream outFile(file);
            if (!outFile.is_open()) {
                std::cout << "Error opening file: " << file << std::endl;
                return true;
            }

            // הגדרת מפות לסטטיסטיקות - std::map דואג למיון אלפביתי אוטומטי
            std::map<std::string, std::string> generalStats;
            std::map<std::string, std::string> teamAStats;
            std::map<std::string, std::string> teamBStats;
            
            // חילוץ שמות הקבוצות (מניחים שיש לפחות אירוע אחד)
            if (events.empty()) {
                outFile.close();
                return true;
            }

            std::string teamA = events[0].get_team_a_name();
            std::string teamB = events[0].get_team_b_name();

            for (const auto& ev : events) {
                for (auto const& [key, val] : ev.get_game_updates()) generalStats[key] = val;
                for (auto const& [key, val] : ev.get_team_a_updates()) teamAStats[key] = val;
                for (auto const& [key, val] : ev.get_team_b_updates()) teamBStats[key] = val;
            }

            // 2. הדפסת הכותרות והסטטיסטיקות (יודפסו בסדר ABC)
            outFile << teamA << " vs " << teamB << "\n";
            outFile << "Game stats:\n";
            
            outFile << "General stats:\n";
            for (auto const& [key, val] : generalStats) outFile << key << ": " << val << "\n";
            
            outFile << teamA << " stats:\n";
            for (auto const& [key, val] : teamAStats) outFile << key << ": " << val << "\n";
            
            outFile << teamB << " stats:\n";
            for (auto const& [key, val] : teamBStats) outFile << key << ": " << val << "\n";
            
            // 3. הדפסת האירועים עצמם (יודפסו בסדר כרונולוגי כי events ממויין)
            outFile << "Game event reports:\n";
            for (const auto& ev : events) {
                // פורמט ההדפסה לפי הדוגמה במטלה
                outFile << ev.get_time() << " - " << ev.get_name() << ":\n\n";
                outFile << ev.get_discription() << "\n\n\n";
            }

            outFile.close();
            std::cout << "Summary created in " << file << std::endl;
        }

        // --- LOGOUT ---
        else if (command == "logout") {
            int receipt = receiptIdCounter++;
            receiptIdToAction[receipt] = "disconnect";

            std::string frame = "DISCONNECT\n";
            frame += "receipt:" + std::to_string(receipt) + "\n";
            frame += "\n";

            handler.sendFrameAscii(frame, '\0');
        }

        return true;
    }

    bool StompProtocol::processServerResponse(std::string& answer) {
        std::lock_guard<std::mutex> lock(stateMutex);
        
        std::stringstream ss(answer);
        std::string command;
        std::getline(ss, command);
        trimCR(command);



        // --- CONNECTED ---
        if (command == "CONNECTED") {
            std::cout << "Login successful" << std::endl;
            isConnected = true;
        }

        // --- ERROR ---
        else if (command == "ERROR") {
            std::cout << answer << std::endl; 
            isConnected = false;
            activeUser = "";
            return false;
        }

        // --- RECEIPT ---
        else if (command == "RECEIPT") {
            std::string line;
            while(std::getline(ss, line) && line != "") {
                trimCR(line);

                if (line.find("receipt-id:") == 0) {
                    int id = std::stoi(line.substr(11));
                    if (receiptIdToAction.count(id)) {
                        std::string action = receiptIdToAction[id];
                        receiptIdToAction.erase(id);
                    
                        if (action == "disconnect") {
                            isConnected = false;
                            activeUser = "";
                            return false;
                        }
                        else if (action.find("joined") == 0) {
                            std::cout << "Joined channel " << action.substr(7) << std::endl;
                        }
                        else if (action.find("exited") == 0) {
                            std::cout << "Exited channel " << action.substr(7) << std::endl;
                            std::string gameName = action.substr(7);
                        // topicToSubId.erase(gameName);

                        }
                    }
                }
            }
        }

        // --- MESSAGE ---
        else if (command == "MESSAGE") {
            std::string destination, body, line;
            
            // Read Headers
            while (std::getline(ss, line) && line != "") {

                trimCR(line);
                if (line.find("destination:") == 0) {
                    destination = line.substr(12);
                }
            }

            // Read Body
            std::string currentLine;
            while (std::getline(ss, currentLine)) {
                body += currentLine + "\n";
            }

            if (!destination.empty()) {
                std::string gameName = destination.substr(1); // Remove leading '/'
                
                Event event = parseEventBody(body); 
                
                // Extract username from body (usually first line: "user:name")
                std::stringstream bodySS(body);
                std::string keyVal, sender;
                std::getline(bodySS, keyVal); 
                if (keyVal.find("user:") == 0) sender = keyVal.substr(5);

                if (!sender.empty()) {
                    saveEvent(gameName, sender, event);
                }
            }
            // Print message to screen
            std::cout << command << "\n" << answer << std::endl;
        }

        return true;
    }

    void StompProtocol::saveEvent(const std::string& gameName, const std::string& sender, const Event& event) {
    auto& eventsVector = totalEvents[gameName][sender];
    eventsVector.push_back(event);

    // לשמור את ההיסטוריה תמיד ממויינת (כדי ש-summary יהיה נכון)
    std::stable_sort(eventsVector.begin(), eventsVector.end(),
        [](const Event& a, const Event& b) {
            bool aBefore = isBeforeHalftime(a);
            bool bBefore = isBeforeHalftime(b);

            if (aBefore != bBefore) return aBefore;   // לפני מחצית קודם
            return a.get_time() < b.get_time();       // ואז לפי זמן
        });
}

    Event StompProtocol::parseEventBody(const std::string& body) {
        std::stringstream ss(body);
        std::string line;
        std::string teamA, teamB, eventName, description;
        int time = 0;
        std::map<std::string, std::string> gameUpdates, teamAUpdates, teamBUpdates;
        
        // 0=headers, 1=general, 2=teamA, 3=teamB, 4=desc
        int parsingState = 0; 

        while (std::getline(ss, line)) {
            if (line == "general game updates:") { parsingState = 1; continue; }
            if (line == "team a updates:") { parsingState = 2; continue; }
            if (line == "team b updates:") { parsingState = 3; continue; }
            if (line == "description:") { parsingState = 4; continue; }

            if (parsingState == 0) {
                if (line.find("team a:") == 0) teamA = line.substr(7);
                else if (line.find("team b:") == 0) teamB = line.substr(7);
                else if (line.find("event name:") == 0) eventName = line.substr(11);
                else if (line.find("time:") == 0) time = std::stoi(line.substr(5));
            }
            else if (parsingState == 4) {
                description += line + "\n";
            }
            else {
                size_t colonPos = line.find(':');
                if (colonPos != std::string::npos) {
                    std::string key = line.substr(0, colonPos);
                    std::string val = line.substr(colonPos + 1);
                    if (parsingState == 1) gameUpdates[key] = val;
                    else if (parsingState == 2) teamAUpdates[key] = val;
                    else if (parsingState == 3) teamBUpdates[key] = val;
                }
            }
        }
        return Event(teamA, teamB, eventName, time, gameUpdates, teamAUpdates, teamBUpdates, description);
    }

    bool StompProtocol::shouldLogout() {
        return !isConnected;
    }

    void StompProtocol::setTerminate(bool val) {
        shouldTerminate = val;
    }