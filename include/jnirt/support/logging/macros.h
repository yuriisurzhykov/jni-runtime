//
// Created by Yurii Surzhykov on 9/28/26.
//

#ifndef JNI_RUNTIME_MARCOS_H
#define JNI_RUNTIME_MARCOS_H

#pragma once
#include "jnirt/support/logging/log.h"

#define LOG_DEFINE_TAG(tag) \
    namespace { [[maybe_unused]] constexpr const char* kSupportLogTag = tag; }

#define LOG_DEFINE_TAG(tag) \
    namespace { [[maybe_unused]] constexpr const char* kSupportLogTag = tag; }

#define SUPPORT_LOG_IMPL(level, ...)                                        \
    do {                                                                    \
        if (::support::logging::enabled(level))                             \
            ::support::logging::write(level, kSupportLogTag, __VA_ARGS__);  \
    } while (0)

#define LOGV(...) SUPPORT_LOG_IMPL(::support::logging::Level::Verbose, __VA_ARGS__)
#define LOGD(...) SUPPORT_LOG_IMPL(::support::logging::Level::Debug,   __VA_ARGS__)
#define LOGI(...) SUPPORT_LOG_IMPL(::support::logging::Level::Info,    __VA_ARGS__)
#define LOGW(...) SUPPORT_LOG_IMPL(::support::logging::Level::Warn,    __VA_ARGS__)
#define LOGE(...) SUPPORT_LOG_IMPL(::support::logging::Level::Error,   __VA_ARGS__)

#endif //JNI_RUNTIME_MARCOS_H
