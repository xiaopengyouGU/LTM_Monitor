cmake_minimum_required(VERSION 3.19)
project(qcustomplot_prebuilt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# 预编译 QCustomPlot 为 Release 动态库（一次性执行，产物留在本目录）：
#   cmake -S . -B prebuilt_build -DCMAKE_PREFIX_PATH=D:/Qt/6.5.11/mingw_64 -DCMAKE_BUILD_TYPE=Release
#   cmake --build prebuilt_build -j4
# 1) 主工程不再每次构建都编译这个数万行的单文件，增量构建快很多；
# 2) 绕开 MinGW Debug 下 COFF 段数超限（too many sections / file too big）。
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets PrintSupport)

add_library(qcustomplot SHARED
    qcustomplot.cpp
    qcustomplot.h
)

target_include_directories(qcustomplot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(qcustomplot
    PRIVATE QCUSTOMPLOT_COMPILE_LIBRARY
    PUBLIC  QCUSTOMPLOT_USE_LIBRARY
)
target_link_libraries(qcustomplot PUBLIC Qt6::Core Qt6::Widgets Qt6::PrintSupport)

set_target_properties(qcustomplot PROPERTIES
    OUTPUT_NAME qcustomplot
    PREFIX ""
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
