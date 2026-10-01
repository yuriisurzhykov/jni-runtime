// The startup check of error domains: no two enums share a domain name. See ADR-0007.
//
// Created by Yurii Surzhykov on 10/1/26.

#include "class_cache_internal.h"

#include <algorithm>
#include <map>
#include <string>

namespace jnirt::detail {
    namespace {
        constexpr const char *kRuntimeDomain = "jnirt";
        constexpr const char *kRuntimeEnum = "jnirt::Errc";

        std::string join(const std::vector<std::string> &names) {
            std::string text;
            for (const std::string &name: names) text += (text.empty() ? "" : ", ") + name;
            return text;
        }
    } // namespace

    void checkDomains(const std::vector<DomainDeclaration> &declared, Problems &problems) {
        // domain name -> the enums that declared it (each enum once, in the order they were declared)
        std::map<std::string, std::vector<std::string> > byDomain;
        for (const DomainDeclaration &declaration: declared) {
            std::vector<std::string> &enums = byDomain[declaration.domain];
            if (std::find(enums.begin(), enums.end(), declaration.enumName) == enums.end()) {
                enums.push_back(declaration.enumName);
            }
        }

        for (const auto &[domain, enums]: byDomain) {
            if (domain.empty()) {
                problems.push_back("error domain of " + join(enums) + " is empty\n"
                                   "  hint: JNIRT_ERROR_DOMAIN(Enum, \"name\") needs a name; use the name of your library.");
            } else if (domain == kRuntimeDomain) {
                std::vector<std::string> others;
                for (const std::string &name: enums) {
                    if (name != kRuntimeEnum) others.push_back(name);
                }
                if (others.empty()) continue;
                problems.push_back("error domain \"jnirt\" is reserved for jnirt::Errc, but " + join(others) +
                                   " declares it\n"
                                   "  hint: use the name of your library as the domain.");
            } else if (enums.size() > 1) {
                problems.push_back("error domain \"" + domain + "\" is declared for " + std::to_string(enums.size()) +
                                   " different enums: " + join(enums) + "\n"
                                   "  why: Kotlin receives only the domain and the number, so two enums with one domain "
                                   "can not be told apart.\n"
                                   "  hint: every library needs its own domain name in JNIRT_ERROR_DOMAIN.");
            }
        }
    }
} // namespace jnirt::detail
