cmake_minimum_required(VERSION 3.10)
project(AudioPlayer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SDL2
find_package(SDL2 REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_program(CARGO_EXECUTABLE cargo)
if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR "cargo is required to build the fuzzy-rank bridge")
endif()

set(FUZZY_RANK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../fuzzy-rank")
set(FUZZY_BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/rust/fuzzy_rank_bridge")
set(FUZZY_BRIDGE_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo-target")
set(FUZZY_BRIDGE_LIBRARY "${FUZZY_BRIDGE_TARGET_DIR}/release/libfuzzy_rank_bridge.a")
if(NOT EXISTS "${FUZZY_RANK_DIR}/Cargo.toml")
    message(FATAL_ERROR "Expected fuzzy-rank crate at ${FUZZY_RANK_DIR}")
endif()
file(GLOB_RECURSE FUZZY_RANK_SOURCES
    "${FUZZY_RANK_DIR}/src/*.rs"
    "${FUZZY_BRIDGE_DIR}/src/*.rs"
)
add_custom_command(
    OUTPUT "${FUZZY_BRIDGE_LIBRARY}"
    COMMAND ${CMAKE_COMMAND} -E env
        "CARGO_TARGET_DIR=${FUZZY_BRIDGE_TARGET_DIR}"
        ${CARGO_EXECUTABLE} build
        --manifest-path "${FUZZY_BRIDGE_DIR}/Cargo.toml"
        --release
    DEPENDS
        "${FUZZY_BRIDGE_DIR}/Cargo.toml"
        "${FUZZY_BRIDGE_DIR}/Cargo.lock"
        "${FUZZY_RANK_DIR}/Cargo.toml"
        ${FUZZY_RANK_SOURCES}
    WORKING_DIRECTORY "${FUZZY_BRIDGE_DIR}"
    COMMENT "Building fuzzy-rank Rust bridge"
    VERBATIM
)
add_custom_target(fuzzy_rank_bridge_build DEPENDS "${FUZZY_BRIDGE_LIBRARY}")

# ImGui Files
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/external
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
)

add_executable(audioplayer
    src/main.cpp
    src/app/application.cpp
    src/audio/audio_player.cpp
    src/audio/audio_process.cpp
    src/audio/crop_service.cpp
    src/core/time_utils.cpp
    src/platform/filesystem_utils.cpp
    src/platform/font_manager.cpp
    src/transcripts/transcript_database.cpp
    src/transcripts/transcript_fingerprint.cpp
    src/transcripts/transcript_loader.cpp
    src/transcripts/transcript_search.cpp
    src/ui/global_search_view.cpp
    src/ui/menu_bar.cpp
    src/ui/player_view.cpp
    src/ui/sidebar_view.cpp
    src/ui/ui.cpp
    ${IMGUI_SOURCES}
)

add_executable(test_transcript
    src/test_transcript.cpp
    src/transcripts/transcript_database.cpp
    src/transcripts/transcript_fingerprint.cpp
    src/transcripts/transcript_loader.cpp
    src/transcripts/transcript_search.cpp
)

add_dependencies(audioplayer fuzzy_rank_bridge_build)
add_dependencies(test_transcript fuzzy_rank_bridge_build)

target_link_libraries(audioplayer
    ${SDL2_LIBRARIES}
    dl
    pthread
    m
    sqlite3
    OpenSSL::Crypto
    "${FUZZY_BRIDGE_LIBRARY}"
)

target_link_libraries(test_transcript
    pthread
    dl
    m
    sqlite3
    OpenSSL::Crypto
    "${FUZZY_BRIDGE_LIBRARY}"
)

include("${CMAKE_CURRENT_SOURCE_DIR}/build-support/AppVersion.cmake")
app_version_enable(GUI_TARGETS audioplayer)
