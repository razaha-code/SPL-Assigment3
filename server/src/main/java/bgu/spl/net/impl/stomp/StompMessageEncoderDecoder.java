package bgu.spl.net.impl.stomp;

import bgu.spl.net.api.MessageEncoderDecoder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public class StompMessageEncoderDecoder implements MessageEncoderDecoder<String> {

    private byte[] bytes = new byte[1024];
    private int len = 0;

    @Override
    public String decodeNextByte(byte nextByte) {
        // In STOMP, the null character '\u0000' indicates the end of the frame
        if (nextByte == '\u0000') {
            return popString();
        }

        pushByte(nextByte);
        return null; // Not ready yet
    }

    @Override
    public byte[] encode(String message) {
        // Encodes the string to bytes and ensures it ends with the null character
        return (message + "\u0000").getBytes(StandardCharsets.UTF_8);
    }

    private void pushByte(byte nextByte) {
        if (len >= bytes.length) {
            bytes = Arrays.copyOf(bytes, len * 2);
        }
        bytes[len++] = nextByte;
    }

    private String popString() {
        String result = new String(bytes, 0, len, StandardCharsets.UTF_8);
        len = 0;
        return result;
    }
}