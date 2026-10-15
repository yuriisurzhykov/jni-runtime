# jnirt_restrict_exports(<shared library target>)
#
# Makes JNI_OnLoad / JNI_OnUnload the only exported symbols of the library (roadmap P8, tools/check-native-lib.sh
# checks the result). Call it for the one .so of the app, after add_library(... SHARED ...).
# CACHE INTERNAL: the function is also called from a parent directory when jni-runtime is added as a subdirectory.
set(JNIRT_EXPORTS_MAP ${CMAKE_CURRENT_LIST_DIR}/jnirt_exports.map CACHE INTERNAL "Version script of jnirt_restrict_exports")

function(jnirt_restrict_exports target)
    target_link_options(${target} PRIVATE
            "LINKER:--version-script=${JNIRT_EXPORTS_MAP}"
            "LINKER:--exclude-libs,ALL")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${JNIRT_EXPORTS_MAP})
endfunction()
