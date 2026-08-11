# CMake generated Testfile for 
# Source directory: D:/Workspace/Auto Mixer
# Build directory: D:/Workspace/Auto Mixer/build-verify
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
if("${CTEST_CONFIGURATION_TYPE}" MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
  add_test(unit_tests "D:/Workspace/Auto Mixer/build-verify/Debug/auto-mixer-unit-tests.exe")
  set_tests_properties(unit_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/Workspace/Auto Mixer/CMakeLists.txt;108;add_test;D:/Workspace/Auto Mixer/CMakeLists.txt;0;")
elseif("${CTEST_CONFIGURATION_TYPE}" MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
  add_test(unit_tests "D:/Workspace/Auto Mixer/build-verify/Release/auto-mixer-unit-tests.exe")
  set_tests_properties(unit_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/Workspace/Auto Mixer/CMakeLists.txt;108;add_test;D:/Workspace/Auto Mixer/CMakeLists.txt;0;")
elseif("${CTEST_CONFIGURATION_TYPE}" MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
  add_test(unit_tests "D:/Workspace/Auto Mixer/build-verify/MinSizeRel/auto-mixer-unit-tests.exe")
  set_tests_properties(unit_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/Workspace/Auto Mixer/CMakeLists.txt;108;add_test;D:/Workspace/Auto Mixer/CMakeLists.txt;0;")
elseif("${CTEST_CONFIGURATION_TYPE}" MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
  add_test(unit_tests "D:/Workspace/Auto Mixer/build-verify/RelWithDebInfo/auto-mixer-unit-tests.exe")
  set_tests_properties(unit_tests PROPERTIES  _BACKTRACE_TRIPLES "D:/Workspace/Auto Mixer/CMakeLists.txt;108;add_test;D:/Workspace/Auto Mixer/CMakeLists.txt;0;")
else()
  add_test(unit_tests NOT_AVAILABLE)
endif()
