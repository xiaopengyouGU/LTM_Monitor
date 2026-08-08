cmake_minimum_required(VERSION 3.19)
project(serial VERSION 1.0.0 LANGUAGES CXX)

# 查找 Qt 组件，最低版本要求6.2
find_package(Qt6 6.2 REQUIRED COMPONENTS Core SerialPort)

qt_standard_project_setup()
# 显式列出要构建的源文件，这是开发规范
set(SOURCES
    src/serial_manager.cpp
    src/serial_worker.cpp
    src/serial_protocol.cpp
)

set(HEADERS
    include/serial_manager.h
    include/serial_worker.h
    include/serial_protocol.h
    include/serial_def.h
    include/serial_manager_private.h
)

# 定义动态库（需要导出宏）
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# 包含目录
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 链接 Qt 库（PUBLIC 以便传递给使用 serial 的其他模块）
target_link_libraries(${PROJECT_NAME} PUBLIC
        Qt::Core
        Qt::SerialPort
)

target_compile_definitions(${PROJECT_NAME} PRIVATE SERIAL_LIBRARY)
