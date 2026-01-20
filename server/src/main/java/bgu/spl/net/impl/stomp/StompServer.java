package bgu.spl.net.impl.stomp;

import bgu.spl.net.impl.data.Database;
import bgu.spl.net.srv.Server;
import java.util.Scanner;

public class StompServer {

    public static void main(String[] args) {
        // Validating arguments based on the assignment instructions
        if (args.length < 2) {
            System.out.println("Usage: <port> <server-type>");
            System.exit(1);
        }

        int port = Integer.parseInt(args[0]); // The port to listen on
        String serverType = args[1];          // "tpc" or "reactor"

        // --- KEYBOARD LISTENER THREAD ---
        // This runs in parallel to the server to handle the "report" command
        new Thread(() -> {
            Scanner scanner = new Scanner(System.in);
            System.out.println("Server started. Type 'report' to see database stats, or 'exit' to stop.");
            
            while (scanner.hasNextLine()) {
                String line = scanner.nextLine().trim();
                if (line.equalsIgnoreCase("report")) {
                    // Prints the SQL report using the existing Database logic
                    Database.getInstance().printReport();
                } else if (line.equalsIgnoreCase("exit")) {
                    System.out.println("Shutting down...");
                    System.exit(0);
                }
            }
        }).start();

        // --- START SERVER ---
        if (serverType.equals("tpc")) {
            Server.threadPerClient(
                    port,
                    () -> new StompMessagingProtocolImpl(), // Protocol factory
                    StompMessageEncoderDecoder::new  // Decoder factory
            ).serve();

        } else if (serverType.equals("reactor")) {
            Server.reactor(
                    Runtime.getRuntime().availableProcessors(), // Number of threads
                    port,
                    () -> new StompMessagingProtocolImpl(), // Protocol factory
                    StompMessageEncoderDecoder::new  // Decoder factory
            ).serve();

        } else {
            System.out.println("Unknown server type. Use 'tpc' or 'reactor'.");
        }
    }
}