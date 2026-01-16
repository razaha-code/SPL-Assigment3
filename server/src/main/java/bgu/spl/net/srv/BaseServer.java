package bgu.spl.net.srv;

import bgu.spl.net.api.MessageEncoderDecoder;
import bgu.spl.net.api.MessagingProtocol;
import bgu.spl.net.api.StompMessagingProtocol;
import bgu.spl.net.impl.stomp.ConnectionsImpl;
import java.io.IOException;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Supplier;

public abstract class BaseServer<T> implements Server<T> {

    private final int port;
    private final Supplier<MessagingProtocol<T>> protocolFactory;
    private final Supplier<MessageEncoderDecoder<T>> encdecFactory;
    private ServerSocket sock;
    
    // We added these fields to manage the shared state of the STOMP server
    private final ConnectionsImpl<T> connections;
    private final AtomicInteger connectionIdCounter;

    public BaseServer(
            int port,
            Supplier<MessagingProtocol<T>> protocolFactory,
            Supplier<MessageEncoderDecoder<T>> encdecFactory) {

        this.port = port;
        this.protocolFactory = protocolFactory;
        this.encdecFactory = encdecFactory;
        this.sock = null;
        
        // Initialize the shared Connections object and the ID counter
        this.connections = new ConnectionsImpl<>();
        this.connectionIdCounter = new AtomicInteger(0);
    }

    @Override
    public void serve() {

        try (ServerSocket serverSock = new ServerSocket(port)) {
            System.out.println("Server started");

            this.sock = serverSock; // just to be able to close

            while (!Thread.currentThread().isInterrupted()) {

                Socket clientSock = serverSock.accept();

                // 1. Create a new Protocol and EncoderDecoder instance for this specific client
                MessagingProtocol<T> protocol = protocolFactory.get();
                MessageEncoderDecoder<T> encdec = encdecFactory.get();

                // 2. Create the BlockingConnectionHandler (the runnable task)
                BlockingConnectionHandler<T> handler = new BlockingConnectionHandler<>(
                        clientSock,
                        encdec,
                        protocol);

                // 3. Generate a unique ID for this client
                int connectionId = connectionIdCounter.getAndIncrement();

                // 4. Initialize the STOMP protocol
                // This injects the client's ID and the shared Connections object into the protocol instance
                if (protocol instanceof StompMessagingProtocol) {
                    ((StompMessagingProtocol<T>) protocol).start(connectionId, connections);
                }

                // 5. Register the new handler in the Connections map
                // This allows the server to send messages to this client later using its ID
                connections.addConnection(connectionId, handler);

                // 6. Execute the handler (in TPC, this starts a new thread)
                execute(handler);
            }
        } catch (IOException ex) {
        }

        System.out.println("server closed!!!");
    }

    @Override
    public void close() throws IOException {
        if (sock != null)
            sock.close();
    }

    protected abstract void execute(BlockingConnectionHandler<T>  handler);

}