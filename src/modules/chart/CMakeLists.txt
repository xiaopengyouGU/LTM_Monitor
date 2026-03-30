cmake_minimum_required(VERSION 3.19)
project(chart VERSION 1.0.0 LANGUAGES CXX)

# 查找 Qt 组件，最低版本要求6.2
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Charts Concurrent)

qt_standard_project_setup()
# 显式列出要构建的源文件，这是开发规范
set(SOURCES
    src/chart_manager.cpp
    src/chart_view.cpp
    src/data_importer.cpp
    src/data_exporter.cpp
    src/data_storage.cpp
)

set(HEADERS
    include/chart_manager.h
    include/chart_view.h
    include/data_importer.h
    include/data_exporter.h
    include/data_storage.h
    include/chart_def.h
    include/chart_manager_private.h
)

# 定义动态库（MinGW 默认导出所有符号，无需导出宏）
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# 包含目录
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 链接 Qt 库（PUBLIC 以便传递给使用 chart 的其他模块）
target_link_libraries(${PROJECT_NAME} PUBLIC
        Qt::Core
        Qt::Widgets
        Qt::Charts
        Qt::Concurrent
)

target_compile_definitions(${PROJECT_NAME} PRIVATE CHART_LIBRARY)
