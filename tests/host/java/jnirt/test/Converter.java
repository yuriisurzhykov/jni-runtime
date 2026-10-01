package jnirt.test;

// The class the conversion tests (convert_test.cpp) describe. The natives are registered by the tests with C++ types;
// the other members are called from native code through class_cache.
public class Converter {
    // ---- strings, bool, optional ----
    public native String echoString(String text);

    public native String echoOptString(String text);

    public native String echoUtf16(String text);

    public native boolean echoBool(boolean value);

    public native String joinThree(String a, String b, int count);

    public native String joinOpt(String a, String b);

    public native String byValue(String text);

    public native String byRvalue(String text);

    public static native String staticEcho(String text);

    public native Boolean echoOptBool(Boolean value);

    public native Byte echoOptByte(Byte value);

    public native Character echoOptChar(Character value);

    public native Short echoOptShort(Short value);

    public native Integer echoOptInt(Integer value);

    public native Long echoOptLong(Long value);

    public native Float echoOptFloat(Float value);

    public native Double echoOptDouble(Double value);

    public native int lengthOf(String text);

    // ---- called from native code ----
    public String name = "";
    public String nick;
    public static String title = "camera";

    public String greet(String who) {
        return "hello " + who;
    }

    public String greetOpt(String who) {
        return who == null ? null : "hello " + who;
    }

    public String nothing() {
        return null;
    }

    public static String shout(String text) {
        return text.toUpperCase();
    }

    public Integer boxedInt(Integer value) {
        return value;
    }

    public void takesTwo(String a, int n, boolean flag) {
        name = a + ":" + n + ":" + flag;
    }
}
