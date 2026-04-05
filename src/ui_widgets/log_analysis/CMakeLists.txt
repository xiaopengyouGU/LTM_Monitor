# ui_widgets/main_window/CMakeLists.txt
project(log_analysis LANGUAGES CXX)

# 查找 Qt 组件，最低版本要求6.2
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Sql)

qt_standard_project_setup()
set(CMAKE_AUTORCC ON)                   #编译资源文件，显示图标
# 显式列出源文件，这是开发规范
set(SOURCES 
    src/log_analysis.cpp
    src/log_table_model.cpp    
)

set(HEADERS 
    include/log_analysis.h
    include/log_table_model.h
)

set(UI_FILES ui/log_analysis.ui)

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
        Qt::Sql
        record                       #上层CMakeLists保证了引入chart,不用再find_package
)

# 添加资源文件
target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/img.qrc)
