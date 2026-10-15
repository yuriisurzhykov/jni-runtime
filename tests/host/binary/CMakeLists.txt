# The release rules for a native library (tools/check-native-lib.sh), tried on host libraries:
# one that follows them must pass, and one for each rule that is broken must fail.
# jnirt_lib_leaks_symbol skips jnirt_restrict_exports, as a consumer would that forgot it.
# The 16 KB alignment is a linker option here; for the Android build it comes from the NDK
# (-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON on r27) and the CI job checks it.
# A test passes when the script prints the expected FAIL line (ctest then ignores the exit code).
set(JNIRT_CHECK_SCRIPT ${PROJECT_SOURCE_DIR}/tools/check-native-lib.sh)

function(jnirt_add_native_lib name source)
    add_library(${name} SHARED ${source})
    target_link_libraries(${name} PRIVATE jnirt::runtime jnirt::options)
    target_link_options(${name} PRIVATE ${ARGN})
endfunction()

jnirt_add_native_lib(jnirt_lib_ok onload_ok.cpp -Wl,-z,max-page-size=16384)
jnirt_restrict_exports(jnirt_lib_ok)
jnirt_add_native_lib(jnirt_lib_leaks_symbol leaks_symbol.cpp -Wl,-z,max-page-size=16384)
jnirt_add_native_lib(jnirt_lib_small_pages onload_ok.cpp -Wl,-z,max-page-size=4096)
jnirt_restrict_exports(jnirt_lib_small_pages)

add_test(NAME NativeLib.FollowsTheRules
        COMMAND ${JNIRT_CHECK_SCRIPT} --require-symbols --allow-libstdcxx $<TARGET_FILE:jnirt_lib_ok>)

add_test(NAME NativeLib.ExtraExportIsCaught
        COMMAND ${JNIRT_CHECK_SCRIPT} --allow-libstdcxx $<TARGET_FILE:jnirt_lib_leaks_symbol>)
set_tests_properties(NativeLib.ExtraExportIsCaught PROPERTIES
        PASS_REGULAR_EXPRESSION "FAIL: .*exports .jnirt_test_leaked_symbol")

add_test(NAME NativeLib.SmallPagesAreCaught
        COMMAND ${JNIRT_CHECK_SCRIPT} --allow-libstdcxx $<TARGET_FILE:jnirt_lib_small_pages>)
set_tests_properties(NativeLib.SmallPagesAreCaught PROPERTIES
        PASS_REGULAR_EXPRESSION "FAIL: .*aligned to 4096 bytes")
