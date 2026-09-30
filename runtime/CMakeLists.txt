# Header-only for now (no sources yet). Becomes STATIC when the first .cpp appears.
add_library(jnirt_runtime INTERFACE)
add_library(jnirt::runtime ALIAS jnirt_runtime)

target_include_directories(jnirt_runtime INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jnirt_runtime INTERFACE jnirt::support)   # runtime -> support, never back

# When src/*.cpp arrives, replace the block above with:
#
#add_library(jnirt_runtime STATIC
#        src/vm.cpp ...
#)
#add_library(jnirt::runtime ALIAS jnirt_runtime)
#
#target_include_directories(jnirt_runtime
#        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
#        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
#)
#target_link_libraries(jnirt_runtime PUBLIC jnirt::support PRIVATE jnirt::options)
#
#target_compile_options(jnirt_runtime PRIVATE -Wall -Wextra -Wformat -Werror=format-security)