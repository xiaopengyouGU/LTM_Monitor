cmake_minimum_required(VERSION 3.19)
project(canfd VERSION 1.0.0 LANGUAGES CXX)

# CAN-FD 分析仪二次开发框架模块（ZCAN_USBCANFD_200U / ControlCANFD）
find_package(Qt6 6.2 REQUIRED COMPONENTS Core)

qt_standard_project_setup()

# 显式列出要构建的源文件，这是开发规范
set(SOURCES
    src/canfd_manager.cpp
    src/canfd_worker.cpp
    src/canfd_controller.cpp
)

set(HEADERS
    include/canfd_manager.h
    include/canfd_worker.h
    include/canfd_controller.h
    include/canfd_def.h
    include/canfd_manager_private.h
    include/canfd.h
)

# 定义动态库（需要导出宏）
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# 包含目录：厂商 SDK 仅对 canfd_controller.cpp 可见（PRIVATE）
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zcan/include
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 链接 Qt 库与厂商导入库
target_link_libraries(${PROJECT_NAME}
    PUBLIC  Qt::Core
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zcan/lib/libControlCANFD.a
)

target_compile_definitions(${PROJECT_NAME} PRIVATE CANFD_LIBRARY)

# 第三方库路径设置（提前暴露问题）
set(ZCAN_IMPLIB ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zcan/lib/libControlCANFD.a)
set(ZCAN_DLL   ${CMAKE_CURRENT_SOURCE_DIR}/third_party/zcan/ControlCANFD.dll)

if(NOT EXISTS ${ZCAN_IMPLIB})
    message(FATAL_ERROR "ControlCANFD import library not found: ${ZCAN_IMPLIB}")
endif()
if(NOT EXISTS ${ZCAN_DLL})
    message(WARNING "ControlCANFD.dll not found: ${ZCAN_DLL}, runtime may fail")
endif()

# 构建后把 DLL 拷贝到输出目录，便于运行时查找
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${ZCAN_DLL} $<TARGET_FILE_DIR:${PROJECT_NAME}>
    COMMENT "Copying ControlCANFD.dll to output directory"
)
