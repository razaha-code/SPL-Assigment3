package bgu.spl.net.impl.stomp;

import bgu.spl.net.api.MessageEncoderDecoder;
import bgu.spl.net.api.StompMessagingProtocol;
import bgu.spl.net.srv.ConnectionHandler;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.IOException;
import java.net.Socket;

public class ConnectionHandlerImpl<T> implements ConnectionHandler<T>, Runnable {

    private final StompMessagingProtocol<T> protocol;
    private final MessageEncoderDecoder<T> encdec;
    private final Socket sock;
    private BufferedInputStream in;
    private BufferedOutputStream out;
    private volatile boolean connected = true;

    public ConnectionHandlerImpl(Socket sock, MessageEncoderDecoder<T> reader, StompMessagingProtocol<T> protocol) {
        this.sock = sock;
        this.encdec = reader;
        this.protocol = protocol;
    }

    // הפונקציה הזו מופעלת ע"י ה-Thread Per Client או ה-Reactor
    // היא אחראית על ה-INPUT (מהלקוח לשרת)
    @Override
    public void run() {
        try (Socket sock = this.sock) { // משאב שייסגר אוטומטית
            // 1. אתחול ה-Streams
            in = new BufferedInputStream(sock.getInputStream());
            out = new BufferedOutputStream(sock.getOutputStream());

            // 2. לולאת קריאה (Read Loop)
            int read;
            while (!protocol.shouldTerminate() && connected && (read = in.read()) >= 0) {
                // מנסים לפענח בייט אחד. אם חזר אובייקט שלם - הלקוח סיים לשלוח הודעה
                T nextMessage = encdec.decodeNextByte((byte) read);
                if (nextMessage != null) {
                    // מעבירים את ההודעה לפרוטוקול לעיבוד
                    protocol.process(nextMessage);
                }
            }
        } catch (IOException ex) {
            ex.printStackTrace();
        } finally {
            close();
        }
    }

    // הפונקציה הזו אחראית על ה-OUTPUT (מהשרת ללקוח)
    @Override
    public void send(T msg) {
        if (msg != null) {
            try {
                // 1. המרה מאובייקט לבתים
                byte[] encodedMsg = encdec.encode(msg);
                // 2. כתיבה ל-Socket
                out.write(encodedMsg);
                // 3. שליחה מיידית (חשוב מאוד!)
                out.flush();
            } catch (IOException e) {
                e.printStackTrace();
                // אם השליחה נכשלה, כנראה החיבור מת
                close();
            }
        }
    }

    @Override
    public void close() {
        try {
            connected = false;
            sock.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }
}