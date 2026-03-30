cmake_minimum_required(VERSION 3.19)
project(LTM_Project VERSION 1.0.0 LANGUAGES CXX)

#使用Qt自带的gcc编译器，否则可能出现版本不一致导致的奇怪问题：
#如：Debug模式下，程序不运行，但是Release模式下程序正常运行
#需要将Qt的dll库文件添加到环境变量PATH中，否则不能运行output文件夹中的.exe文件
#执行一次即可，例如：
#set PATH=D:\Qt\6.5.11\mingw_64\bin;%PATH%，借助python脚本script.py可以自动完成
# 设置运行时输出目录为构建目录下的output
# 顶层 CMakeLists.txt

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 启用 Qt 自动工具
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 查找 Qt 组件
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Charts Sql SerialPort)

# 统一输出目录（所有目标输出到 build/output/ 下）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)

# 多配置支持（Debug/Release）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/output/Debug)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/output/Release)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/output/Debug)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/output/Release)

# 按顺序添加模块子目录
add_subdirectory(src/modules)                  
add_subdirectory(src/ui_widgets)
add_subdirectory(src/application)

include(GNUInstallDirs)
# TARGET为可执行文件名
#执行cmake --install build, 将可执行文件（.exe）和相关的依赖安装到默认的安装目录下
#一般为C:/Program File(x86)/xx， 这时双击可执行文件（.exe）可以直接运行，无需配置环境变量

# ========== 统一安装 ==========
# 1. 安装主程序（假设 application 中定义了 UIM_APP 目标）
set(APP_NAME LTM_Monitor)       #可执行文件名
install(TARGETS ${APP_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

# 2. 安装所有动态库模块（手动列出目标）
set(MODULE_TARGETS
    chart
    serial
    chart_dialog
    main_window
    # 如果有其他模块，继续添加
)

foreach(target ${MODULE_TARGETS})
    install(TARGETS ${target}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        #INCLUDES DESTINATION include， 注释该行，避免与手动安装冲突
    )
endforeach()

# 3. 安装每个模块的头文件（直接平铺到 include/ 下，模块间需保证文件名唯一）
install(DIRECTORY src/modules/chart/include/ DESTINATION include)
install(DIRECTORY src/modules/serial/include/ DESTINATION include)
install(DIRECTORY src/ui_widgets/main_window/include/ DESTINATION include)
install(DIRECTORY src/ui_widgets/chart_dialog/include/ DESTINATION include)
# 如果有其他模块，继续添加

# # 4. 安装第三方 DLL（例如 motion 模块的 LTSMC.dll）
# set(LTSMC_DLL ${CMAKE_CURRENT_SOURCE_DIR}/src/modules/motion/third_party/LTSMC/lib/LTSMC.dll)
# if(EXISTS ${LTSMC_DLL})
#     install(FILES ${LTSMC_DLL} DESTINATION ${CMAKE_INSTALL_BINDIR})
# endif()
# 如果有其他第三方 DLL，同理

# 5. 生成 Qt 部署脚本（将 Qt 运行时依赖复制到安装目录）
qt_generate_deploy_app_script(
    TARGET ${APP_NAME}
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})