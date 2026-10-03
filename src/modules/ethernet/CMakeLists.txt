cmake_minimum_required(VERSION 3.19)
project(ethernet VERSION 1.0.0 LANGUAGES CXX)

# 网口传输模块：当前只提供 TCP Client，协议解析全部由 DataHub 负责
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Network)

qt_standard_project_setup()

set(SOURCES
    src/ethernet_manager.cpp
    src/ethernet_worker.cpp
)

set(HEADERS
    include/ethernet.h
    include/ethernet_def.h
    include/ethernet_manager.h
    include/ethernet_manager_private.h
    include/ethernet_worker.h
)

add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC Qt::Core Qt::Network
)

target_compile_definitions(${PROJECT_NAME} PRIVATE ETHERNET_LIBRARY)