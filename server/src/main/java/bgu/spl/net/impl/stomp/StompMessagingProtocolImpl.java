package bgu.spl.net.impl.stomp;

import bgu.spl.net.api.StompMessagingProtocol;
import bgu.spl.net.srv.Connections;
import java.util.HashMap;
import java.util.Map;
// import java.util.concurrent.ConcurrentHashMap;

import bgu.spl.net.impl.data.Database;
import bgu.spl.net.impl.data.LoginStatus;

public class StompMessagingProtocolImpl implements StompMessagingProtocol<String> {

    private int connectionId;
    private Connections<String> connections;
    private boolean shouldTerminate = false;
    private String currentUsername = null;
    
    // Map to track subscriptions for THIS client: SubscriptionID -> ChannelName
    // This helps us when the client sends UNSUBSCRIBE id:X (we need to know which channel X belonged to)
    private Map<String, String> mySubscriptions = new HashMap<>();

    @Override
    public void start(int connectionId, Connections<String> connections) {
        this.connectionId = connectionId;
        this.connections = connections;
    }

    @Override
    public String process(String message) {
        String[] lines = message.split("\n");
        if (lines.length == 0) return null;

        String command = lines[0];
        Map<String, String> headers = parseHeaders(lines);
        String body = parseBody(message);

        if (command.equals("CONNECT")) {
            handleConnect(headers);
        } else if (command.equals("SUBSCRIBE")) {
            handleSubscribe(headers);
        } else if (command.equals("UNSUBSCRIBE")) {
            handleUnsubscribe(headers);
        } else if (command.equals("SEND")) {
            handleSend(headers, body);
        } else if (command.equals("DISCONNECT")) {
            handleDisconnect(headers);
        } else {
            sendError(headers, "Unknown Command", "Command not supported");
        }

        // Receipt Handling
        if (headers.containsKey("receipt")) {
            connections.send(connectionId, "RECEIPT\nreceipt-id:" + headers.get("receipt") + "\n\n\u0000");
            if (command.equals("DISCONNECT")) {
                shouldTerminate = true;
                connections.disconnect(connectionId);
            }
        }

        return null;
    }

    @Override
    public boolean shouldTerminate() {
        return shouldTerminate;
    }

    // --- Handlers ---

    private void handleConnect(Map<String, String> headers) {
        String login = headers.get("login");
        String passcode = headers.get("passcode");

        if (login == null || passcode == null) {
            sendError(headers, "Malformed Frame", "Missing login or passcode header");
            return;
        }

        // Delegate the login logic to the Database singleton
        // This handles checking the password, creating new users, and locking the session
        LoginStatus status = Database.getInstance().login(connectionId, login, passcode);

        if (status == LoginStatus.LOGGED_IN_SUCCESSFULLY || status == LoginStatus.ADDED_NEW_USER) {
            // 1. Success Case
            this.currentUsername = login;
            
            String response = "CONNECTED\n" +
                            "version:1.2\n" +
                            "\n" + 
                            "\u0000";
            connections.send(connectionId, response);
            
        } else if (status == LoginStatus.WRONG_PASSWORD) {
            // 2. Wrong Password
            sendError(headers, "Bad Credentials", "Password does not match");
            
        } else if (status == LoginStatus.ALREADY_LOGGED_IN) {
            // 3. User is already active on another client
            sendError(headers, "User already logged in", "User " + login + " is already active");
            
        } else if (status == LoginStatus.CLIENT_ALREADY_CONNECTED) {
            // 4. This specific client connection is already authenticated
            sendError(headers, "Client already connected", "You are already logged in on this connection");
        }
    }

    private void handleSubscribe(Map<String, String> headers) {
        String destination = headers.get("destination");
        String id = headers.get("id");

        if (destination == null || id == null) {
            sendError(headers, "Malformed Frame", "Missing destination or id");
            return;
        }

        // 1. Save locally to track this client's subscriptions
        mySubscriptions.put(id, destination);

        // 2. Register in the ConnectionsImpl (Cast needed)
        ((ConnectionsImpl<String>) connections).subscribe(destination, connectionId, Integer.parseInt(id));
    }

    private void handleUnsubscribe(Map<String, String> headers) {
        String id = headers.get("id");
        if (id == null) {
            sendError(headers, "Malformed Frame", "Missing id");
            return;
        }

        // Find which channel this ID belongs to
        String channel = mySubscriptions.remove(id);
        if (channel != null) {
            ((ConnectionsImpl<String>) connections).unsubscribe(channel, connectionId);
        }
    }

    private void handleSend(Map<String, String> headers, String body) {
        String destination = headers.get("destination");
        
        // 1. Basic validation
        if (destination == null) {
            sendError(headers, "Malformed Frame", "Missing destination header");
            return;
        }

        // 2. Database Integration: File Tracking
        // We look for the extra header "file-name" that the Client should send.
        // If it exists, it means this message is part of a file report.
        String filename = headers.get("file-name"); 
        
        if (filename != null && currentUsername != null) {
            // We delegate the saving to the Database class.
            // Note: The Database class will execute "INSERT INTO file_tracking ..."
            Database.getInstance().trackFileUpload(currentUsername, filename, destination);
        }

        // 3. Broadcast the message to all subscribers
        // The server adds a message-id and sends it to everyone subscribed to this topic.
        String messageFrame = "MESSAGE\n" + 
                            "subscription:0\n" + // In a real impl, you'd map subId to user
                            "message-id:" + java.util.UUID.randomUUID().toString() + "\n" +
                            "destination:" + destination + "\n" +
                            "\n" + 
                            body + "\u0000";

        connections.send(destination, messageFrame);
    }

    private void handleDisconnect(Map<String, String> headers) {
        String receiptId = headers.get("receipt");
        if (receiptId != null) {
            connections.send(connectionId, "RECEIPT\nreceipt-id:" + receiptId + "\n\n\u0000");
        }

        // IMPORTANT: Update the database to record the logout timestamp
        Database.getInstance().logout(connectionId);
        
        this.currentUsername = null;
        this.shouldTerminate = true;
        connections.disconnect(connectionId);
    }

    private void sendError(Map<String, String> headers, String errMsg, String desc) {
        String frame = "ERROR\nmessage:" + errMsg + "\n\n" + desc + "\n\u0000";
        connections.send(connectionId, frame);
        shouldTerminate = true;
        connections.disconnect(connectionId);
    }

    // --- Parsers ---
    
    private Map<String, String> parseHeaders(String[] lines) {
        Map<String, String> h = new HashMap<>();
        for (int i = 1; i < lines.length; i++) {
            if (lines[i].isEmpty()) break;
            String[] parts = lines[i].split(":", 2);
            if (parts.length == 2) h.put(parts[0], parts[1]);
        }
        return h;
    }

    private String parseBody(String msg) {
        int idx = msg.indexOf("\n\n");
        return (idx == -1) ? "" : msg.substring(idx + 2);
    }
}