#include <stdlib.h>
#include "../include/ConnectionHandler.h"
#include "../include/StompProtocol.h"
#include <thread>
#include <iostream>
#include <mutex>
#include <atomic>

// משתנה בוליאני אטומי כדי לסנכרן בין התרדים מתי צריך לעצור
// (למשל: אם השרת סגר את החיבור, התרד של המקלדת צריך לדעת לעצור)
std::atomic<bool> isConnected(false);

/**
 * THREAD 2: Socket Listener
 * תפקידו: רק לקרוא מהרשת ולהדפיס למסך.
 * הוא רץ ברקע במקביל למקלדת.
 */
void socketListenerTask(ConnectionHandler* handler, StompProtocol* protocol) {
    while (isConnected) {
        std::string answer;
        
        // 1. קריאה חוסמת מהשרת (מחכה עד שתגיע הודעה או שהחיבור ייסגר)
        // ב-STOMP אנחנו קוראים עד ה-Null Character
        if (!handler->getFrameAscii(answer, '\0')) {
            std::cout << "Disconnected from server." << std::endl;
            isConnected = false;
            break;
        }

        // 2. עיבוד התשובה (הדפסה למסך + בדיקה אם זה RECEIPT של ניתוק)
        // הפונקציה הזו תחזיר false אם קיבלנו אישור להתנתק
        bool shouldContinue = protocol->processServerResponse(answer);
        
        if (!shouldContinue) {
            isConnected = false;
            break;
        }
    }
}

/**
 * THREAD 1: Main / Keyboard
 * תפקידו: לנהל את הלוגין, להפעיל את התרד השני, ולשלוח פקודות.
 */
int main(int argc, char *argv[]) {
    
    // לולאה שתאפשר למשתמש לנסות להתחבר מחדש אם נכשל או התנתק
    while (true) {
        const short bufsize = 1024;
        char buf[bufsize];

        // --- שלב 1: התחברות (לפני שמפעילים תרדים) ---
        // אנחנו בודקים שמחרוזת הכניסה היא login חוקי
        std::cout << "Please login (login {host} {port} {username} {password}):" << std::endl;
        std::cin.getline(buf, bufsize);
        std::string line(buf);
        
        // בדיקה בסיסית שזו פקודת login (אפשר לשפר פרסור כאן)
        if (line.find("login") != 0) {
            std::cout << "First command must be login" << std::endl;
            continue;
        }

        // חילוץ פרטים (הנחה פשוטה שהקלט תקין, כדאי לשפר)
        std::stringstream ss(line);
        std::string cmd, host, portStr, username, password;
        ss >> cmd >> host >> portStr >> username >> password;

        short port = std::stoi(portStr);
        
        ConnectionHandler connectionHandler(host, port);
        if (!connectionHandler.connect()) {
            std::cerr << "Cannot connect to " << host << ":" << port << std::endl;
            continue;
        }

        // ברגע שה-TCP הצליח, אנחנו מסמנים שאנחנו מחוברים
        isConnected = true;
        StompProtocol protocol;

        // שליחת הודעת ה-CONNECT הראשונית (בפרוטוקול STOMP)
        // אנחנו שולחים את זה ידנית מה-Main Thread לפני שמתחילים את הבלגן
        if (!protocol.processKeyboardCommand(line, connectionHandler)) {
            connectionHandler.close();
            continue;
        }

        // --- שלב 2: הפעלת ה-Thread השני (האזנה) ---
        // מעבירים לו פוינטר להנדלר ולפרוטוקול
        std::thread listenerThread(socketListenerTask, &connectionHandler, &protocol);

        // --- שלב 3: לולאת המקלדת (Thread ראשי) ---
        while (isConnected) {
            std::cin.getline(buf, bufsize);
            std::string commandLine(buf);

            // המרה מפקודת משתמש (join/add) לפקודת שרת (SUBSCRIBE/SEND) ושליחה
            bool success = protocol.processKeyboardCommand(commandLine, connectionHandler);
            
            // אם הייתה שגיאה קריטית בשליחה או שהמשתמש ביקש לצאת (והתהליך התחיל)
            if (!success) {
                // שים לב: אנחנו לא שוברים מיד! ב-logout אנחנו מחכים שהתרד השני יקבל RECEIPT
                // הפרוטוקול יודע לנהל את המצב הזה.
            }
        }

        // --- שלב 4: סגירה וניקוי ---
        // אם הגענו לפה, isConnected הפך ל-false (או ע"י השרת או ע"י ה-Receipt של הניתוק)
        
        connectionHandler.close(); // סגירת הסוקט תגרום לתרד השני להשתחרר מה-getFrameAscii
        
        if (listenerThread.joinable()) {
            listenerThread.join(); // מחכים שהתרד השני יסיים יפה
        }
        
        std::cout << "Client disconnected. Ready for new login." << std::endl;
    }
    return 0;
}