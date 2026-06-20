cmake_minimum_required(VERSION 3.22)

project(juce-arranger VERSION 0.0.1)

# Add JUCE from the sibling directory
add_subdirectory(../JUCE juce_build EXCLUDE_FROM_ALL)

juce_add_gui_app(juce-arranger
    PRODUCT_NAME "juce-arranger")

set_target_properties(juce-arranger PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}")

target_sources(juce-arranger
    PRIVATE
        Main.cpp
        MainComponent.cpp
        TimelineComponent.cpp
        BarGridComponent.cpp
        ArrangementView.cpp
        ArrangementJson.h)

target_compile_definitions(juce-arranger
    PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:juce-arranger,JUCE_PRODUCT_NAME>"
        JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:juce-arranger,JUCE_VERSION>")

target_link_libraries(juce-arranger
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
