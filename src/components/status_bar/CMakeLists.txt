cmake_minimum_required(VERSION 3.19)
project(status_bar VERSION 1.0.0 LANGUAGES CXX)

# 状态栏组件：信息显示 + 时间刷新（Pimpl，供各 widget 直连注入）
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()

set(SOURCES
    src/status_bar.cpp
)

set(HEADERS
    include/status_bar.h
)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME} PUBLIC Qt::Core Qt::Widgets)

target_compile_definitions(${PROJECT_NAME} PRIVATE STATUS_BAR_LIBRARY)
