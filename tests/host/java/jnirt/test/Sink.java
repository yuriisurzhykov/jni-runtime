package jnirt.test;

// The Java listener of listener_test.cpp.
public interface Sink {
    void onText(String text);

    void onFrame(java.nio.ByteBuffer frame);
}
