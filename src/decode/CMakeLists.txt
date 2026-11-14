# FFmpeg comes from vcpkg (manifest mode, see /vcpkg.json). vcpkg's ffmpeg port installs a find-module
# wrapper that defines FFMPEG_INCLUDE_DIRS, FFMPEG_LIBRARY_DIRS and FFMPEG_LIBRARIES (full paths to the
# import libs, with optimized/debug variants, plus the system libs they need).
find_package(FFMPEG REQUIRED)

add_library(StemAB_Decode STATIC
    Decode.cpp
    Decode.h)

target_include_directories(StemAB_Decode PUBLIC "${PROJECT_SOURCE_DIR}/src")
target_include_directories(StemAB_Decode SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})

# PUBLIC so that every exe that links StemAB_Decode also links FFmpeg's import libs. Because the vcpkg
# toolchain's VCPKG_APPLOCAL_DEPS (default ON) post-build-copies the DLLs an exe actually imports, the
# exes get the FFmpeg DLLs beside them automatically.
target_link_libraries(StemAB_Decode PUBLIC ${FFMPEG_LIBRARIES})
