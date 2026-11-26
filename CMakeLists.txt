cmake_minimum_required(VERSION 3.25)

# vcpkg: configure with the "vs2022" preset (sets CMAKE_TOOLCHAIN_FILE from $VCPKG_ROOT). See BUILDING.md.
project(StemABPlayer VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# ------------------------------------------------------------------------------------------------
# Third-party sources (pinned)
# ------------------------------------------------------------------------------------------------
include(FetchContent)

# JUCE 8.x line (ADR 0001). 8.0.17-security is the latest 8.x tag (JUCE 9.0.x also exists).
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.17-security
    GIT_SHALLOW    TRUE)

# Catch2 v3 - keep its self-tests/docs/install rules out of our build.
set(CATCH_INSTALL_DOCS    OFF CACHE BOOL "" FORCE)
set(CATCH_INSTALL_EXTRAS  OFF CACHE BOOL "" FORCE)
set(CATCH_BUILD_TESTING   OFF CACHE BOOL "" FORCE)
set(CATCH_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(CATCH_DEVELOPMENT_BUILD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.16.1
    GIT_SHALLOW    TRUE)

# r8brain-free-src (MIT) - offline high-quality resampler (ADR 0004: used identically on A and B). It has no
# CMakeLists of its own, so we only fetch the sources (SOURCE_SUBDIR points at a non-existent folder so that
# MakeAvailable does not try add_subdirectory) and compile them in src/decode.
FetchContent_Declare(r8brain
    GIT_REPOSITORY https://github.com/avaneev/r8brain-free-src.git
    GIT_TAG        version-6.5
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  no-cmake-project)

FetchContent_MakeAvailable(JUCE Catch2 r8brain)

include(CTest)
enable_testing()
list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")   # provides include(Catch) / catch_discover_tests

# ------------------------------------------------------------------------------------------------
# Sharing JUCE modules between static libs, the GUI app and the test exe
# ------------------------------------------------------------------------------------------------
# JUCE module targets (juce::juce_core, ...) are INTERFACE libraries whose *.cpp implementation files
# are added to the SOURCES of every target that links them (docs/CMake API.md, "juce_add_module").
# If StemAB_Engine, StemAB_Analysis and StemAB_App each linked juce::juce_dsp, the module
# implementation would be compiled into every one of them -> duplicate definitions / silent ODR
# violations once the static libs are linked into one exe. So we split "headers" from "implementation":
#
#   StemAB_JuceHeaders  (INTERFACE) include dirs + compile definitions of the JUCE modules, mirrored
#                       from the module targets WITHOUT their sources. Linked PUBLIC by the static libs
#                       (Engine/Analysis), so they can #include <juce_...> but never compile JUCE.
#   StemAB_JuceModules  (INTERFACE) the real juce::juce_* module targets. Linked PRIVATE by each FINAL
#                       exe (StemAB_App, StemAB_Tests) exactly once, so every exe contains one copy of
#                       the module implementations built with the same definitions/flags.
#
# (This is the same idea as JUCE's own plugin "shared code" target / the pamplejuce SharedCode target,
# adapted so the middle layer can be static libs.)

function(_stemab_walk_juce_target tgt)
    if(NOT TARGET "${tgt}")
        return()
    endif()
    get_target_property(aliased "${tgt}" ALIASED_TARGET)
    if(aliased)
        set(tgt "${aliased}")
    endif()
    get_property(visited GLOBAL PROPERTY STEMAB_JUCE_VISITED)
    if("${tgt}" IN_LIST visited)
        return()
    endif()
    set_property(GLOBAL APPEND PROPERTY STEMAB_JUCE_VISITED "${tgt}")

    get_target_property(inc "${tgt}" INTERFACE_INCLUDE_DIRECTORIES)
    if(inc)
        set_property(GLOBAL APPEND PROPERTY STEMAB_JUCE_INCLUDES ${inc})
    endif()
    get_target_property(defs "${tgt}" INTERFACE_COMPILE_DEFINITIONS)
    if(defs)
        set_property(GLOBAL APPEND PROPERTY STEMAB_JUCE_DEFS ${defs})
    endif()
    get_target_property(libs "${tgt}" INTERFACE_LINK_LIBRARIES)
    if(libs)
        foreach(lib IN LISTS libs)
            if(NOT "${lib}" MATCHES "^\\$<")
                _stemab_walk_juce_target("${lib}")
            endif()
        endforeach()
    endif()
endfunction()

# stemab_add_juce_headers_target(<name> <juce_module>...)
function(stemab_add_juce_headers_target name)
    set_property(GLOBAL PROPERTY STEMAB_JUCE_VISITED "")
    set_property(GLOBAL PROPERTY STEMAB_JUCE_INCLUDES "")
    set_property(GLOBAL PROPERTY STEMAB_JUCE_DEFS "")
    foreach(module IN LISTS ARGN)
        _stemab_walk_juce_target("${module}")
    endforeach()
    get_property(includes GLOBAL PROPERTY STEMAB_JUCE_INCLUDES)
    get_property(defs     GLOBAL PROPERTY STEMAB_JUCE_DEFS)
    list(APPEND defs JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    list(REMOVE_DUPLICATES includes)
    list(REMOVE_DUPLICATES defs)

    add_library(${name} INTERFACE)
    target_include_directories(${name} INTERFACE ${includes})
    # Same global settings the final exes use, so JUCE class layouts match everywhere.
    target_compile_definitions(${name} INTERFACE
        ${defs}
        JUCE_STANDALONE_APPLICATION=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0)
endfunction()

stemab_add_juce_headers_target(StemAB_JuceHeaders juce::juce_audio_basics juce::juce_dsp)

add_library(StemAB_JuceModules INTERFACE)
target_link_libraries(StemAB_JuceModules INTERFACE
    juce::juce_audio_basics
    juce::juce_dsp
    juce::juce_recommended_config_flags)

# ------------------------------------------------------------------------------------------------
# ASIO SDK (optional). JUCE 8 also ships bundled ASIO headers, but we use the external Steinberg SDK
# so the driver interface matches the SDK we are licensed for.
# ------------------------------------------------------------------------------------------------
set(STEMAB_ASIO_SDK_DIR "C:/Projects/SDKs/asiosdk" CACHE PATH
    "Root of the Steinberg ASIO SDK (contains common/iasiodrv.h). Leave missing to build WASAPI-only.")

# ------------------------------------------------------------------------------------------------
add_subdirectory(src/engine)
add_subdirectory(src/analysis)
add_subdirectory(src/decode)
add_subdirectory(src/load)
add_subdirectory(src/app)
add_subdirectory(tests)
add_subdirectory(tools/StemAB_Probe)
