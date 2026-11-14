# FFmpeg comes from vcpkg (manifest mode, see /vcpkg.json). vcpkg's ffmpeg port installs a find-module
# wrapper that defines FFMPEG_INCLUDE_DIRS, FFMPEG_LIBRARY_DIRS and FFMPEG_LIBRARIES (full paths to the
# import libs, with optimized/debug variants, plus the system libs they need).
find_package(FFMPEG REQUIRED)

# r8brain-free-src (fetched in the root CMakeLists.txt, MIT) is a header-only library apart from r8bbase.cpp,
# which defines its global tables. Built as its own tiny lib so our warning flags don't apply to third-party code.
add_library(StemAB_R8brain STATIC "${r8brain_SOURCE_DIR}/r8bbase.cpp")
target_include_directories(StemAB_R8brain SYSTEM PUBLIC "${r8brain_SOURCE_DIR}")
set_target_properties(StemAB_R8brain PROPERTIES FOLDER "third_party")

add_library(StemAB_Decode STATIC
    Decode.cpp
    Decode.h
    Resample.cpp)

target_include_directories(StemAB_Decode PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_include_directories(StemAB_Decode SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})

# JUCE headers only (juce_audio_basics for AudioBuffer); implementations are compiled once per final exe via
# StemAB_JuceModules - see the root CMakeLists.txt. FFmpeg and r8brain headers stay private to the .cpp files.
target_link_libraries(StemAB_Decode
    PUBLIC  StemAB_JuceHeaders
    PRIVATE StemAB_R8brain
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

# PUBLIC so that every exe that links StemAB_Decode also links FFmpeg's import libs. Because the vcpkg
# toolchain's VCPKG_APPLOCAL_DEPS (default ON) post-build-copies the DLLs an exe actually imports, the
# exes get the FFmpeg DLLs beside them automatically.
target_link_libraries(StemAB_Decode PUBLIC ${FFMPEG_LIBRARIES})
