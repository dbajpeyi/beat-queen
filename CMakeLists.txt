cmake_minimum_required(VERSION 3.22)

project(BeatQueen VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BEATQUEEN_BUILD_PLUGIN "Build the JUCE plugin and standalone app (downloads JUCE)" ON)
option(BEATQUEEN_BUILD_TESTS "Build the core tests and offline renderer" ON)

# ---------------------------------------------------------------------------
# Core: circuit simulator, MRK-1 instrumentation, rhythms, clock. Plain C++, no JUCE.
# ---------------------------------------------------------------------------
add_library(beatqueen_core STATIC
    core/src/DrumMachine.cpp
    core/src/circuit/Circuit.cpp
    core/src/mrk1/Instrumentation.cpp
    core/src/mrk1/Rhythms.cpp)
target_include_directories(beatqueen_core PUBLIC core/include)
if(MSVC)
    target_compile_options(beatqueen_core PRIVATE /W4)
else()
    target_compile_options(beatqueen_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BEATQUEEN_BUILD_TESTS)
    enable_testing()

    add_executable(beatqueen_core_tests tests/core_tests.cpp)
    target_link_libraries(beatqueen_core_tests PRIVATE beatqueen_core)
    add_test(NAME core_tests COMMAND beatqueen_core_tests)

    add_executable(beatqueen_render tools/render_wav.cpp)
    target_link_libraries(beatqueen_render PRIVATE beatqueen_core)
endif()

# ---------------------------------------------------------------------------
# Plugin (VST3, AU on macOS) + standalone app.
# ---------------------------------------------------------------------------
if(BEATQUEEN_BUILD_PLUGIN)
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)

    set(BEATQUEEN_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND BEATQUEEN_FORMATS AU)
    endif()

    juce_add_plugin(BeatQueen
        COMPANY_NAME "Beat Queen"
        PRODUCT_NAME "Beat Queen"
        BUNDLE_ID com.beatqueen.beatqueen
        PLUGIN_MANUFACTURER_CODE Btqn
        PLUGIN_CODE Bqk1
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
        FORMATS ${BEATQUEEN_FORMATS})

    target_sources(BeatQueen PRIVATE
        plugin/PluginProcessor.cpp
        plugin/PluginEditor.cpp
        plugin/PanelComponents.cpp)

    target_compile_definitions(BeatQueen PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(BeatQueen
        PRIVATE
            beatqueen_core
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()
