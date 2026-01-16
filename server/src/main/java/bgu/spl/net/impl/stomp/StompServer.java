package bgu.spl.net.impl.stomp;

import bgu.spl.net.srv.Server;

public class StompServer {

    public static void main(String[] args) {
        // Validating arguments based on the assignment instructions
        if (args.length < 2) {
            System.out.println("Usage: <port> <server-type>");
            System.exit(1);
        }

        int port = Integer.parseInt(args[0]); // The port to listen on
        String serverType = args[1];          // "tpc" or "reactor"

        if (serverType.equals("tpc")) {
            // Run Thread-Per-Client Server
            Server.threadPerClient(
                    port,
                    () -> new StompMessagingProtocolImpl(), // Protocol factory
                    () -> new StompMessageEncoderDecoder()  // Decoder factory
            ).serve();

        } else if (serverType.equals("reactor")) {
            // Run Reactor Server
            Server.reactor(
                    Runtime.getRuntime().availableProcessors(), // Number of threads
                    port,
                    () -> new StompMessagingProtocolImpl(), // Protocol factory
                    () -> new StompMessageEncoderDecoder()  // Decoder factory
            ).serve();

        } else {
            System.out.println("Unknown server type. Use 'tpc' or 'reactor'.");
        }
    }
}
