juce_add_gui_app(StemAB_App
    PRODUCT_NAME "Stem AB Player"
    COMPANY_NAME "StemAB"
    VERSION      ${PROJECT_VERSION})

target_sources(StemAB_App PRIVATE
    Main.cpp
    MainComponent.cpp
    MainComponent.h
    PlatformWindows.cpp
    PlatformWindows.h
    Theme.h)

target_compile_definitions(StemAB_App PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:StemAB_App,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:StemAB_App,JUCE_VERSION>")

# ASIO: only if the Steinberg SDK is present. JUCE's ASIO code includes <iasiodrv.h>, which lives in
# <sdk>/common/ (it pulls in asio.h and asiosys.h from the same folder).
if(EXISTS "${STEMAB_ASIO_SDK_DIR}/common/iasiodrv.h")
    message(STATUS "StemAB: ASIO SDK found at ${STEMAB_ASIO_SDK_DIR} - ASIO enabled")
    target_compile_definitions(StemAB_App PRIVATE
        JUCE_ASIO=1
        JUCE_ASIO_USE_EXTERNAL_SDK=1)
    target_include_directories(StemAB_App PRIVATE "${STEMAB_ASIO_SDK_DIR}/common")
else()
    message(WARNING "ASIO SDK not found at '${STEMAB_ASIO_SDK_DIR}' (expected common/iasiodrv.h) - "
                    "building WASAPI-only. Set -DSTEMAB_ASIO_SDK_DIR=<path> to enable ASIO.")
endif()

# JUCE modules are compiled into this exe (once) through StemAB_JuceModules + the explicit modules below.
target_link_libraries(StemAB_App
    PRIVATE
        StemAB_Engine
        StemAB_Analysis
        StemAB_Decode
        StemAB_JuceModules
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_devices
        dwmapi
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# FFmpeg DLLs: vcpkg's toolchain adds a post-build "applocal" step to every add_executable() (including
# the one inside juce_add_gui_app) that copies the DLLs the exe imports next to it. The exe imports
# FFmpeg because MainComponent calls stemab::ffmpegVersionString().
