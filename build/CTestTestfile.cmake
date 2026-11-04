# CMake generated Testfile for 
# Source directory: /tmp/claude-0/p/plt-converter
# Build directory: /tmp/claude-0/p/plt-converter/build
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(core "/tmp/claude-0/p/plt-converter/build/test_core" "/tmp/claude-0/p/plt-converter/tests/data")
set_tests_properties(core PROPERTIES  _BACKTRACE_TRIPLES "/tmp/claude-0/p/plt-converter/CMakeLists.txt;25;add_test;/tmp/claude-0/p/plt-converter/CMakeLists.txt;0;")
add_test(batch "/tmp/claude-0/p/plt-converter/build/test_batch")
set_tests_properties(batch PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT "300" _BACKTRACE_TRIPLES "/tmp/claude-0/p/plt-converter/CMakeLists.txt;51;add_test;/tmp/claude-0/p/plt-converter/CMakeLists.txt;0;")
add_test(gui "/tmp/claude-0/p/plt-converter/build/test_gui" "/tmp/claude-0/p/plt-converter/tests/data")
set_tests_properties(gui PROPERTIES  ENVIRONMENT "QT_QPA_PLATFORM=offscreen" _BACKTRACE_TRIPLES "/tmp/claude-0/p/plt-converter/CMakeLists.txt;53;add_test;/tmp/claude-0/p/plt-converter/CMakeLists.txt;0;")
