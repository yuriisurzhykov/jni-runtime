package jnirt;

// Stands in for android/src/main/kotlin/jnirt/NativeException.kt on the host: what the native side throws for a
// jnirt::Error. Same name, same constructor, same fields.
public class NativeException extends RuntimeException {
    public final String domain;
    public final int code;

    public NativeException(String domain, int code, String message, Throwable cause) {
        super(message, cause);
        this.domain = domain;
        this.code = code;
    }
}
