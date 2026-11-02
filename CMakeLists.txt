cmake_minimum_required(VERSION 3.22)

# Static MSVC runtime so the .exe runs without installing the VC++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

project(Transport VERSION 0.4.1 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: use a local checkout if JUCE_DIR is given, otherwise fetch it.
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(TRANSPORT_CORE_SOURCES
    src/core/LoudnessMeter.cpp
    src/core/TempoTracker.cpp
    src/core/KeyDetector.cpp
    src/core/Effects.cpp
    src/core/Analyzer.cpp)

# ---------------------------------------------------------------- app
juce_add_gui_app(Transport
    PRODUCT_NAME "Transport"
    COMPANY_NAME "Transport"
    BUNDLE_ID "app.transport.player"
    VERSION ${PROJECT_VERSION}
    ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon.png
    MICROPHONE_PERMISSION_ENABLED FALSE
    DOCUMENT_EXTENSIONS mp3 wav flac aiff aif ogg m4a mp4 m4v mov)

target_sources(Transport PRIVATE
    ${TRANSPORT_CORE_SOURCES}
    src/app/Main.cpp
    src/app/AudioEngine.cpp
    src/app/TrackInfo.cpp
    src/app/Visualizers.cpp
    src/app/MorePlugins.cpp
    src/app/EffectPlugins.cpp
    src/app/SlotGrid.cpp
    src/app/PluginBrowser.cpp
    src/app/HeaderView.cpp
    src/app/WaveformView.cpp
    src/app/TitleBar.cpp
    src/app/TaskbarPlayer.cpp
    src/app/DefaultPlayer.cpp
    src/app/VideoPlayer.cpp
    src/app/VideoView.cpp
    src/app/media/MediaCommon.cpp
    src/app/MainComponent.cpp)

# ---------------------------------------------------------------- video backend (the platform's own decoders)
if(WIN32)
    target_sources(Transport PRIVATE src/app/media/MediaWindows.cpp)
    target_link_libraries(Transport PRIVATE mfplat mfreadwrite mfuuid)
    set(TRANSPORT_VIDEO "Media Foundation")
elseif(APPLE)
    target_sources(Transport PRIVATE src/app/media/MediaMac.mm)
    set_source_files_properties(src/app/media/MediaMac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(Transport PRIVATE "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo")
    set(TRANSPORT_VIDEO "AVFoundation")
else()
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libswscale libswresample libavutil)
    endif()
    if(FFMPEG_FOUND)
        target_sources(Transport PRIVATE src/app/media/MediaFFmpeg.cpp)
        target_link_libraries(Transport PRIVATE PkgConfig::FFMPEG)
        set(TRANSPORT_VIDEO "FFmpeg ${FFMPEG_libavformat_VERSION}")
    else()
        target_sources(Transport PRIVATE src/app/media/MediaNone.cpp)
        set(TRANSPORT_VIDEO "none (install the FFmpeg development packages for video)")
    endif()
endif()
message(STATUS "Transport video playback: ${TRANSPORT_VIDEO}")

if(APPLE)
    target_link_libraries(Transport PRIVATE "-framework CoreServices")
endif()
if(WIN32)
    target_link_libraries(Transport PRIVATE dwmapi comctl32 ole32 uuid shell32)
endif()

target_include_directories(Transport PRIVATE src)

target_compile_definitions(Transport PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:Transport,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:Transport,JUCE_VERSION>")

target_link_libraries(Transport PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- tests
juce_add_console_app(TransportTests PRODUCT_NAME "TransportTests")

target_sources(TransportTests PRIVATE ${TRANSPORT_CORE_SOURCES} tests/Tests.cpp)
target_include_directories(TransportTests PRIVATE src)
target_compile_definitions(TransportTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(TransportTests PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME core COMMAND TransportTests)
