# apps/main_app/CMakeLists.txt
project(LTM_Monitor LANGUAGES CXX)

# 添加资源文件
set(APP_RC ${CMAKE_CURRENT_SOURCE_DIR}/app.rc)  #设置程序图标

# 主程序只需一个 main.cpp
# Debug 版本带控制台方便调试，Release 版本不带控制台
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_executable(${PROJECT_NAME} WIN32 main.cpp ${APP_RC})
else()
    add_executable(${PROJECT_NAME} main.cpp ${APP_RC})
endif()

# 设置图标（仅 Windows）
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        RC_ICONS "${CMAKE_CURRENT_SOURCE_DIR}/icon.ico"
    )
endif()

# 链接所有需要的 UI 模块和业务模块
target_link_libraries(${PROJECT_NAME}
    PRIVATE
        main_window
        chart_dialog
        log_analysis
)

# 确保运行时能找到依赖的动态库（因输出目录统一，无需额外操作）