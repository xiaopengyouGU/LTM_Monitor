cmake_minimum_required(VERSION 3.19)
project(record VERSION 1.0.0 LANGUAGES CXX)

# 查找 Qt 组件，最低版本要求6.2
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Sql Widgets)

qt_standard_project_setup()
# 显式列出要构建的源文件，这是开发规范
set(SOURCES
    src/log_worker.cpp
    src/record_manager.cpp
    src/record_worker.cpp
)

set(HEADERS
    include/log_worker.h 
    include/record_manager.h
    include/record_worker.h
    include/record_manager_private.h
)

# 使用了spdlog的自定义Formatter,须定义如下宏
add_definitions(-DSPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_TRACE)

# 定义动态库
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# 包含目录
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include>
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 链接 Qt 库（PUBLIC 以便传递给使用 chart 的其他模块）
target_link_libraries(${PROJECT_NAME} PUBLIC
        Qt::Core
        Qt::Sql
        Qt::Widgets
)

# 告诉编译器去哪里找 spdlog 的头文件
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/include
)

# 定义 SPDLOG_COMPILED_LIB 宏，切换到编译库模式（库模式下，编译时间从 30s 缩短到 3s）
target_compile_definitions(${PROJECT_NAME} PRIVATE SPDLOG_COMPILED_LIB)

# 链接 libspdlog.a 静态库
target_link_libraries(${PROJECT_NAME} PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/spdlog/lib/libspdlog.a
)

target_compile_definitions(${PROJECT_NAME} PRIVATE RECORD_LIBRARY)
