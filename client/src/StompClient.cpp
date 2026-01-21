#include <stdlib.h>
#include "../include/ConnectionHandler.h"
#include "../include/StompProtocol.h"
#include <thread>
#include <iostream>
#include <mutex>
#include <atomic>
#include <sstream>

// דגל שמייצג "TCP socket פתוח ורצים"
// לא לבלבל עם isConnected בתוך StompProtocol (שהוא STOMP logged-in)
std::atomic<bool> tcpRunning(false);

static bool parseHostPort(const std::string& hostPort, std::string& hostOut, short& portOut) {
    size_t colon = hostPort.find(':');
    if (colon == std::string::npos) return false;
    hostOut = hostPort.substr(0, colon);
    std::string portStr = hostPort.substr(colon + 1);
    try {
        int p = std::stoi(portStr);
        if (p < 1 || p > 65535) return false;
        portOut = static_cast<short>(p);
    } catch (...) {
        return false;
    }
    return true;
}

void socketListenerTask(ConnectionHandler* handler, StompProtocol* protocol) {
    while (tcpRunning) {
        std::string answer;

        if (!handler->getFrameAscii(answer, '\0')) {
            std::cout << "Disconnected from server." << std::endl;
            tcpRunning = false;
            break;
        }

        bool shouldContinue = protocol->processServerResponse(answer);
        if (!shouldContinue) {
            tcpRunning = false;
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    while (true) {
        const short bufsize = 1024;
        char buf[bufsize];

        std::cout << "Please login (login {host:port} {username} {password}):" << std::endl;
        std::cin.getline(buf, bufsize);
        std::string line(buf);

        if (line.find("login") != 0) {
            std::cout << "First command must be login" << std::endl;
            continue;
        }

        std::stringstream ss(line);
        std::string cmd, hostPort, username, password;
        ss >> cmd >> hostPort >> username >> password;

        std::string host;
        short port = 0;
        if (!parseHostPort(hostPort, host, port)) {
            std::cout << "Invalid login format. Use: login {host:port} {username} {password}" << std::endl;
            continue;
        }

        ConnectionHandler connectionHandler(host, port);
        if (!connectionHandler.connect()) {
            std::cerr << "Could not connect to server" << std::endl;
            continue;
        }

        tcpRunning = true;
        StompProtocol protocol;

        // שולח CONNECT לפי הפרוטוקול (הוא יידע לפרק host:port)
        if (!protocol.processKeyboardCommand(line, connectionHandler)) {
            connectionHandler.close();
            tcpRunning = false;
            continue;
        }

        std::thread listenerThread(socketListenerTask, &connectionHandler, &protocol);

        while (tcpRunning) {
            std::cin.getline(buf, bufsize);
            std::string commandLine(buf);

            bool success = protocol.processKeyboardCommand(commandLine, connectionHandler);
            (void)success; // לא חייבים לשבור פה; logout נשלט ע"י RECEIPT בצד השני
        }

        connectionHandler.close();
        if (listenerThread.joinable())
            listenerThread.join();

        std::cout << "Client disconnected. Ready for new login." << std::endl;
    }
    return 0;
}
