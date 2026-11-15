include(Catch)   # from ${catch2_SOURCE_DIR}/extras (added to CMAKE_MODULE_PATH in the root CMakeLists.txt)

add_executable(StemAB_Tests
    CrossfadeTests.cpp
    PlaybackEngineTests.cpp
    OutputResamplerTests.cpp
    DecodeTests.cpp
    ResampleTests.cpp
    AnalysisTests.cpp
    AlignmentTests.cpp
    LoadTests.cpp)

# StemAB_JuceModules compiles the JUCE module implementations into this exe (once); the static libs
# only see JUCE headers. StemAB_Decode brings FFmpeg, whose DLLs vcpkg's applocal step copies beside
# the exe so the tests can run.
target_link_libraries(StemAB_Tests PRIVATE
    StemAB_Engine
    StemAB_Analysis
    StemAB_Decode
    StemAB_Load
    StemAB_JuceModules
    Catch2::Catch2WithMain)

# Real-file tests look for "Audio Files/" and "Stem Files/" under this folder and SKIP when they are absent.
target_compile_definitions(StemAB_Tests PRIVATE STEMAB_TEST_DATA_DIR="${PROJECT_SOURCE_DIR}")

# PRE_TEST discovery: test names are listed when ctest runs, not as a post-build step of the exe, so
# discovery cannot race the DLL copy.
catch_discover_tests(StemAB_Tests
    DISCOVERY_MODE PRE_TEST
    WORKING_DIRECTORY "$<TARGET_FILE_DIR:StemAB_Tests>")
