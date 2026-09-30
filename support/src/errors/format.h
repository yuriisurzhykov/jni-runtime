// Internal: printf-style formatting into std::string.
//
// Created by Yurii Surzhykov on 9/30/26.

#ifndef JNIRT_SUPPORT_FORMAT_H
#define JNIRT_SUPPORT_FORMAT_H

#pragma once
#include <jnirt/support/compiler.h>

#include <cstdarg>
#include <string>

namespace jnirt::detail {
    JNIRT_PRINTF(1, 0)
    std::string vformat(const char *fmt, va_list args);
}

#endif //JNIRT_SUPPORT_FORMAT_H
