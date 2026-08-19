cmake_minimum_required(VERSION 3.19)
project(map_table VERSION 1.0.0 LANGUAGES CXX)

# 图表映射表编辑器：编辑 chart_map 的映射表（≤10 槽）
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)

# 显式列出源文件，这是开发规范
set(SOURCES
    src/map_table.cpp
)

set(HEADERS
    include/map_table.h
)

set(UI_FILES
    ui/map_table.ui
)

# 定义动态库（需要导出宏）
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS} ${UI_FILES})

# 包含目录
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/ui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 链接 Qt 与映射表组件（PUBLIC，头文件引用 chart_map_def.h）
target_link_libraries(${PROJECT_NAME} PUBLIC
    Qt::Core
    Qt::Widgets
    chart_map
)

target_compile_definitions(${PROJECT_NAME} PRIVATE MAP_TABLE_LIBRARY)
