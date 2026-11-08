set(RADIO_CONTROL_SOURCES
    src/radio-control.cpp
    src/protocol.cpp
    src/socket-transport.cpp
)

if(COMMAND idf_component_register)
    idf_component_register(SRCS ${RADIO_CONTROL_SOURCES}
        INCLUDE_DIRS include PRIV_REQUIRES lwip)
    target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_11)
    target_compile_options(${COMPONENT_LIB} PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion
        -Wshadow -Wformat=2 -Wundef)
else()
    add_library(radio_control STATIC ${RADIO_CONTROL_SOURCES})
    target_include_directories(radio_control PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_features(radio_control PUBLIC cxx_std_11)
    set_target_properties(radio_control PROPERTIES CXX_EXTENSIONS OFF)
endif()
