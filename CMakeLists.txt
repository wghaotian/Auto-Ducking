cmake_minimum_required(VERSION 3.15)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(AutoMixer VERSION 0.2.0 LANGUAGES CXX)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(AUTO_MIXER_BUILD_TESTS "Build unit tests" ON)

add_library(auto_mixer_formatting STATIC
    src/Formatting.cpp
)
target_include_directories(auto_mixer_formatting PUBLIC src)
target_compile_features(auto_mixer_formatting PUBLIC cxx_std_17)

add_library(auto_mixer_core STATIC
    src/ActivityDetector.cpp
    src/CoreAudioMonitor.cpp
    src/DuckingController.cpp
    src/ProcessLoopbackCapture.cpp
)
target_include_directories(auto_mixer_core PUBLIC src)
target_compile_features(auto_mixer_core PUBLIC cxx_std_17)
target_compile_definitions(auto_mixer_core PUBLIC
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)
target_link_libraries(auto_mixer_core PUBLIC
    auto_mixer_formatting
    ole32
    mmdevapi
    uuid
)

add_executable(auto-mixer-diagnostics
    src/main.cpp
)
target_include_directories(auto-mixer-diagnostics PRIVATE src)
target_compile_features(auto-mixer-diagnostics PRIVATE cxx_std_17)
target_compile_definitions(auto-mixer-diagnostics PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)
target_link_libraries(auto-mixer-diagnostics PRIVATE
    auto_mixer_core
)

add_executable(auto-mixer-process-meter
    src/process_meter_main.cpp
)
target_include_directories(auto-mixer-process-meter PRIVATE src)
target_compile_features(auto-mixer-process-meter PRIVATE cxx_std_17)
target_compile_definitions(auto-mixer-process-meter PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)
target_link_libraries(auto-mixer-process-meter PRIVATE auto_mixer_core)

add_executable(auto-mixer-ui WIN32
    src/ui_main.cpp
    src/WaveformControl.cpp
    src/auto-mixer-ui.manifest
)
target_include_directories(auto-mixer-ui PRIVATE src)
target_compile_features(auto-mixer-ui PRIVATE cxx_std_17)
target_compile_definitions(auto-mixer-ui PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)
target_link_libraries(auto-mixer-ui PRIVATE
    auto_mixer_core
    comctl32
    uxtheme
)

if(MSVC)
    foreach(target auto_mixer_formatting auto_mixer_core auto-mixer-diagnostics auto-mixer-process-meter auto-mixer-ui)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endforeach()
endif()

if(AUTO_MIXER_BUILD_TESTS)
    enable_testing()
    add_executable(auto-mixer-unit-tests
        tests/ActivityDetectorTests.cpp
        tests/DuckingControllerTests.cpp
        tests/FormattingTests.cpp
        tests/ProcessLoopbackCaptureTests.cpp
    )
    target_link_libraries(auto-mixer-unit-tests PRIVATE auto_mixer_core)
    target_compile_features(auto-mixer-unit-tests PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(auto-mixer-unit-tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME unit_tests COMMAND auto-mixer-unit-tests)
endif()
