package jnirt.test;

import java.nio.ByteBuffer;

// The class the array tests (convert_array_test.cpp) describe. The natives are registered by the tests with C++ types;
// the other members are called from native code through class_cache.
public class ArrayConverter {
    // ---- copies: std::vector <-> array ----
    public native int[] doubled(int[] values);

    public native byte[] echoBytes(byte[] values);

    public native byte[] echoSignedBytes(byte[] values);

    public native boolean[] flip(boolean[] flags);

    public native char[] echoChars(char[] values);

    public native short[] echoShorts(short[] values);

    public native long[] echoLongs(long[] values);

    public native float[] echoFloats(float[] values);

    public native double[] echoDoubles(double[] values);

    public native int[] echoOptInts(int[] values);

    // ---- pieces: readArray / writeArray ----
    public native int[] slice(int[] values, int start, int count);

    public native void fillPiece(int[] target, int start, int count, int value);

    public native void fillBytes(byte[] target, int value);

    public native int[] sliceOutside(int[] values);

    public native int lengthOf(int[] values);

    // ---- no copy: withCritical ----
    public native long sum(int[] values);

    public native void negate(int[] values);

    public native void negateOneThenThrow(int[] values);

    public native long sumBytes(byte[] values);

    public native int sizeSeen(int[] values);

    // ---- direct buffer ----
    public native int capacityOf(ByteBuffer buffer);

    public native void fill(ByteBuffer buffer, int value);

    public native int capacityOrMinusOne(ByteBuffer buffer);

    // ---- called from native code ----
    public int[] ints = {1, 2, 3};
    public byte[] bytes = {10, 20, 30};
    public int[] nothing;
    public ByteBuffer buffer = ByteBuffer.allocateDirect(16);

    public int sumOf(int[] values) {
        int total = 0;
        for (int value : values) total += value;
        return total;
    }

    public int[] makeInts(int count) {
        int[] result = new int[count];
        for (int i = 0; i < count; i++) result[i] = i * i;
        return result;
    }

    public int[] nullInts() {
        return null;
    }

    public ByteBuffer direct(int capacity) {
        return ByteBuffer.allocateDirect(capacity);
    }

    public ByteBuffer heap(int capacity) {
        return ByteBuffer.allocate(capacity);
    }

    public ByteBuffer nullBuffer() {
        return null;
    }
}
