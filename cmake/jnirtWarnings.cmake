# Common compile settings for every jnirt-based module. Link with: PRIVATE jnirt::options
add_library(jnirt_options INTERFACE)
add_library(jnirt::options ALIAS jnirt_options)

target_compile_features(jnirt_options INTERFACE cxx_std_17)
target_compile_options(jnirt_options INTERFACE
        -Wall -Wextra
        -Wformat -Werror=format-security   # printf-style checks for LOGx
        -fvisibility=hidden                # hide everything except explicit exports
        -fexceptions -frtti                # ADR-0001 6.1: errors are C++ exceptions; a consumer must not switch them off
)