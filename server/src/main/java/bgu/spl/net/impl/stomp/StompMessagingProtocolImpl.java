package bgu.spl.net.impl.stomp;

import bgu.spl.net.api.StompMessagingProtocol;
import bgu.spl.net.srv.Connections;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

public class StompMessagingProtocolImpl implements StompMessagingProtocol<String> {

    private int connectionId;
    private Connections<String> connections;
    private boolean shouldTerminate = false;
    
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
            sendError(headers, "Malformed Frame", "Missing login or passcode");
            return;
        }

        UserDatabase db = UserDatabase.getInstance();
        
        // Registration / Login logic
        if (!db.isUserExists(login)) {
            db.register(login, passcode);
        } else if (!db.validatePassword(login, passcode)) {
            sendError(headers, "Login Failed", "Wrong password");
            return;
        }

        if (!db.login(login)) {
            sendError(headers, "Login Failed", "User already logged in");
            return;
        }

        connections.send(connectionId, "CONNECTED\nversion:1.2\n\n\u0000");
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
        if (destination == null) {
            sendError(headers, "Malformed Frame", "Missing destination");
            return;
        }

        // We prepare the message WITHOUT the subscription-id header and WITHOUT \u0000
        // ConnectionsImpl will add them for each client
        String frameBase = "MESSAGE\n" +
                           "destination:" + destination + "\n" +
                           "message-id:" + System.currentTimeMillis() + "\n" +
                           "\n" + // Empty line before body
                           body;  // Note: NO \u0000 at the end here, ConnectionsImpl adds it!
        
        connections.send(destination, frameBase);
    }

    private void handleDisconnect(Map<String, String> headers) {
        // Just cleanup, the actual disconnect happens after sending Receipt
        // You might want to remove user from UserDatabase (logout) here
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