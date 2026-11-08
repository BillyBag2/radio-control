add_executable(radio-control-test radio-control-test.cpp)
target_link_libraries(radio-control-test PRIVATE radio_control)
target_include_directories(radio-control-test PRIVATE "${PROJECT_SOURCE_DIR}/lib/src")
add_test(NAME radio-protocol COMMAND radio-control-test)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME simulated-radio COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/simulated-radio.py" "$<TARGET_FILE:radio-control>")
    set_tests_properties(simulated-radio PROPERTIES TIMEOUT 20)
endif()
