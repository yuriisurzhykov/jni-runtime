package jnirt.test;

import java.nio.ByteBuffer;
import java.nio.file.Files;
import java.nio.file.Paths;

// The driver of frame_memory_bench.cpp: frames that are made and dropped at once (what Delivery.postLatest does to the ones
// it replaces), and how much native memory is alive meanwhile.
public class FrameMemory {
    public static native ByteBuffer frame(int size);

    public static native long liveBytes();

    public static native long forcedCollections();

    private static long rssMiB() {
        try {
            for (String line : Files.readAllLines(Paths.get("/proc/self/status"))) {
                if (line.startsWith("VmRSS:")) return Long.parseLong(line.replaceAll("[^0-9]", "")) / 1024;
            }
        } catch (Exception e) {
            // not Linux
        }
        return -1;
    }

    public static String run(int frames, int size, int pauseMicros) throws InterruptedException {
        long peak = 0;
        for (int i = 0; i < frames; i++) {
            ByteBuffer buffer = frame(size);
            buffer.put(0, (byte) i); // touch it, as a consumer would
            if (i % 20 == 0) peak = Math.max(peak, liveBytes());
            if (pauseMicros > 0) Thread.sleep(pauseMicros / 1000, (pauseMicros % 1000) * 1000);
        }
        peak = Math.max(peak, liveBytes());
        return String.format("frames=%d size=%d KiB  peak live native memory=%d MiB  rss=%d MiB  forced collections=%d%n",
                frames, size / 1024, peak / (1024 * 1024), rssMiB(), forcedCollections());
    }
}
