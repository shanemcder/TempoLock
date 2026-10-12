cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")
project(TempoLock VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(FATAL_ERROR "JUCE is missing. Run ./setup.sh from the TempoLock directory first.")
endif()

add_subdirectory(JUCE)

juce_add_gui_app(TempoLock
    PRODUCT_NAME "TempoLock"
    COMPANY_NAME "TempoLock"
    BUNDLE_ID "com.tempolock.app"
)

juce_generate_juce_header(TempoLock)

target_sources(TempoLock PRIVATE Source/Main.cpp)

target_compile_definitions(TempoLock PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:TempoLock,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:TempoLock,JUCE_VERSION>"
)

target_link_libraries(TempoLock PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
