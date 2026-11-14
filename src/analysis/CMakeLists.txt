add_library(StemAB_Analysis STATIC
    NullDepth.cpp
    NullDepth.h)

target_include_directories(StemAB_Analysis PUBLIC "${PROJECT_SOURCE_DIR}/src")

# JUCE headers only (juce_dsp); implementations are compiled once per final exe - see root CMakeLists.txt.
target_link_libraries(StemAB_Analysis
    PUBLIC  StemAB_JuceHeaders
    PRIVATE juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
