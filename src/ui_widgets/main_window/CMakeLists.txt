# ui_widgets/main_window/CMakeLists.txt
project(main_window LANGUAGES CXX)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Charts)

qt_standard_project_setup()
set(CMAKE_AUTORCC ON)                   #编译资源文件，显示图标

# 显式列出源文件，这是开发规范
set(SOURCES
    src/mainwindow.cpp 
    src/status_bar.cpp 
    src/window_data_processor.cpp
)

set(HEADERS
    include/mainwindow.h 
    include/status_bar.h 
    include/window_data_processor.h
)

set(UI_FILES  
    ui/mainwindow.ui 
)   # AUTOUIC 会自动处理

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

# 链接Qt模块，公有链接，链接关系可传递
target_link_libraries(${PROJECT_NAME}   PUBLIC
        Qt::Core
        Qt::Widgets
        Qt::Charts
)

#链接业务模块
target_link_libraries(${PROJECT_NAME}   PUBLIC
        chart
        serial
        canfd
        record
        data_map
        chart_dialog
        canfd_widget
        log_analysis
) 

# 添加资源文件
target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/img.qrc)
