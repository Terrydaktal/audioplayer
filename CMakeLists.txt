cmake_minimum_required(VERSION 3.10)
project(AudioPlayer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SDL2
find_package(SDL2 REQUIRED)

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
    src/audio_player.cpp
    src/transcript.cpp
    ${IMGUI_SOURCES}
)

add_executable(test_transcript
    src/test_transcript.cpp
    src/transcript.cpp
)

target_link_libraries(audioplayer
    ${SDL2_LIBRARIES}
    dl
    pthread
    m
)

target_link_libraries(test_transcript
    pthread
)
