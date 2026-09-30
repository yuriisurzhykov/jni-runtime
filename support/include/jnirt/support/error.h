// The file provides a unified error handling mechanism. Different error types, result mapping, etc.
//
// Three kinds of failures (ADR-0002):
//   recoverable error  -> throw jnirt::Error          (the caller can react: timeout, no network, ...)
//   programmer error   -> JNIRT_REQUIRE / JNIRT_CHECK (assert.h; throws IllegalArgument / IllegalState)
//   broken invariant   -> JNIRT_ASSERT / JNIRT_FATAL  (assert.h; crashes with a tombstone)
//
// Created by Yurii Surzhykov on 9/28/26.

#ifndef JNIRT_SUPPORT_ERROR_H
#define JNIRT_SUPPORT_ERROR_H

#pragma once
#include <jnirt/support/compiler.h>

#include <cstdarg>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace jnirt {
    // Ties an error-code enum to its domain name. Do not specialize by hand: use JNIRT_ERROR_DOMAIN.
    template<class E>
    struct ErrorTraits {
    };

    namespace detail {
        template<class E, class = void>
        struct HasDomain : std::false_type {
        };
        template<class E>
        struct HasDomain<E, std::void_t<decltype(ErrorTraits<E>::kDomain)> > : std::true_type {
        };

        template<class E>
        constexpr const char *domainOf() noexcept {
            static_assert(std::is_enum<E>::value,
                          "jnirt::Error: the code must be a value of an enum class");
            static_assert(HasDomain<E>::value,
                          "jnirt::Error: this enum has no domain; add JNIRT_ERROR_DOMAIN(YourEnum, \"name\")");
            return ErrorTraits<E>::kDomain;
        }
    } // namespace detail

    // A recoverable error: something outside the caller's control failed, and the caller can react.
    //
    // It is identified by a domain (one per library) and a code (a value of that library's enum class):
    //
    //   enum class Errc { AuthFailed = 1, StreamLost = 2 };   // numbers are forever: Kotlin depends on them
    //   JNIRT_ERROR_DOMAIN(wificameras::Errc, "wificameras")  // once, at global scope
    //
    //   throw jnirt::Error(Errc::AuthFailed, "camera %s rejected login", host);
    //
    // To keep the cause, throw inside a catch block: std::throw_with_nested(jnirt::Error(...)).
    class Error : public std::exception {
    public:
        // The message is printf-style and checked by the compiler.
        template<class E>
        JNIRT_PRINTF(3, 4)
        Error(E code, const char *fmt, ...)
            : domain_(detail::domainOf<E>()), code_(static_cast<int>(code)) {
            va_list args;
            va_start(args, fmt);
            setMessage(fmt, args);
            va_end(args);
        }

        ~Error() override;

        const char *domain() const noexcept { return domain_; }
        int code() const noexcept { return code_; }
        const char *what() const noexcept override;

        // if (e.is(Errc::AuthFailed)) ...
        template<class E>
        bool is(E code) const noexcept {
            return matches(detail::domainOf<E>(), static_cast<int>(code));
        }

    private:
        void setMessage(const char *fmt, va_list args) noexcept;

        bool matches(const char *domain, int code) const noexcept;

        const char *domain_; // a string literal
        int code_;
        // Shared, so that copying the exception can never throw (a throwing copy means std::terminate).
        std::shared_ptr<const std::string> message_;
    };

    static_assert(std::is_nothrow_copy_constructible<Error>::value, "an exception must be nothrow-copyable");

    // Generic codes that any library may use. Domain: "jnirt".
    enum class Errc : int {
        Timeout = 1,
        NotFound = 2,
        Io = 3,
        Internal = 4,
    };

    // A programmer error: the caller broke the contract. Thrown by JNIRT_REQUIRE / JNIRT_CHECK.
    // what() is the message only; the place in the code is in file() / line().
    class ContractViolation : public std::logic_error {
    public:
        ContractViolation(const std::string &message, const char *file, int line);

        ~ContractViolation() override;

        const char *file() const noexcept { return file_; } // __FILE__, a string literal
        int line() const noexcept { return line_; }

    private:
        const char *file_;
        int line_;
    };

    // A bad argument. In Kotlin: IllegalArgumentException.
    class IllegalArgument : public ContractViolation {
    public:
        using ContractViolation::ContractViolation;

        ~IllegalArgument() override;
    };

    // A call in a wrong state (use after close, wrong order). In Kotlin: IllegalStateException.
    class IllegalState : public ContractViolation {
    public:
        using ContractViolation::ContractViolation;

        ~IllegalState() override;
    };
} // namespace jnirt

// Binds an error-code enum to a domain name. Use once per enum, at global scope (outside any namespace):
//   JNIRT_ERROR_DOMAIN(wificameras::Errc, "wificameras")
// Domain names must be unique across libraries.
#define JNIRT_ERROR_DOMAIN(EnumType, DomainName)                                  \
    namespace jnirt {                                                             \
        template <> struct ErrorTraits<EnumType> {                                \
            static constexpr const char* kDomain = DomainName;                    \
        };                                                                        \
    }

JNIRT_ERROR_DOMAIN(jnirt::Errc, "jnirt")

#endif //JNIRT_SUPPORT_ERROR_H
