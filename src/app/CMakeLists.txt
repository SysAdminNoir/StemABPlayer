juce_add_gui_app(StemAB_App
    PRODUCT_NAME "Stem AB Player"
    COMPANY_NAME "StemAB"
    VERSION      ${PROJECT_VERSION})

target_sources(StemAB_App PRIVATE
    AppSettings.cpp
    AppSettings.h
    Main.cpp
    MainComponent.cpp
    MainComponent.h
    PlatformWindows.cpp
    PlatformWindows.h
    SessionLoader.cpp
    SessionLoader.h
    SessionState.cpp
    SessionState.h
    Theme.h
    WaveformBuilder.cpp
    WaveformBuilder.h
    ui/ControlDeck.cpp
    ui/ControlDeck.h
    ui/Format.h
    ui/HeaderBar.cpp
    ui/HeaderBar.h
    ui/LevelMeter.cpp
    ui/LevelMeter.h
    ui/MeterBridge.cpp
    ui/MeterBridge.h
    ui/PlayheadOverlay.cpp
    ui/PlayheadOverlay.h
    ui/PreferencesPanel.cpp
    ui/PreferencesPanel.h
    ui/TimeRuler.cpp
    ui/TimeRuler.h
    ui/TrackArea.cpp
    ui/TrackArea.h
    ui/TrackHeader.cpp
    ui/TrackHeader.h
    ui/WaveformLane.cpp
    ui/WaveformLane.h
    ui/Widgets.cpp
    ui/Widgets.h)

# STEMAB_SOURCE_DIR: the file choosers open in the repo's "Audio Files/" and "Stem Files/" when present.
target_compile_definitions(StemAB_App PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    STEMAB_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
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
        StemAB_Load
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
