add_library(StemAB_Engine STATIC
    Crossfade.cpp
    Crossfade.h
    LiveNullDepth.cpp
    LiveNullDepth.h
    PlaybackEngine.cpp
    PlaybackEngine.h
    Session.cpp
    Session.h)

# Includes look like #include "engine/Crossfade.h".
target_include_directories(StemAB_Engine PUBLIC "${PROJECT_SOURCE_DIR}/src")

# JUCE headers only (juce_audio_basics, juce_dsp and their dependencies); the module implementations
# are compiled once per final exe via StemAB_JuceModules - see the root CMakeLists.txt.
# StemAB_Analysis: StemBuffers (Session.h) and nullDepthDbFromEnergies (Live Null Depth). Analysis never
# depends on Engine, so the graph stays acyclic.
target_link_libraries(StemAB_Engine
    PUBLIC  StemAB_JuceHeaders
            StemAB_Analysis
    PRIVATE juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
