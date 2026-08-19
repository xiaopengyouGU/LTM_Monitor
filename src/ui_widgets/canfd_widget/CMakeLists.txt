cmake_minimum_required(VERSION 3.19)
project(canfd_widget VERSION 1.0.0 LANGUAGES CXX)

# CAN-FD 调试界面：帧发送配置 + 帧接收表格
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

# 显式列出源文件，这是开发规范
set(SOURCES
    src/canfd_widget.cpp
    src/canfd_frame_model.cpp
)

set(HEADERS
    include/canfd_widget.h
    include/canfd_frame_model.h
)

set(UI_FILES
    ui/canfd_widget.ui
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

# 链接 Qt 与 CAN-FD 业务模块（PUBLIC，头文件引用 canfd_def.h）
target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt::Core
    Qt::Widgets
    canfd
    record
    uds_server
)

target_compile_definitions(${PROJECT_NAME} PRIVATE CANFD_WIDGET_LIBRARY)
