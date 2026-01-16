package bgu.spl.net.impl.stomp;

import java.util.concurrent.ConcurrentHashMap;

public class UserDatabase {

    private static class UserDatabaseHolder {
        private static final UserDatabase instance = new UserDatabase();
    }

    private final ConcurrentHashMap<String, String> users = new ConcurrentHashMap<>();
    private final ConcurrentHashMap<String, Boolean> activeLogins = new ConcurrentHashMap<>();

    // Private constructor to enforce Singleton
    private UserDatabase() {}

    public static UserDatabase getInstance() {
        return UserDatabaseHolder.instance;
    }

    public void register(String username, String password) {
        users.putIfAbsent(username, password);
    }

    public boolean isUserExists(String username) {
        return users.containsKey(username);
    }

    public boolean validatePassword(String username, String password) {
        String storedPassword = users.get(username);
        return storedPassword != null && storedPassword.equals(password);
    }

    public boolean login(String username) {
        return activeLogins.putIfAbsent(username, true) == null;
    }

    public void logout(String username) {
        activeLogins.remove(username);
    }
}