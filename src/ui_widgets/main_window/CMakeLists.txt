# ui_widgets/main_window/CMakeLists.txt
project(main_window LANGUAGES CXX)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)

qt_standard_project_setup()
set(CMAKE_AUTORCC ON)                   #编译资源文件，显示图标

# 显式列出源文件，这是开发规范
set(SOURCES
    src/mainwindow.cpp 
    src/data_hub.cpp
    src/pid_widget.cpp
    src/serial_widget.cpp
    src/console_widget.cpp
)

set(HEADERS
    include/mainwindow.h 
    include/data_hub.h
    include/pid_widget.h
    include/serial_widget.h
    include/console_widget.h
)

set(UI_FILES  
    ui/mainwindow.ui 
    ui/pid_widget.ui
    ui/serial_widget.ui
    ui/console_widget.ui
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
)

#链接业务模块
target_link_libraries(${PROJECT_NAME}   PUBLIC
        chart
        serial
        canfd
        ethernet
        record
        protocol
        data_map
        chart_map
        chart_dialog
        canfd_widget
        uds_widget
        status_bar
        log_analysis
) 

# 添加资源文件
target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/img.qrc)
