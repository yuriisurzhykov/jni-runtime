package jnirt;

import java.lang.ref.Cleaner;

// Stands in for android/src/main/kotlin/jnirt/NativeBuffers.kt on the host: closes the handle of the native memory
// when the ByteBuffer is collected.
public class NativeBuffers {
    private static final Cleaner cleaner = Cleaner.create();

    public static void watch(Object buffer, long handle) {
        cleaner.register(buffer, new Release(handle));
    }

    private static final class Release implements Runnable {
        private final long handle;

        Release(long handle) {
            this.handle = handle;
        }

        @Override
        public void run() {
            NativePeerNatives.close(handle);
        }
    }
}
