package jnirt.test;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;

// The class the collection tests (convert_collection_test.cpp) describe. The natives are registered by the tests with
// C++ types; the other members are called from native code through class_cache.
public class Collector {
    // ---- List ----
    public native List<String> echoStrings(List<String> items);

    public native List<String> echoOptStrings(List<String> items);

    public native List<Frame> reversed(List<Frame> frames);

    public native List<List<String>> echoNested(List<List<String>> nested);

    public native List<Integer> echoBoxed(List<Integer> numbers);

    public native List<int[]> echoArrays(List<int[]> arrays);

    public native int totalLength(List<String> items);

    public native List<String> range(int count);

    public native List<String> echoProbes(List<String> items);

    // ---- Map ----
    public native Map<String, Integer> echoCounts(Map<String, Integer> counts);

    public native Map<String, Frame> echoFrameMap(Map<String, Frame> frames);

    public native Map<Integer, String> echoIntKeys(Map<Integer, String> names);

    public native Map<String, String> echoOptValues(Map<String, String> map);

    public native Map<String, Integer> numbered(int count);

    // ---- objects and arrays ----
    public native Frame echoFrame(Frame frame);

    public native Frame maybeFrame(Frame frame);

    public native Frame[] firstTwo(Frame[] frames);

    public native Frame[] makeFrameArray(int count);

    public native int arrayLength(Frame[] frames);

    public native Frame[] echoFrameArray(Frame[] frames);

    // ---- called from native code ----
    public List<String> names = new ArrayList<>(List.of("a", "b"));
    public Map<String, Integer> ages = new HashMap<>(Map.of("x", 1));
    public Frame[] frames = {new Frame(1, "one"), null};

    public List<String> makeList(int count) {
        List<String> result = new ArrayList<>();
        for (int i = 0; i < count; i++) result.add("item " + i);
        return result;
    }

    public List<String> makeLinkedList(int count) {
        List<String> result = new LinkedList<>();
        for (int i = 0; i < count; i++) result.add("n" + i);
        return result;
    }

    public List<String> listWithNull() {
        List<String> result = new ArrayList<>();
        result.add("a");
        result.add(null);
        return result;
    }

    public Map<String, String> mapWithNullKey() {
        Map<String, String> result = new HashMap<>();
        result.put(null, "value");
        return result;
    }

    public Map<String, String> mapWithNullValue() {
        Map<String, String> result = new HashMap<>();
        result.put("key", null);
        return result;
    }

    public int sizeOf(List<String> items) {
        return items.size();
    }

    public String joined(List<String> items) {
        return String.join(",", items);
    }

    public List<String> keysInOrder(Map<String, String> map) {
        return new ArrayList<>(map.keySet());
    }

    public String describe(Map<String, Integer> map) {
        return new java.util.TreeMap<>(map).toString();
    }

    public List<String> nothingList() {
        return null;
    }

    public Map<String, Integer> manyEntries(int count) {
        Map<String, Integer> result = new HashMap<>();
        for (int i = 0; i < count; i++) result.put("k" + i, i);
        return result;
    }
}
