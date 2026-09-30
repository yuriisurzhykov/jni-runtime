// An Android implementation of the logging mechanism to sink every log message into Logcat.
//
// Created by Yurii Surzhykov on 9/28/26.

#include <logging/log_sink.h>
#include <android/log.h>

namespace jnirt::logging {
    namespace {
        int toPriority(Level l) {
            switch (l) {
                case Level::Verbose: return ANDROID_LOG_VERBOSE;
                case Level::Debug: return ANDROID_LOG_DEBUG;
                case Level::Info: return ANDROID_LOG_INFO;
                case Level::Warn: return ANDROID_LOG_WARN;
                case Level::Error: return ANDROID_LOG_ERROR;
                default: return ANDROID_LOG_INFO;
            }
            return ANDROID_LOG_INFO;
        }
    }

    void sinkWrite(Level level, const char *tag, const char *message) noexcept {
        __android_log_write(toPriority(level), tag, message);
    }
}
