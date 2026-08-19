#按照依赖顺序添加目录，否则可能会找不到依赖库。
add_subdirectory(map_table)
add_subdirectory(main_window)
add_subdirectory(chart_dialog)
add_subdirectory(log_analysis)
add_subdirectory(canfd_widget)     # 放最后，避免与主窗口目标冲突
add_subdirectory(uds_widget)

