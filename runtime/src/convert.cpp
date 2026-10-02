// The non-template part of the conversions: texts, boxes, and the errors for a null. See
// <jnirt/runtime/convert.h> and docs/adr/0008-type-conversion.md.
//
// Created by Yurii Surzhykov on 10/1/26.

#include <jnirt/runtime/convert.h>
#include <jnirt/runtime/exception.h>
#include <jnirt/support/error.h>

#include "runtime_classes.h"
#include "utf.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace jnirt::detail {
    namespace {
        // A violation raised by jnirt itself has no file and line: the Java stack trace names the Kotlin method.
        [[noreturn]] void throwIllegalArgument(const std::string &message) { throw IllegalArgument(message, "", 0); }

        // Up to this many UTF-16 units the text of a Java string is read through the stack.
        constexpr jsize kStackUnits = 256;
    } // namespace

    void nullArgument(std::size_t number, const char *descriptor) {
        throwIllegalArgument("argument " + std::to_string(number) + " is null, but this parameter is a non-null " +
                             descriptor + ".\n  fix: declare it as std::optional<...> if null is allowed (Kotlin: T?).");
    }

    void nullValue(const char *descriptor) {
        throwIllegalArgument(std::string("the Java value is null, but its C++ type is a non-null ") + descriptor +
                             ".\n  fix: use std::optional<...> if null is allowed.");
    }

    void nullPart(Part part, std::size_t index, const char *descriptor) {
        std::string where;
        std::string kind;
        switch (part) {
            case Part::Element:
                where = "element " + std::to_string(index);
                kind = "the element type";
                break;
            case Part::Key:
                where = "the key of entry " + std::to_string(index);
                kind = "the key type";
                break;
            case Part::Value:
                where = "the value of entry " + std::to_string(index);
                kind = "the value type";
                break;
        }
        throwIllegalArgument(where + " is null, but " + kind + " is a non-null " + descriptor +
                             ".\n  fix: use std::optional<...> for it if null is allowed.");
    }

    std::string readString(JNIEnv *env, jstring text) {
        jsize length = env->GetStringLength(text);
        jchar stack[kStackUnits];
        std::unique_ptr<jchar[]> heap;
        jchar *units = stack;
        if (length > kStackUnits) {
            heap.reset(new jchar[static_cast<std::size_t>(length)]);
            units = heap.get();
        }
        env->GetStringRegion(text, 0, length, units);
        return utf16ToUtf8(reinterpret_cast<const char16_t *>(units), static_cast<std::size_t>(length));
    }

    std::u16string readString16(JNIEnv *env, jstring text) {
        jsize length = env->GetStringLength(text);
        std::u16string result(static_cast<std::size_t>(length), u'\0');
        env->GetStringRegion(text, 0, length, reinterpret_cast<jchar *>(result.data()));
        return result;
    }

    LocalRef<jstring> makeString16(JNIEnv *env, std::u16string_view utf16) {
        if (utf16.size() > static_cast<std::size_t>(INT32_MAX)) {
            throwIllegalArgument("the text is too long for a Java string (" + std::to_string(utf16.size()) +
                                 " UTF-16 units)");
        }
        LocalRef<jstring> text(env, env->NewString(reinterpret_cast<const jchar *>(utf16.data()),
                                                   static_cast<jsize>(utf16.size())));
        if (!text) throwPendingException(env); // OutOfMemoryError
        return text;
    }

    LocalRef<jstring> makeString(JNIEnv *env, std::string_view utf8) { return makeString16(env, utf8ToUtf16(utf8)); }

    jsize arraySize(std::size_t count) {
        if (count > static_cast<std::size_t>(INT32_MAX)) {
            throwIllegalArgument("the array is too long for Java (" + std::to_string(count) + " elements)");
        }
        return static_cast<jsize>(count);
    }

    void requireArray(jarray array) {
        if (array == nullptr) throwIllegalArgument("the Java array is null");
    }

    DirectBuffer readDirectBuffer(JNIEnv *env, jobject buffer) {
        void *address = env->GetDirectBufferAddress(buffer);
        jlong capacity = env->GetDirectBufferCapacity(buffer);
        if (address == nullptr || capacity < 0) {
            throwIllegalArgument("the ByteBuffer is not direct (or this JVM can not give its memory).\n"
                                 "  fix: create it with ByteBuffer.allocateDirect(n) on the Kotlin side.");
        }
        return DirectBuffer(static_cast<std::byte *>(address), static_cast<std::size_t>(capacity));
    }

    LocalRef<jobjectArray> collectionToArray(JNIEnv *env, jobject collection) {
        Array<Object> items = CollectionJ::toArray(env, collection);
        return copyOf(env, items.raw());
    }

    LocalRef<jobjectArray> mapEntries(JNIEnv *env, jobject map) {
        LocalRef<jobject> entries = MapJ::entrySet(env, map);
        return collectionToArray(env, entries.get());
    }

    LocalRef<jobject> entryKey(JNIEnv *env, jobject entry) { return MapEntryJ::getKey(env, entry); }

    LocalRef<jobject> entryValue(JNIEnv *env, jobject entry) { return MapEntryJ::getValue(env, entry); }

    LocalRef<jobject> newList(JNIEnv *env, jsize capacity) { return ArrayListJ::create(env, capacity); }

    void listAdd(JNIEnv *env, jobject list, jobject element) { ArrayListJ::add(env, list, element); }

    LocalRef<jobject> newMap(JNIEnv *env, jsize capacity) { return LinkedHashMapJ::create(env, capacity); }

    void mapPut(JNIEnv *env, jobject map, jobject key, jobject value) { MapJ::put(env, map, key, value); }

    LocalRef<jobject> Box<jboolean>::box(JNIEnv *env, jboolean value) { return BooleanJ::valueOf(env, value); }

    jboolean Box<jboolean>::unbox(JNIEnv *env, jobject boxed) { return BooleanJ::booleanValue(env, boxed); }

    LocalRef<jobject> Box<jbyte>::box(JNIEnv *env, jbyte value) { return ByteJ::valueOf(env, value); }

    jbyte Box<jbyte>::unbox(JNIEnv *env, jobject boxed) { return ByteJ::byteValue(env, boxed); }

    LocalRef<jobject> Box<jchar>::box(JNIEnv *env, jchar value) { return CharacterJ::valueOf(env, value); }

    jchar Box<jchar>::unbox(JNIEnv *env, jobject boxed) { return CharacterJ::charValue(env, boxed); }

    LocalRef<jobject> Box<jshort>::box(JNIEnv *env, jshort value) { return ShortJ::valueOf(env, value); }

    jshort Box<jshort>::unbox(JNIEnv *env, jobject boxed) { return ShortJ::shortValue(env, boxed); }

    LocalRef<jobject> Box<jint>::box(JNIEnv *env, jint value) { return IntegerJ::valueOf(env, value); }

    jint Box<jint>::unbox(JNIEnv *env, jobject boxed) { return IntegerJ::intValue(env, boxed); }

    LocalRef<jobject> Box<jlong>::box(JNIEnv *env, jlong value) { return LongJ::valueOf(env, value); }

    jlong Box<jlong>::unbox(JNIEnv *env, jobject boxed) { return LongJ::longValue(env, boxed); }

    LocalRef<jobject> Box<jfloat>::box(JNIEnv *env, jfloat value) { return FloatJ::valueOf(env, value); }

    jfloat Box<jfloat>::unbox(JNIEnv *env, jobject boxed) { return FloatJ::floatValue(env, boxed); }

    LocalRef<jobject> Box<jdouble>::box(JNIEnv *env, jdouble value) { return DoubleJ::valueOf(env, value); }

    jdouble Box<jdouble>::unbox(JNIEnv *env, jobject boxed) { return DoubleJ::doubleValue(env, boxed); }
} // namespace jnirt::detail
