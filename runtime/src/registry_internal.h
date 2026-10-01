// Runtime-internal access to what a Registry collected. Not part of the public API.
//
// Created by Yurii Surzhykov on 10/1/26.

#ifndef JNIRT_RUNTIME_REGISTRY_INTERNAL_H
#define JNIRT_RUNTIME_REGISTRY_INTERNAL_H

#pragma once
#include <jnirt/runtime/registry.h>

namespace jnirt::detail {
    struct RegistryAccess {
        static const std::vector<ClassEntry> &classes(const Registry &registry) { return registry.classes_; }
        static std::vector<std::string> &problems(Registry &registry) { return registry.problems_; }
    };
} // namespace jnirt::detail

#endif //JNIRT_RUNTIME_REGISTRY_INTERNAL_H
