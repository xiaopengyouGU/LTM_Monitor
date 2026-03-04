cmake_minimum_required(VERSION 3.19)
project(LTM_Monitor_V_0_1 LANGUAGES CXX)

#使用Qt自带的gcc编译器，否则可能出现版本不一致导致的奇怪问题：
#如：Debug模式下，程序不运行，但是Release模式下程序正常运行
#需要将Qt的dll库文件添加到环境变量PATH中，否则不能运行output文件夹中的.exe文件
#执行一次即可，例如：
#set PATH=D:\Qt\6.5.11\mingw_64\bin;%PATH%，借助python脚本script.py可以自动完成

# 设置运行时输出目录为构建目录下的output
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/output/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/output/Release)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets SerialPort Charts)

qt_standard_project_setup()

aux_source_directory(src/drivers/ui_widgets UI_WIDGETS)
aux_source_directory(src/drivers/protocol PROTOCOL)
aux_source_directory(src/drivers/serial SERIAL)
aux_source_directory(src/drivers/threads THREADS)
aux_source_directory(src/drivers/chart CHART)
aux_source_directory(src/ui UI)
aux_source_directory(src/application APP)
aux_source_directory(src/data_def DEF)

#添加autoUIC 搜索路径
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)

set(CMAKE_AUTORCC ON)  # 启动RCC编译，才能正确显示图标

qt_add_executable(${PROJECT_NAME}   #不能出现“，”
    ${UI_WIDGETS}
    ${UI}
    ${PROTOCOL}
    ${SERIAL}
    ${THREADS}
    ${CHART}
    ${APP}
    src/img.qrc                     #添加RCC， qrc文件就放在src文件夹下
)

# 添加头文件搜索路径
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/ui_widgets   # mainwindow.h 所在目录
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/application
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/serial
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/threads
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/chart
    ${CMAKE_CURRENT_SOURCE_DIR}/src/drivers/data_def
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::SerialPort
        Qt::Charts
)

include(GNUInstallDirs)

#执行cmake --install build, 将可执行文件（.exe）和相关的依赖安装到默认的安装目录下
#一般为C:/Program File(x86)/xx， 这时双击可执行文件（.exe）可以直接运行，无需配置环境变量
install(TARGETS ${PROJECT_NAME}
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET ${PROJECT_NAME}
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})