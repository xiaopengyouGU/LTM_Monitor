# chart 模块 单元/压力测试

测试对象只有**对外可见的公共接口**：`include/chart_manager.h`。
外部用户拿到的是 `chart_manager.h` + `chart.dll`，所以测试不直接包含
`DataStorage` / `ChartController` 等内部实现——完全从用户视角验证模块质量。
刷新路径内部即"全量范围 + 默认 M4(2400) 降采样"，公共接口测试已覆盖。

## 测试项

| 目标 | 覆盖内容 | 防的是哪个历史问题 |
|---|---|---|
| tst_chart_api | 默认配色循环、视图生命周期、自动/手动模式、FFT 正确性、导出/导入往返、失败信号 | 公共 API 语义回归（已抓出 20 色调板里 darkYellow 与橄榄绿重复） |
| tst_chart_stress.batchWriteThroughput | 60 通道 x 24 万点（14.4M）批量写入吞吐 + FFT 完整性 | 环形缓冲、批量写入 |
| tst_chart_stress.fullRangeRefreshStress | 预填充 14.4M 点后 60 通道全量范围实时刷新（默认 M4 2400 降采样），5ms 心跳监控 UI 最大卡顿 | 重绘卡顿（870ms/帧）、广播/重绘阻塞 UI |
| tst_chart_stress.exportImportStress | 60 通道 x 6 万点全量导出/导入吞吐 + 行数/列数完整性 | 导出线程、空通道跳过、CSV 大文件 |

## 构建与运行

随主工程构建（默认开启，可用 `-DCHART_BUILD_TESTS=OFF` 关闭）：

    cmake --build build --target tst_chart_api tst_chart_stress
    ctest --test-dir build -R tst_chart --output-on-failure

单独构建 chart 模块：

    cmake -S src/modules/chart -B build_chart -DCMAKE_PREFIX_PATH=D:/Qt/6.5.11/mingw_64 -DCHART_BUILD_TESTS=ON
    cmake --build build_chart --target tst_chart_api tst_chart_stress
    ctest --test-dir build_chart -R tst_chart --output-on-failure

说明：

- 测试程序内部自动设置 `QT_QPA_PLATFORM=offscreen`，命令行/CI 下无窗口运行；
- ctest 的 ENVIRONMENT 已自动把 Qt bin 目录加进 PATH，无需手工配置；
- 顶尖压力项内存占用约 0.5~1GB（60 通道环形缓冲本身 230MB），属预期；
- 耗时断言按 Debug 构建的宽松阈值给；卡顿类断言（心跳间隔 < 300ms）
  才是真正抓历史性能病的地方。