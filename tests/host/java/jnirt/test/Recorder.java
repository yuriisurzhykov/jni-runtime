package jnirt.test;

import java.util.concurrent.atomic.AtomicInteger;

// A Sink that counts what it gets. With failOnText it throws after counting.
public class Recorder implements Sink {
    private final boolean failOnText;
    private final AtomicInteger texts = new AtomicInteger();
    private final AtomicInteger frames = new AtomicInteger();
    private volatile int lastSize = -1;
    private volatile int lastFirst = -1;

    public Recorder(boolean failOnText) {
        this.failOnText = failOnText;
    }

    @Override
    public void onText(String text) {
        texts.incrementAndGet();
        if (failOnText) throw new IllegalStateException("listener failed on " + text);
    }

    @Override
    public void onFrame(java.nio.ByteBuffer frame) {
        frames.incrementAndGet();
        lastSize = frame.remaining();
        lastFirst = frame.get(0);
    }

    public int texts() {
        return texts.get();
    }

    public int frames() {
        return frames.get();
    }

    public int lastSize() {
        return lastSize;
    }

    public int lastFirst() {
        return lastFirst;
    }
}
