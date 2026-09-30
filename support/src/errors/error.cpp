// Out-of-line parts of the error types. Defining the destructors here gives each exception class one
// home for its typeinfo, which matters with -fvisibility=hidden.
//
// Created by Yurii Surzhykov on 9/30/26.

#include <jnirt/support/error.h>

#include "format.h"

#include <cstring>

namespace jnirt {
    Error::~Error() = default;

    void Error::setMessage(const char *fmt, va_list args) noexcept {
        try {
            message_ = std::make_shared<const std::string>(detail::vformat(fmt, args));
        } catch (...) {
            // Out of memory: the error is still thrown, just without its text.
            message_.reset();
        }
    }

    const char *Error::what() const noexcept {
        return message_ ? message_->c_str() : "jnirt::Error (message lost: out of memory)";
    }

    bool Error::matches(const char *domain, int code) const noexcept {
        return code_ == code && std::strcmp(domain_, domain) == 0;
    }

    ContractViolation::ContractViolation(const std::string &message, const char *file, int line)
        : std::logic_error(message), file_(file), line_(line) {
    }

    ContractViolation::~ContractViolation() = default;

    IllegalArgument::~IllegalArgument() = default;

    IllegalState::~IllegalState() = default;
}
