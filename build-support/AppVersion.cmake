# Portable, repository-local build-time application versioning.
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
set(_APP_VERSION_SUPPORT "${CMAKE_CURRENT_LIST_DIR}")

function(app_version_enable)
    cmake_parse_arguments(APP "" "" "GUI_TARGETS" ${ARGN})
    set(_root "${_APP_VERSION_SUPPORT}/..")
    set(_output "${CMAKE_BINARY_DIR}/app-version")
    add_custom_target(app_version_stamp
        COMMAND "${Python3_EXECUTABLE}" "${_APP_VERSION_SUPPORT}/app_version.py"
            --root "${_root}" --component "cmake:${CMAKE_PROJECT_NAME}"
            --allocate --header "${_output}" --output "${_output}/app-version.json"
        BYPRODUCTS "${_output}/app_version.h" "${_output}/app_version_value.h"
            "${_output}/app-version.json"
        COMMENT "Freezing the application commit/build version"
        VERBATIM
    )
    set_property(GLOBAL PROPERTY APP_VERSION_OUTPUT "${_output}")
    set_property(GLOBAL PROPERTY APP_VERSION_GUI_TARGETS "${APP_GUI_TARGETS}")
    _app_version_directory("${CMAKE_SOURCE_DIR}")
endfunction()

function(_app_version_directory directory)
    file(RELATIVE_PATH _relative "${CMAKE_SOURCE_DIR}" "${directory}")
    if(_relative MATCHES "^\\.\\./")
        return()
    endif()
    get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_output GLOBAL PROPERTY APP_VERSION_OUTPUT)
    get_property(_gui GLOBAL PROPERTY APP_VERSION_GUI_TARGETS)
    foreach(_target IN LISTS _targets)
        get_target_property(_type "${_target}" TYPE)
        if(_type STREQUAL "EXECUTABLE" AND NOT _target MATCHES "(^test|test$|benchmark)")
            add_dependencies("${_target}" app_version_stamp)
            target_include_directories("${_target}" PRIVATE "${_output}")
            target_compile_definitions("${_target}" PRIVATE APP_VERSION_BUILD=1)
            set(_capabilities "cli")
            if(_target IN_LIST _gui)
                set(_capabilities "gui,cli")
            endif()
            set(_source "${_output}/${_target}-version.cpp")
            file(WRITE "${_source}" [=[
#include "app_version.h"
#include "app_version_value.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace app_version {
const char *version() { return APP_VERSION; }
void handle_cli(int argc, char *const *argv) {
    if (argc == 2 && (!std::strcmp(argv[1], "--version") || !std::strcmp(argv[1], "-V"))) {
        const char *name = std::strrchr(argv[0], '/');
        std::printf("%s %s\n", name ? name + 1 : argv[0], version());
        std::exit(0);
    }
}
}
]=]
                "__attribute__((used, retain, section(\".program_metadata\"), aligned(1)))\n"
                "static const char app_version_record[] = \"PROGRAM-METADATA-V1\\n${_target}\\n\" APP_VERSION \"\\n${_capabilities}\\n\";\n")
            target_sources("${_target}" PRIVATE "${_source}")
        endif()
    endforeach()
    get_property(_children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(_child IN LISTS _children)
        _app_version_directory("${_child}")
    endforeach()
endfunction()
