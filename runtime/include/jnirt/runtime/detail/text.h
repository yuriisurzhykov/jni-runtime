// Not for direct use. Strings that the compiler builds: the pieces of JVM descriptors and signatures
// (signature.h, convert.h). See docs/adr/0005-signatures.md.
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_DETAIL_TEXT_H
#define JNIRT_RUNTIME_DETAIL_TEXT_H

#pragma once
#include <cstddef>
#include <type_traits>

namespace jnirt::detail {
    // A string whose length N (without the terminating '\0') is part of its type, so the compiler can build it.
    template<std::size_t N>
    struct Text {
        char chars[N + 1]{};

        constexpr const char *c_str() const { return chars; }
    };

    // "abc" -> Text<3>
    template<std::size_t N>
    constexpr Text<N - 1> text(const char (&literal)[N]) {
        Text<N - 1> result{};
        for (std::size_t i = 0; i < N - 1; ++i) result.chars[i] = literal[i];
        return result;
    }

    // Text<A> + Text<B> -> Text<A + B>
    template<std::size_t A, std::size_t B>
    constexpr Text<A + B> operator+(const Text<A> &left, const Text<B> &right) {
        Text<A + B> result{};
        for (std::size_t i = 0; i < A; ++i) result.chars[i] = left.chars[i];
        for (std::size_t i = 0; i < B; ++i) result.chars[A + i] = right.chars[i];
        return result;
    }

    constexpr std::size_t length(const char *s) {
        std::size_t n = 0;
        while (s[n] != '\0') ++n;
        return n;
    }

    // false, but only once T is known: lets a static_assert fire only for the type somebody actually used.
    template<class T>
    struct AlwaysFalse : std::false_type {
    };
} // namespace jnirt::detail

#endif //JNIRT_RUNTIME_DETAIL_TEXT_H
