cmake_minimum_required(VERSION 3.22)

project(Lodestone VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: link the C++ runtime statically so the plugin never depends on a
# Visual C++ Redistributable being installed on the user's machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(LODESTONE_BUILD_PLUGIN "Build the VST3 / CLAP / Standalone plugin (needs JUCE)" ON)
option(LODESTONE_BUILD_CLAP   "Add the CLAP format (clap-juce-extensions)"           ON)
option(LODESTONE_BUILD_TESTS  "Build the offline DSP tests"                     ON)

include(FetchContent)

# ---------------------------------------------------------------------------
# DSP core: header-only, plain C++, no JUCE. Shared by the plugin and tests.
add_library(lodestone_dsp INTERFACE)
target_include_directories(lodestone_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
if(LODESTONE_BUILD_PLUGIN)
    # Pinned for reproducible builds. Local override:
    #   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.3
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)

    # JUCE has no built-in CLAP, so CLAP comes from clap-juce-extensions.
    # Pinned to the same commit as the rest of the LowKei Audio family.
    # Local override: -DFETCHCONTENT_SOURCE_DIR_CLAP-JUCE-EXTENSIONS=/path
    if(LODESTONE_BUILD_CLAP)
        FetchContent_Declare(clap-juce-extensions
            GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
            GIT_TAG        7adee3a1bd4684d4caa5601100e364abccff4b4d
            GIT_SUBMODULES_RECURSE ON)
        FetchContent_MakeAvailable(clap-juce-extensions)
    endif()

    add_subdirectory(src)

    # Headless checker: UI snapshots, state checks and audio through every factory preset.
    # Compiles the plugin's own sources (not the VST3 wrapper) as a plain app.
    juce_add_console_app(lodestone_check PRODUCT_NAME "lodestone_check")
    target_sources(lodestone_check PRIVATE tools/Check.cpp ${LODESTONE_PLUGIN_SOURCES})
    target_include_directories(lodestone_check PRIVATE src)
    target_compile_definitions(lodestone_check PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        JucePlugin_Name="Lodestone"
        JucePlugin_VersionString="${PROJECT_VERSION}")
    target_link_libraries(lodestone_check PRIVATE lodestone_dsp LodestoneAssets juce::juce_audio_utils
                                                   juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
if(LODESTONE_BUILD_TESTS)
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.16.0
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(Catch2)

    enable_testing()
    add_executable(lodestone_tests tests/DspTests.cpp)
    target_link_libraries(lodestone_tests PRIVATE lodestone_dsp Catch2::Catch2WithMain)
    add_test(NAME dsp COMMAND lodestone_tests)
endif()
