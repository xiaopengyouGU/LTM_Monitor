cmake_minimum_required(VERSION 3.19)
project(uds_server VERSION 1.0.0 LANGUAGES CXX)

# UDS 升级协议引擎组件：硬件无关（无 UI、无传输层依赖），供 UdsWidget 等界面绑定
find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

qt_standard_project_setup()

# 显式列出源文件，这是开发规范
set(SOURCES
    src/uds_server.cpp
)

set(HEADERS
    include/uds_server.h
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

# 链接 Qt 库（PUBLIC 以便传递给使用 uds_server 的其他模块）
target_link_libraries(${PROJECT_NAME} PUBLIC Qt::Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE UDS_SERVER_LIBRARY)
