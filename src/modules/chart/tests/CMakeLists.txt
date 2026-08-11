# ============================================================
# chart 模块 单元/压力测试
# 只测对外可见的公共接口（include/chart_manager.h）。
# 内部实现（DataStorage / ChartController / 导入导出器）用户看不到，
# 因此测试不直接包含它们，与"用户视角"保持一致。
# ============================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Test)

add_executable(tst_chart_api tst_chart_api.cpp)
add_executable(tst_chart_stress tst_chart_stress.cpp)

# Qt bin 目录（ctest 运行时把 Qt DLL 加进 PATH）
get_target_property(_qt6_core_loc Qt6::Core IMPORTED_LOCATION)
if(_qt6_core_loc)
    get_filename_component(QT_BIN_DIR "${_qt6_core_loc}" DIRECTORY)
else()
    set(QT_BIN_DIR "")
endif()

foreach(tgt IN ITEMS tst_chart_api tst_chart_stress)
    target_link_libraries(${tgt} PRIVATE chart Qt6::Test Qt6::Widgets Qt6::Core)

    # 独立构建 chart 模块时，把 chart.dll / qcustomplot.dll 复制到测试 exe 同目录
    add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:chart>" "$<TARGET_FILE_DIR:${tgt}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/QCustomPlot/qcustomplot.dll"
            "$<TARGET_FILE_DIR:${tgt}>"
        COMMENT "复制 chart.dll / qcustomplot.dll 到测试目录")

    add_test(NAME ${tgt} COMMAND $<TARGET_FILE:${tgt}>)
    set_tests_properties(${tgt} PROPERTIES
        TIMEOUT 300
        ENVIRONMENT "PATH=${QT_BIN_DIR};$ENV{PATH}")
endforeach()