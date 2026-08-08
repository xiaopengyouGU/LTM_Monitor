cmake_minimum_required(VERSION 3.19)
project(data_map VERSION 1.0.0 LANGUAGES CXX)

# CAN 数据映射组件：DBC / 用户自定义协议解析 + 按位解码引擎
find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

qt_standard_project_setup()

# 显式列出要构建的源文件，这是开发规范
set(SOURCES
    src/data_map.cpp
    src/dbc_parser.cpp
    src/json_parser.cpp
)

set(HEADERS
    include/data_map.h
    include/data_map_def.h
    src/data_map_parser.h
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

# 链接 Qt 库（PUBLIC 以便传递给使用 data_map 的其他模块）
target_link_libraries(${PROJECT_NAME} PUBLIC Qt::Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE DATA_MAP_LIBRARY)
