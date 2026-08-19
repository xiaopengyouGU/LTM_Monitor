cmake_minimum_required(VERSION 3.19)
project(protocol VERSION 1.0.0 LANGUAGES CXX)

# 协议解析组件：LTM / Modbus 共用环形缓冲，与介质无关
# 对外提供 LtmProtocol / ModbusProtocol（Pimpl），内部采用 RingBuffer
find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

qt_standard_project_setup()

set(SOURCES
    src/ring_buffer.cpp
    src/ltm_protocol.cpp
    src/modbus_protocol.cpp
    src/modbus_master.cpp
)

set(HEADERS
    include/ltm_protocol.h
    include/modbus_protocol.h
    include/modbus_master.h
    src/ring_buffer.h
)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC Qt::Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE PROTOCOL_LIBRARY)
