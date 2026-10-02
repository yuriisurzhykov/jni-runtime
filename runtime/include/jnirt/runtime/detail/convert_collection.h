// Not for direct use: include <jnirt/runtime/convert.h>. Objects and collections: jnirt::Array<C> (a Java array of
// objects of class C), std::vector<T> as java.util.List, std::map / std::unordered_map as java.util.Map. See
// docs/adr/0008-type-conversion.md §7.
//
//   std::vector<std::string>                    List<String>
//   std::vector<std::optional<std::string>>     List<String?>
//   std::map<std::string, jint>                 Map<String, Int>
//   std::unordered_map<std::string, Frame>      Map<String, Frame>        (Frame: your own row)
//   jnirt::Array<FrameJ>                        Array<Frame>              (the Java array itself)
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_DETAIL_CONVERT_COLLECTION_H
#define JNIRT_RUNTIME_DETAIL_CONVERT_COLLECTION_H

#pragma once
#include <jnirt/runtime/detail/convert_array.h>
#include <jnirt/runtime/detail/convert_optional.h>

#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace jnirt {
    // A Java array of objects, `Array<Frame>` in Kotlin, with the class of its elements in the type: the signature is
    // [Lcom/acme/camera/Frame; for Array<FrameJ>. It holds a local reference to the array (no elements are copied or
    // converted), so it is as thread-bound as LocalRef. Elements are raw objects: get() gives a LocalRef<jobject>,
    // which the loop body owns for one iteration.
    //
    //   auto frames = jnirt::newArray<FrameJ>(env, 3);
    //   frames.set(env, 0, frame.get());
    //   LocalRef<jobject> first = frames.get(env, 0);
    template<class C>
    class Array {
        static_assert(detail::isJavaClass<C>,
                      "jnirt: Array<C>: C must be a described class (struct C : jnirt::JavaClass<C> { ... })");

    public:
        Array() noexcept = default;

        explicit Array(LocalRef<jobjectArray> array) noexcept : array_(std::move(array)) {}

        // The JNI reference; the Array still owns it.
        jobjectArray raw() const noexcept { return array_.get(); }

        explicit operator bool() const noexcept { return static_cast<bool>(array_); }

        jsize size(JNIEnv *env) const {
            detail::requireArray(array_.get());
            return env->GetArrayLength(array_.get());
        }

        // Element i, null if it is null. A bad index is the JVM's ArrayIndexOutOfBoundsException, thrown as JavaException.
        LocalRef<jobject> get(JNIEnv *env, jsize index) const {
            detail::requireArray(array_.get());
            LocalRef<jobject> element(env, env->GetObjectArrayElement(array_.get(), index));
            if (!element) detail::throwIfPending(env);
            return element;
        }

        // A wrong class is the JVM's ArrayStoreException, a bad index its ArrayIndexOutOfBoundsException.
        void set(JNIEnv *env, jsize index, jobject element) const {
            detail::requireArray(array_.get());
            env->SetObjectArrayElement(array_.get(), index, element);
            detail::throwIfPending(env);
        }

    private:
        LocalRef<jobjectArray> array_;
    };

    // A new array of `size` nulls. C::cls() needs jnirt::init() to have run.
    template<class C>
    Array<C> newArray(JNIEnv *env, jsize size) {
        LocalRef<jobjectArray> array(env, env->NewObjectArray(size, C::cls(), nullptr));
        if (!array) detail::throwPendingException(env); // OutOfMemoryError, NegativeArraySizeException
        return Array<C>(std::move(array));
    }

    namespace detail {
        // A second local reference to the same array: the Array outlives the one JNI gave.
        inline LocalRef<jobjectArray> copyOf(JNIEnv *env, jobjectArray array) {
            LocalRef<jobjectArray> copy(env, static_cast<jobjectArray>(env->NewLocalRef(array)));
            if (!copy && array != nullptr) throwPendingException(env);
            return copy;
        }

        // The pieces of List and Map that need the classes of runtime_classes.h. Defined in convert.cpp.
        LocalRef<jobjectArray> collectionToArray(JNIEnv *env, jobject collection); // collection.toArray()
        LocalRef<jobjectArray> mapEntries(JNIEnv *env, jobject map); // map.entrySet().toArray()
        LocalRef<jobject> entryKey(JNIEnv *env, jobject entry);
        LocalRef<jobject> entryValue(JNIEnv *env, jobject entry);
        LocalRef<jobject> newList(JNIEnv *env, jsize capacity); // new ArrayList(capacity)
        void listAdd(JNIEnv *env, jobject list, jobject element);
        LocalRef<jobject> newMap(JNIEnv *env, jsize capacity); // new LinkedHashMap(capacity)
        void mapPut(JNIEnv *env, jobject map, jobject key, jobject value);

        // What may be an element of a List, or a key or value of a Map. A raw JNI reference would point to a local
        // reference that is deleted at the end of the iteration; a DirectBuffer to memory that may be collected.
        template<class T>
        constexpr void checkMember() {
            static_assert(!std::is_void_v<T>, "jnirt: void is not a type of a collection");
            static_assert(!isHandle<T>,
                          "jnirt: a raw JNI reference (jstring, jobject, a described class) can not be an element or a "
                          "key / value: it would point to a local reference that is already deleted. Use a converted "
                          "value (std::string, your own type with a Convert row).");
            static_assert(!BorrowFlag<RowOf<T> >::value,
                          "jnirt: this value points into a Java object that may be collected (a DirectBuffer); it can "
                          "not be an element of a collection.");
            static_assert(!PeerFlag<RowOf<T> >::value,
                          "jnirt: a Peer can not be an element of a collection: a Peer is a native parameter or result "
                          "and nothing else (ADR-0009 §4).");
        }

        // An element, key or value, read as T. `index` is for the message.
        template<class T>
        T readMember(JNIEnv *env, jobject item, std::size_t index, Part part) {
            if constexpr (!NullFlag<RowOf<T> >::value) {
                if (item == nullptr) nullPart(part, index, descriptorText<T>().c_str());
            }
            return ObjectOf<T>::fromObject(env, static_cast<typename ObjectOf<T>::Jni>(item));
        }

        template<class M>
        auto reserveIfCan(M &map, std::size_t count, int) -> decltype(map.reserve(count), void()) {
            map.reserve(count);
        }

        template<class M>
        void reserveIfCan(M &, std::size_t, long) {}

        // Object element i of an Object[] (empty LocalRef for null).
        inline LocalRef<jobject> elementOf(JNIEnv *env, jobjectArray array, jsize index) {
            LocalRef<jobject> element(env, env->GetObjectArrayElement(array, index));
            if (!element) throwIfPending(env);
            return element;
        }

        template<class Map, class K, class V>
        struct MapRow {
            static_assert((checkMember<K>(), checkMember<V>(), true), "");

            using Jni = jobject;
            using Value = Map;
            static constexpr auto descriptor = text("Ljava/util/Map;");

            static Map fromJava(JNIEnv *env, jobject map) {
                LocalRef<jobjectArray> entries = mapEntries(env, map);
                const jsize count = env->GetArrayLength(entries.get());
                Map result;
                reserveIfCan(result, static_cast<std::size_t>(count), 0);
                for (jsize i = 0; i < count; ++i) {
                    LocalRef<jobject> entry = elementOf(env, entries.get(), i);
                    LocalRef<jobject> key = entryKey(env, entry.get());
                    LocalRef<jobject> value = entryValue(env, entry.get());
                    K readKey = readMember<K>(env, key.get(), static_cast<std::size_t>(i), Part::Key);
                    V readValue = readMember<V>(env, value.get(), static_cast<std::size_t>(i), Part::Value);
                    result.emplace(std::move(readKey), std::move(readValue));
                }
                return result;
            }

            static LocalRef<jobject> toJava(JNIEnv *env, const Map &map) {
                LocalRef<jobject> result = newMap(env, arraySize(map.size()));
                for (const auto &pair: map) {
                    LocalRef<typename ObjectOf<K>::Jni> key = ObjectOf<K>::toObject(env, pair.first);
                    LocalRef<typename ObjectOf<V>::Jni> value = ObjectOf<V>::toObject(env, pair.second);
                    mapPut(env, result.get(), key.get(), value.get());
                }
                return result;
            }
        };
    } // namespace detail

    // Array<C> is the reference to a Java array: a null is an error unless std::optional<Array<C> >.
    template<class C>
    struct Convert<Array<C> > {
        using Jni = jobjectArray;
        using Value = Array<C>;
        static constexpr auto descriptor = detail::text("[") + detail::classDescriptor<C>();

        static Array<C> fromJava(JNIEnv *env, jobjectArray array) { return Array<C>(detail::copyOf(env, array)); }

        static LocalRef<jobjectArray> toJava(JNIEnv *env, const Array<C> &array) {
            return detail::copyOf(env, array.raw());
        }
    };

    // std::vector<T> for a T that is not a number: java.util.List. The list is read with toArray() (one call, then the
    // elements: a LinkedList is not quadratic) and written as an ArrayList. A local reference per element lives for one
    // iteration, so the number of local references does not depend on the length.
    template<class T>
    struct Convert<std::vector<T>, std::enable_if_t<!detail::isNumberElement<T> && !std::is_same_v<T, bool> &&
                                                    !std::is_same_v<T, jboolean> > > {
        static_assert((detail::checkMember<T>(), true), "");

        using Jni = jobject;
        using Value = std::vector<T>;
        static constexpr auto descriptor = detail::text("Ljava/util/List;");

        static Value fromJava(JNIEnv *env, jobject list) {
            LocalRef<jobjectArray> items = detail::collectionToArray(env, list);
            const jsize count = env->GetArrayLength(items.get());
            Value result;
            result.reserve(static_cast<std::size_t>(count));
            for (jsize i = 0; i < count; ++i) {
                LocalRef<jobject> item = detail::elementOf(env, items.get(), i);
                result.push_back(detail::readMember<T>(env, item.get(), static_cast<std::size_t>(i),
                                                       detail::Part::Element));
            }
            return result;
        }

        static LocalRef<jobject> toJava(JNIEnv *env, const Value &values) {
            LocalRef<jobject> result = detail::newList(env, detail::arraySize(values.size()));
            for (const T &value: values) {
                LocalRef<typename detail::ObjectOf<T>::Jni> element = detail::ObjectOf<T>::toObject(env, value);
                detail::listAdd(env, result.get(), element.get());
            }
            return result;
        }
    };

    // Map<K, V>. Reading takes entrySet().toArray(), then two calls per entry; writing makes a LinkedHashMap, so Java
    // sees the iteration order of the C++ map. A number as key or value is boxed (Map<String, Int>).
    template<class K, class V, class Compare, class Alloc>
    struct Convert<std::map<K, V, Compare, Alloc> > : detail::MapRow<std::map<K, V, Compare, Alloc>, K, V> {
    };

    template<class K, class V, class Hash, class Equal, class Alloc>
    struct Convert<std::unordered_map<K, V, Hash, Equal, Alloc> >
            : detail::MapRow<std::unordered_map<K, V, Hash, Equal, Alloc>, K, V> {
    };
} // namespace jnirt

#endif //JNIRT_RUNTIME_DETAIL_CONVERT_COLLECTION_H
