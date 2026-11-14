# StemAB_Probe: headless console tool that decodes Original/Stem File pairs and prints Alignment and
# Null Depth numbers. Like StemAB_Tests it links StemAB_JuceModules exactly once (JUCE implementations);
# StemAB_Decode brings FFmpeg, whose DLLs vcpkg's applocal post-build step copies beside the exe.
add_executable(StemAB_Probe Probe.cpp)

target_link_libraries(StemAB_Probe PRIVATE
    StemAB_Decode
    StemAB_Analysis
    StemAB_JuceModules)

# With no arguments the probe runs the three test pairs from "Audio Files/" and "Stem Files/" here.
target_compile_definitions(StemAB_Probe PRIVATE STEMAB_REPO_DIR="${PROJECT_SOURCE_DIR}")
set_target_properties(StemAB_Probe PROPERTIES FOLDER "tools")
