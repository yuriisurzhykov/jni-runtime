package jnirt.test;

import java.nio.ByteBuffer;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

// The cost of crossing (convert_bench.cpp): Java calls natives that convert their parameters, and the loop is timed here,
// the way a Kotlin app calls native code. Every case has a baseline (raw JNI types, nothing converted) where there is one.
public class Bench {
    public native String echoRaw(String text);

    public native String echoString(String text);

    public native int plainInt(int value);

    public native Integer boxedInt(Integer value);

    public native int[] echoInts(int[] values);

    public native long sumCopy(int[] values);

    public native long sumPieces(int[] values);

    public native long sumCritical(int[] values);

    public native long sumDirect(ByteBuffer buffer);

    public native List<String> echoList(List<String> items);

    public native Map<String, Integer> echoMap(Map<String, Integer> counts);

    private static volatile long sink;

    // nanoseconds per call of `body`, after a warm-up of the same length
    private static double measure(int iterations, Runnable body) {
        for (int i = 0; i < iterations; i++) body.run();
        long start = System.nanoTime();
        for (int i = 0; i < iterations; i++) body.run();
        return (System.nanoTime() - start) / (double) iterations;
    }

    private static void report(StringBuilder out, String what, String size, double nanos, int elements) {
        out.append(String.format("%-34s %-12s %12.0f ns/call", what, size, nanos));
        if (elements > 1) out.append(String.format(" %10.1f ns/element", nanos / elements));
        out.append('\n');
    }

    private static String repeat(int length) {
        StringBuilder text = new StringBuilder(length);
        for (int i = 0; i < length; i++) text.append((char) ('a' + i % 26));
        return text.toString();
    }

    public static String run() {
        Bench bench = new Bench();
        StringBuilder out = new StringBuilder();
        out.append("what                               size              time\n");

        // one crossing, nothing to convert
        report(out, "int (baseline)", "1", measure(2_000_000, () -> sink += bench.plainInt(1)), 1);
        report(out, "Int? as std::optional<jint>", "1", measure(2_000_000, () -> sink += bench.boxedInt(1)), 1);

        // text
        for (int length : new int[]{16, 1024, 65536}) {
            String text = repeat(length);
            int iterations = Math.max(200, 20_000_000 / (length + 200));
            report(out, "String raw jstring (baseline)", length + " chars",
                    measure(iterations, () -> sink += bench.echoRaw(text).length()), 1);
            report(out, "String as std::string (both ways)", length + " chars",
                    measure(iterations, () -> sink += bench.echoString(text).length()), 1);
        }

        // arrays
        for (int count : new int[]{1_000, 100_000, 1_000_000}) {
            int[] values = new int[count];
            for (int i = 0; i < count; i++) values[i] = i;
            ByteBuffer buffer = ByteBuffer.allocateDirect(count * 4);
            int iterations = Math.max(20, 20_000_000 / count);
            report(out, "IntArray copy, one way", count + " ints",
                    measure(iterations, () -> sink += bench.sumCopy(values)), count);
            report(out, "IntArray copy, both ways", count + " ints",
                    measure(iterations, () -> sink += bench.echoInts(values).length), count);
            report(out, "IntArray in 4096-pieces", count + " ints",
                    measure(iterations, () -> sink += bench.sumPieces(values)), count);
            report(out, "IntArray withCriticalRead", count + " ints",
                    measure(iterations, () -> sink += bench.sumCritical(values)), count);
            report(out, "DirectBuffer (same bytes)", count * 4 + " bytes",
                    measure(iterations, () -> sink += bench.sumDirect(buffer)), count * 4);
        }

        // collections
        for (int count : new int[]{10, 1_000, 100_000}) {
            List<String> items = new ArrayList<>();
            Map<String, Integer> counts = new HashMap<>();
            for (int i = 0; i < count; i++) {
                items.add("item " + i);
                counts.put("k" + i, i);
            }
            int iterations = Math.max(5, 2_000_000 / count);
            report(out, "List<String>, both ways", count + " items",
                    measure(iterations, () -> sink += bench.echoList(items).size()), count);
            report(out, "Map<String, Int>, both ways", count + " entries",
                    measure(iterations, () -> sink += bench.echoMap(counts).size()), count);
        }
        return out.toString();
    }
}
