package jnirt.test;

// FindClass runs the static initializer: the class is found, but its initialization fails.
public class Boom {
    static {
        if (true) throw new IllegalStateException("no context");
    }

    public static void touch() {
    }
}
