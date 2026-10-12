cmake_minimum_required(VERSION 3.21)
project(LspAudioPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(LSPA_BUILD_TESTS "Build unit tests" ON)
option(LSPA_BUILD_TOOLS "Build diagnostic tools (lv2probe)" ON)

find_package(Threads REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)
find_package(PkgConfig REQUIRED)

pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
pkg_check_modules(ALSA    REQUIRED IMPORTED_TARGET alsa)
pkg_check_modules(LILV    REQUIRED IMPORTED_TARGET lilv-0)
pkg_check_modules(SUIL    REQUIRED IMPORTED_TARGET suil-0)
pkg_check_modules(X11     REQUIRED IMPORTED_TARGET x11)

# ---------------------------------------------------------------------------
# lspa_core: session/persistence + audio engine (no Qt dependency)
# ---------------------------------------------------------------------------
add_library(lspa_core STATIC
  src/core/Logger.cpp
  src/core/Paths.cpp
  src/core/Session.cpp
  src/engine/SndfileDecoder.cpp
  src/engine/Player.cpp
  src/engine/EffectChain.cpp
  src/engine/AudioEngine.cpp
  src/engine/AlsaOutput.cpp
  src/engine/WavWriter.cpp
)
target_include_directories(lspa_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(lspa_core
  PUBLIC Threads::Threads Qt6::Core
  PRIVATE PkgConfig::SNDFILE PkgConfig::ALSA
)

# ---------------------------------------------------------------------------
# lspa_plugins: plugin host abstraction + LV2 (Lilv + Suil) adapter
# ---------------------------------------------------------------------------
add_library(lspa_plugins STATIC
  src/plugins/PluginCatalog.cpp
  src/plugins/Lv2Host.cpp
  src/plugins/Lv2Instance.cpp
  src/plugins/Lv2Worker.cpp
  src/plugins/Lv2UiHost.cpp
  src/plugins/UridMap.cpp
)
target_include_directories(lspa_plugins PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(lspa_plugins
  PUBLIC lspa_core
  PRIVATE PkgConfig::LILV PkgConfig::SUIL PkgConfig::X11 Qt6::Core Qt6::Gui Qt6::Widgets
)

# ---------------------------------------------------------------------------
# Application (Qt Widgets UI)
# ---------------------------------------------------------------------------
add_executable(lsp-audio-player
  src/main.cpp
  src/ui/MainWindow.cpp
  src/ui/PlayerPanel.cpp
  src/ui/RackView.cpp
  src/ui/RackCard.cpp
  src/ui/PluginBrowser.cpp
  src/ui/PluginEditor.cpp
  src/ui/SessionController.cpp
  src/ui/widgets/Knob.cpp
  src/ui/widgets/AudioSpectrumVisualizer.cpp
  src/ui/widgets/MasterVolumeDial.cpp
  src/ui/widgets/Waveform.cpp
  src/ui/widgets/Icons.cpp
  src/ui/widgets/AlbumArt.cpp
  src/ui/widgets/LevelMeter.cpp
  src/ui/theme/Theme.cpp
  src/ui/resources/material.qrc
)
target_link_libraries(lsp-audio-player
  PRIVATE lspa_core lspa_plugins Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg
)

# ---------------------------------------------------------------------------
# Tools & tests
# ---------------------------------------------------------------------------
if(LSPA_BUILD_TOOLS)
  add_executable(lv2probe tools/lv2probe.cpp)
  target_link_libraries(lv2probe PRIVATE lspa_plugins)
endif()

if(LSPA_BUILD_TESTS)
  enable_testing()
  add_executable(lspa_tests
    tests/TestMain.cpp
    tests/TestPluginCatalog.cpp
    tests/TestRingBuffer.cpp
    tests/TestFade.cpp
    tests/TestSession.cpp
    tests/TestEngineRender.cpp
  )
  target_link_libraries(lspa_tests PRIVATE lspa_core lspa_plugins)
  add_test(NAME lspa_tests COMMAND lspa_tests)
endif()
