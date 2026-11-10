# Lodestone plugin targets (VST3 + CLAP + Standalone; AU added automatically on macOS).

# Embedded font (SIL OFL 1.1, see resources/fonts/OFL-Barlow.txt), so the UI
# renders identically on every OS instead of using each system's default.
juce_add_binary_data(LodestoneAssets
    NAMESPACE LodestoneAssets
    SOURCES
        ${PROJECT_SOURCE_DIR}/resources/fonts/BarlowSemiCondensed-Medium.ttf
        ${PROJECT_SOURCE_DIR}/resources/fonts/BarlowSemiCondensed-SemiBold.ttf)
set_target_properties(LodestoneAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(LODESTONE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND LODESTONE_FORMATS AU)
endif()

juce_add_plugin(Lodestone
    PRODUCT_NAME             "Lodestone"
    COMPANY_NAME             "LowKei Audio"
    BUNDLE_ID                "com.lowkeiaudio.lodestone"
    PLUGIN_MANUFACTURER_CODE Lwka          # 4 chars, at least one uppercase
    PLUGIN_CODE              Lstn          # unique per plugin; never change once shipped
    FORMATS                  ${LODESTONE_FORMATS}
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         TRUE        # MIDI Mode: scale, melody or harmony from held notes
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES          Fx "Pitch Shift"
    AU_MAIN_TYPE             kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD  FALSE)        # CI and local builds never touch system folders

# CLAP wrapper (FL Studio, Bitwig, Reaper). The ID is stored in every host
# project that uses the plugin, so never change it once shipped.
if(LODESTONE_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET Lodestone
        CLAP_ID       "com.lowkeiaudio.lodestone"
        CLAP_FEATURES audio-effect pitch-correction pitch-shifter)
endif()

# Shared with the headless checker (tools/Check.cpp).
set(LODESTONE_PLUGIN_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/presets/FactoryPresets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/presets/PresetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/ui/LodestoneLookAndFeel.cpp
    PARENT_SCOPE)

target_sources(Lodestone PRIVATE
    Parameters.cpp
    PluginProcessor.cpp
    PluginEditor.cpp
    presets/FactoryPresets.cpp
    presets/PresetManager.cpp
    ui/LodestoneLookAndFeel.cpp)

target_compile_definitions(Lodestone PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_VST3_EMULATE_MIDI_CC_WITH_PARAMETERS=0   # MIDI is for notes only: no 2048 hidden CC parameters in the DAW
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Lodestone
    PRIVATE
        lodestone_dsp
        LodestoneAssets
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
