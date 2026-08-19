cmake_minimum_required(VERSION 3.19)
project(chart_map VERSION 1.0.0 LANGUAGES CXX)

# 图表映射组件：数据源信号 → 图表槽位（10 槽）的统一映射表
find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

qt_standard_project_setup()

# 显式列出要构建的源文件，这是开发规范
set(SOURCES
    src/chart_map.cpp
)

set(HEADERS
    include/chart_map.h
    include/chart_map_def.h
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

# 链接 Qt 库（PUBLIC 以便传递给使用 chart_map 的其他模块）
target_link_libraries(${PROJECT_NAME} PUBLIC Qt::Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE CHART_MAP_LIBRARY)
