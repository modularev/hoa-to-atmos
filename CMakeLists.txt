cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

project(HOA2ADM VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_OSX_ARCHITECTURES)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

# Reuse the JUCE checkout that lives in beads-au-plugin.
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../beads-au-plugin/third_party/JUCE" CACHE PATH "Path to JUCE")
add_subdirectory(${JUCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/JUCE)

# ---------------------------------------------------------------------------
# Core: decoder + ADM BWF writer + conversion pipeline
# ---------------------------------------------------------------------------
# JUCE modules are compiled into each target that links them, so the core sources
# are added per target instead of going through a shared static library.
set(HOA_CORE_SOURCES
  Source/Core/SphericalHarmonics.cpp
  Source/Core/AtmosLayout.cpp
  Source/Core/AllRadDecoder.cpp
  Source/Core/AdmBwfWriter.cpp
  Source/Core/Converter.cpp
)
set(HOA_COMMON_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)

# ---------------------------------------------------------------------------
# GUI app
# ---------------------------------------------------------------------------
juce_add_gui_app(HOAtoAtmos
  PRODUCT_NAME "HOA to Atmos"
  COMPANY_NAME "arev"
  BUNDLE_ID "com.arev.hoa2adm"
)
target_sources(HOAtoAtmos PRIVATE Source/App/Main.cpp Source/App/MainComponent.cpp ${HOA_CORE_SOURCES})
target_compile_definitions(HOAtoAtmos PRIVATE ${HOA_COMMON_DEFS}
  JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:HOAtoAtmos,JUCE_PRODUCT_NAME>"
  JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:HOAtoAtmos,JUCE_VERSION>"
)
target_link_libraries(HOAtoAtmos PRIVATE juce::juce_audio_formats juce::juce_gui_extra
  PUBLIC juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Command-line tool
# ---------------------------------------------------------------------------
juce_add_console_app(hoa2adm PRODUCT_NAME "hoa2adm")
target_sources(hoa2adm PRIVATE Source/Cli/Main.cpp ${HOA_CORE_SOURCES})
target_compile_definitions(hoa2adm PRIVATE ${HOA_COMMON_DEFS})
target_link_libraries(hoa2adm PRIVATE juce::juce_audio_formats juce::juce_events
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
juce_add_console_app(hoa_tests PRODUCT_NAME "hoa_tests")
target_sources(hoa_tests PRIVATE Tests/CoreTests.cpp ${HOA_CORE_SOURCES})
target_compile_definitions(hoa_tests PRIVATE ${HOA_COMMON_DEFS})
target_link_libraries(hoa_tests PRIVATE juce::juce_audio_formats juce::juce_events
  PUBLIC juce::juce_recommended_config_flags)
enable_testing()
add_test(NAME core COMMAND hoa_tests)
