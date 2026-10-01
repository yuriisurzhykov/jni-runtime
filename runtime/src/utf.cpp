// UTF-8 to a Java string. See utf.h.
//
// Created by Yurii Surzhykov on 10/1/26.

#include "utf.h"

#include <cstdint>

namespace jnirt::detail {
    namespace {
        constexpr char16_t kReplacement = 0xFFFD;

        bool isContinuation(unsigned char byte) { return (byte & 0xC0) == 0x80; }

        // For a lead byte: how many bytes the character has, and the range its second byte must be in (this is what
        // rules out overlong forms, surrogates and values above U+10FFFF). length 0: not a lead byte.
        struct Lead {
            int length;
            unsigned char secondMin;
            unsigned char secondMax;
        };

        Lead leadOf(unsigned char b) {
            if (b >= 0xC2 && b <= 0xDF) return {2, 0x80, 0xBF};
            if (b == 0xE0) return {3, 0xA0, 0xBF};
            if (b == 0xED) return {3, 0x80, 0x9F};
            if (b >= 0xE1 && b <= 0xEF) return {3, 0x80, 0xBF};
            if (b == 0xF0) return {4, 0x90, 0xBF};
            if (b >= 0xF1 && b <= 0xF3) return {4, 0x80, 0xBF};
            if (b == 0xF4) return {4, 0x80, 0x8F};
            return {0, 0, 0};
        }
    } // namespace

    std::u16string utf8ToUtf16(std::string_view text) {
        std::u16string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size();) {
            auto b = static_cast<unsigned char>(text[i]);
            if (b < 0x80) {
                out.push_back(b);
                ++i;
                continue;
            }
            Lead lead = leadOf(b);
            if (lead.length == 0) {
                out.push_back(kReplacement);
                ++i;
                continue;
            }
            // The lead byte and as many valid continuation bytes as there are; a short run is one bad sequence.
            std::uint32_t code = b & (0xFF >> (lead.length + 1));
            int taken = 1;
            while (taken < lead.length && i + taken < text.size()) {
                auto next = static_cast<unsigned char>(text[i + taken]);
                bool ok = taken == 1 ? (next >= lead.secondMin && next <= lead.secondMax) : isContinuation(next);
                if (!ok) break;
                code = (code << 6) | (next & 0x3F);
                ++taken;
            }
            i += taken;
            if (taken < lead.length) {
                out.push_back(kReplacement);
            } else if (code >= 0x10000) {
                code -= 0x10000;
                out.push_back(static_cast<char16_t>(0xD800 + (code >> 10)));
                out.push_back(static_cast<char16_t>(0xDC00 + (code & 0x3FF)));
            } else {
                out.push_back(static_cast<char16_t>(code));
            }
        }
        return out;
    }

    jstring newJavaString(JNIEnv *env, std::string_view text) {
        std::u16string utf16 = utf8ToUtf16(text);
        return env->NewString(reinterpret_cast<const jchar *>(utf16.data()), static_cast<jsize>(utf16.size()));
    }
} // namespace jnirt::detail
