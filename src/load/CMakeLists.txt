add_library(StemAB_Load STATIC
    AlignmentCache.cpp
    AlignmentCache.h
    PairLoader.cpp
    PairLoader.h)

# Includes look like #include "load/PairLoader.h".
target_include_directories(StemAB_Load PUBLIC "${PROJECT_SOURCE_DIR}/src")

# Staged, cancelable loading of a pair (Decode -> Session -> Alignment -> upgrade -> Track Null Depth) and
# the persistent alignment cache. No audio device or GUI code, so the tests and the probe can drive it.
# JUCE headers only (juce_core for ThreadPool, File and JSON); implementations come from each exe.
target_link_libraries(StemAB_Load
    PUBLIC  StemAB_JuceHeaders
            StemAB_Engine
            StemAB_Analysis
            StemAB_Decode
    PRIVATE juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
