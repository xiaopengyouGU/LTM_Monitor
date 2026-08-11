cmake_minimum_required(VERSION 3.19)
project(chart VERSION 1.0.0 LANGUAGES CXX)

# 查找 Qt 组件，最低版本要求6.2
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Concurrent PrintSupport)

qt_standard_project_setup()
# 显式列出要构建的源文件，这是开发规范
set(SOURCES
    src/chart_manager.cpp
    src/data_importer.cpp
    src/data_exporter.cpp
    src/data_storage.cpp
    src/chart_controller.cpp
)

set(HEADERS
    include/chart_manager.h
    include/data_importer.h
    include/data_exporter.h
    include/data_storage.h
    include/chart_def.h
    include/chart_controller.h
    include/chart_manager_private.h
)

# 定义动态库（MinGW 默认导出所有符号，无需导出宏）
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS})

# 包含目录
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/QCustomPlot
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# 链接 Qt 库（PUBLIC 以便传递给使用 chart 的其他模块）
target_link_libraries(${PROJECT_NAME} PUBLIC
        Qt::Core
        Qt::Widgets
        Qt::Concurrent
        Qt::PrintSupport
)

target_compile_definitions(${PROJECT_NAME} PRIVATE CHART_LIBRARY QCUSTOMPLOT_USE_LIBRARY)

# QCustomPlot 使用预编译 Release 动态库（third_party/QCustomPlot/qcustomplot.dll），
# 避免每次构建都编译数万行的单文件，也绕开 MinGW Debug 下 COFF 段数超限问题。
# 重新生成预编译库（一次性）：
#   cmake -S third_party/QCustomPlot -B third_party/QCustomPlot/prebuilt_build \
#         -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=D:/Qt/6.5.11/mingw_64 -DCMAKE_BUILD_TYPE=Release
#   cmake --build third_party/QCustomPlot/prebuilt_build -j4
target_link_libraries(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/QCustomPlot/libqcustomplot.dll.a)

# 运行时需要 qcustomplot.dll 与 chart.dll 同目录
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/QCustomPlot/qcustomplot.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "复制预编译 qcustomplot.dll"
)

# ===== 单元/压力测试（默认关闭，只测公共接口 chart_manager.h；-DCHART_BUILD_TESTS=OFF 关闭）=====
option(CHART_BUILD_TESTS "Build chart module stress/unit tests" OFF)
if(CHART_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
