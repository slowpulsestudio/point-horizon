cmake_minimum_required(VERSION 3.22)

project(JungleStretch VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(JungleStretch
    COMPANY_NAME "Slow Pulse Studio"
    BUNDLE_ID "com.slowpulsestudio.junglestretch"
    PLUGIN_MANUFACTURER_CODE Slps
    PLUGIN_CODE Jstr
    FORMATS VST3
    PRODUCT_NAME "Jungle Stretch"
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3/Slow Pulse Studio"
)

juce_generate_juce_header(JungleStretch)

target_sources(JungleStretch
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
)

target_compile_definitions(JungleStretch
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(JungleStretch
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
