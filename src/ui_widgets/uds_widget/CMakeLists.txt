cmake_minimum_required(VERSION 3.19)
project(uds_widget VERSION 1.0.0 LANGUAGES CXX)

# UDS 固件升级界面：UI + CAN-FD 传输绑定（协议引擎在 components/uds_server）
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

# 显式列出源文件，这是开发规范
set(SOURCES
    src/uds_widget.cpp
)

set(HEADERS
    include/uds_widget.h
)

set(UI_FILES
    ui/uds_widget.ui
)

# 定义动态库（需要导出宏）
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS} ${UI_FILES})

# 包含目录
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
        ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/record/include
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 链接 Qt 与业务模块（PUBLIC，头文件引用 canfd_def.h / serial.h / record.h / uds_server.h）
target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt::Core
    Qt::Widgets
    canfd
    serial
    record
    uds_server
    protocol
)

target_compile_definitions(${PROJECT_NAME} PRIVATE UDS_WIDGET_LIBRARY)
