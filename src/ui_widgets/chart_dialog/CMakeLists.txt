# ui_widgets/main_window/CMakeLists.txt
project(chart_dialog LANGUAGES CXX)

# 查找 Qt 组件，最低版本要求6.2
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Charts)

qt_standard_project_setup()
set(CMAKE_AUTORCC ON)                   #编译资源文件，显示图标
# 显式列出源文件，这是开发规范
set(SOURCES src/chart_dialog.cpp)
set(HEADERS include/chart_dialog.h)
set(UI_FILES ui/chart_dialog.ui)

#添加 autoUIC 搜索路径
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/ui)
# 定义动态库
add_library(${PROJECT_NAME} SHARED ${SOURCES} ${HEADERS} ${UI_FILES})

# 包含目录
target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/ui   # .ui 生成的头文件在此
)
# 链接业务模块和 Qt，公有链接，链接关系可传递
target_link_libraries(${PROJECT_NAME}   PUBLIC
        Qt::Core
        Qt::Widgets
        Qt::Charts
        chart                       #上层CMakeLists保证了引入chart,不用再find_package
)

# 添加资源文件
target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/img.qrc)
