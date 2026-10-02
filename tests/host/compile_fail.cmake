# One compile-fail case: the file must NOT compile, and the compiler output must contain the text of its `// EXPECT:`
# line (the message of a static_assert of jnirt). A case that compiles, or fails for another reason, fails the test.
#
# cmake -DCXX=<compiler> -DINCLUDES=<dir|dir|...> -DSOURCE=<file> -P compile_fail.cmake

file(STRINGS "${SOURCE}" expect_lines REGEX "^// EXPECT: ")
if (NOT expect_lines)
    message(FATAL_ERROR "${SOURCE}: no '// EXPECT: <text>' line")
endif ()
list(GET expect_lines 0 expect_line)
string(REGEX REPLACE "^// EXPECT: " "" expected "${expect_line}")

string(REPLACE "|" ";" include_dirs "${INCLUDES}")
set(include_flags "")
foreach (dir IN LISTS include_dirs)
    list(APPEND include_flags "-isystem" "${dir}")
endforeach ()

execute_process(
        COMMAND ${CXX} -std=c++17 -fsyntax-only ${include_flags} "${SOURCE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err)

if (result EQUAL 0)
    message(FATAL_ERROR "${SOURCE} compiled, but it must not.")
endif ()
string(FIND "${out}${err}" "${expected}" at)
if (at EQUAL -1)
    message(FATAL_ERROR "${SOURCE} failed, but not with the expected message.\n"
            "expected: ${expected}\n--- compiler output ---\n${out}${err}")
endif ()
