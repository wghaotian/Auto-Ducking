cmake_minimum_required(VERSION 3.15)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(AutoDucking VERSION 0.2.0 LANGUAGES CXX)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(AUTO_DUCKING_BUILD_TESTS "Build unit tests" ON)

add_library(auto_ducking_formatting STATIC
    src/Formatting.cpp
)
target_include_directories(auto_ducking_formatting PUBLIC src)
target_compile_features(auto_ducking_formatting PUBLIC cxx_std_17)

add_library(auto_ducking_core STATIC
    src/ActivityDetector.cpp
    src/CoreAudioMonitor.cpp
    src/DuckingController.cpp
    src/ProcessLoopbackCapture.cpp
)
target_include_directories(auto_ducking_core PUBLIC src)
target_compile_features(auto_ducking_core PUBLIC cxx_std_17)
target_compile_definitions(auto_ducking_core PUBLIC
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)
target_link_libraries(auto_ducking_core PUBLIC
    auto_ducking_formatting
    ole32
    mmdevapi
    uuid
)

add_executable(auto-ducking-diagnostics
    src/main.cpp
)
target_include_directories(auto-ducking-diagnostics PRIVATE src)
target_compile_features(auto-ducking-diagnostics PRIVATE cxx_std_17)
target_compile_definitions(auto-ducking-diagnostics PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)
target_link_libraries(auto-ducking-diagnostics PRIVATE
    auto_ducking_core
)

add_executable(auto-ducking-process-meter
    src/process_meter_main.cpp
)
target_include_directories(auto-ducking-process-meter PRIVATE src)
target_compile_features(auto-ducking-process-meter PRIVATE cxx_std_17)
target_compile_definitions(auto-ducking-process-meter PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)
target_link_libraries(auto-ducking-process-meter PRIVATE auto_ducking_core)

add_executable(auto-ducking-ui WIN32
    src/ui_main.cpp
    src/WaveformControl.cpp
    src/auto-ducking-ui.manifest
)
target_include_directories(auto-ducking-ui PRIVATE src)
target_compile_features(auto-ducking-ui PRIVATE cxx_std_17)
target_compile_definitions(auto-ducking-ui PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)
target_link_libraries(auto-ducking-ui PRIVATE
    auto_ducking_core
    comctl32
    uxtheme
)

if(MSVC)
    foreach(target auto_ducking_formatting auto_ducking_core auto-ducking-diagnostics auto-ducking-process-meter auto-ducking-ui)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endforeach()
endif()

if(AUTO_DUCKING_BUILD_TESTS)
    enable_testing()
    add_executable(auto-ducking-unit-tests
        tests/ActivityDetectorTests.cpp
        tests/DuckingControllerTests.cpp
        tests/FormattingTests.cpp
        tests/ProcessLoopbackCaptureTests.cpp
    )
    target_link_libraries(auto-ducking-unit-tests PRIVATE auto_ducking_core)
    target_compile_features(auto-ducking-unit-tests PRIVATE cxx_std_17)
    if(MSVC)
        target_compile_options(auto-ducking-unit-tests PRIVATE /W4 /permissive- /utf-8)
    endif()
    add_test(NAME unit_tests COMMAND auto-ducking-unit-tests)
endif()
