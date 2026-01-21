package bgu.spl.net.impl.stomp;

import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import bgu.spl.net.srv.ConnectionHandler;
import bgu.spl.net.srv.Connections;

public class ConnectionsImpl<T> implements Connections<T> {

    private final ConcurrentHashMap<Integer, ConnectionHandler<T>> activeConnections;
    private final ConcurrentHashMap<String, ConcurrentHashMap<Integer, Integer>> channelSubscribers;

    public ConnectionsImpl() {
        this.activeConnections = new ConcurrentHashMap<>();
        this.channelSubscribers = new ConcurrentHashMap<>();
    }

    @Override
    public boolean send(int connectionId, T msg) {
        ConnectionHandler<T> handler = activeConnections.get(connectionId);
        if (handler == null) {
            return false;
        }
        try {
            handler.send(msg);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    @Override
    public void send(String channel, T msg) {
        ConcurrentHashMap<Integer, Integer> subscribers = channelSubscribers.get(channel);

        if (subscribers != null) {
            for (Map.Entry<Integer, Integer> entry : subscribers.entrySet()) {
                Integer connectionId = entry.getKey();
                Integer subscriptionId = entry.getValue();

                String messageAsString = (String) msg;
                String finalFrame;

                // Find the separation between headers and body
                int bodySeparator = messageAsString.indexOf("\n\n");

                if (bodySeparator != -1) {
                    // Inject the subscription header before the blank line
                    finalFrame = messageAsString.substring(0, bodySeparator) + 
                                 "\nsubscription:" + subscriptionId + 
                                 messageAsString.substring(bodySeparator) + 
                                 "\u0000";
                } else {
                    // Case where there is no body yet, just headers
                    finalFrame = messageAsString + 
                                 "\nsubscription:" + subscriptionId + 
                                 "\n\n\u0000";
                }

                send(connectionId, (T) finalFrame);
            }
        }
    }

    @Override
    public void disconnect(int connectionId) {
        activeConnections.remove(connectionId);
        
        for (ConcurrentHashMap<Integer, Integer> subscribers : channelSubscribers.values()) {
            subscribers.remove(connectionId);
        }
    }
    
    public void addConnection(int connectionId, ConnectionHandler<T> handler) {
        activeConnections.put(connectionId, handler);
    }

    public void subscribe(String channel, int connectionId, int subscriptionId) {
        channelSubscribers.computeIfAbsent(channel, k -> new ConcurrentHashMap<>())
                          .put(connectionId, subscriptionId);
    }
    
    public void unsubscribe(String channel, int connectionId) {
         if (channelSubscribers.containsKey(channel)) {
             channelSubscribers.get(channel).remove(connectionId);
         }
    }
}