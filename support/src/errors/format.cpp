#include "format.h"

#include <cstdio>

namespace jnirt::detail {
    std::string vformat(const char *fmt, va_list args) {
        va_list copy;
        va_copy(copy, args);
        const int length = std::vsnprintf(nullptr, 0, fmt, copy);
        va_end(copy);
        if (length < 0) return "<format error>";

        std::string out(static_cast<size_t>(length), '\0');
        std::vsnprintf(&out[0], out.size() + 1, fmt, args);
        return out;
    }
}
