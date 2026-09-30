// A host implementation of the logging sink (stderr). Lets support build and be tested off-device.
//
// Created by Yurii Surzhykov on 9/30/26.

#include <logging/log_sink.h>

#include <cstdio>

namespace jnirt::logging {
    namespace {
        char toLetter(Level l) {
            switch (l) {
                case Level::Verbose: return 'V';
                case Level::Debug: return 'D';
                case Level::Info: return 'I';
                case Level::Warn: return 'W';
                case Level::Error: return 'E';
            }
            return '?';
        }
    }

    void sinkWrite(Level level, const char *tag, const char *message) noexcept {
        std::fprintf(stderr, "%c/%s: %s\n", toLetter(level), tag, message);
    }
}
