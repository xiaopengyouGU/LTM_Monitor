#===========================================================
# 工具链配置文件
# 适用于 arm-none-eabi-gcc 交叉编译工具链
# 目标芯片：R7KA8T2LFLCAB (RA8T2, Cortex-M85, FPU)
#===========================================================

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

#-----------------------------------------------------------
# 指定交叉编译工具链路径（需确保已添加到 PATH）
#-----------------------------------------------------------
set(CMAKE_C_COMPILER    arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER  arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER  arm-none-eabi-gcc)
set(CMAKE_AR            arm-none-eabi-ar)
set(CMAKE_OBJCOPY       arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP       arm-none-eabi-objdump)
set(SIZE                arm-none-eabi-size)

#-----------------------------------------------------------
# 禁止 CMake 在配置阶段尝试运行测试程序
#-----------------------------------------------------------
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

#-----------------------------------------------------------
# 基础编译选项（CPU架构 + FPU + ABI + 警告 + 优化）
# 基于 RASC 生成的配置整理
#-----------------------------------------------------------
# CPU架构：Cortex-M85，Thumb指令集
# FPU：启用单精度硬件浮点 (FPv5-SP-D16)
# 浮点ABI：硬浮点 (hard)
set(ARCH_FLAGS "-mcpu=cortex-m85 -mthumb -mfpu=fpv5-sp-d16 -mfloat-abi=hard")

# 警告选项（来自 RASC 配置）
set(WARNING_FLAGS "-Wall -Wextra -Wunused -Wuninitialized -Wmissing-declarations -Wconversion -Wpointer-arith -Wshadow -Wlogical-op -Waggregate-return -Wfloat-equal")

# 通用选项（节区分离、依赖生成）
set(COMMON_FLAGS "${ARCH_FLAGS} ${WARNING_FLAGS} -fmessage-length=0 -fsigned-char -ffunction-sections -fdata-sections -MMD -MP")

# C 编译器默认选项
set(CMAKE_C_FLAGS_INIT "${COMMON_FLAGS} -std=c99")

# C++ 编译器默认选项
set(CMAKE_CXX_FLAGS_INIT "${COMMON_FLAGS} -std=c++11")

# 打印内存使用
set(CMAKE_EXE_LINKER_FLAGS_INIT "${ARCH_FLAGS} -Wl,--gc-sections -Wl,--print-memory-usage")
#-----------------------------------------------------------
# 预定义宏（基于 RASC 生成的 RASC_CMAKE_DEFINITIONS）
#-----------------------------------------------------------
add_compile_definitions(
    _RA_CORE=CPU0
    _RA_ORDINAL=1
    _RENESAS_RA_
)

#-----------------------------------------------------------
# GCC 版本 >= 12.2 时的额外兼容选项
#-----------------------------------------------------------
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 12.2)
    add_compile_options(
        --param=min-pagesize=0
        -Wno-format-truncation
        -Wno-stringop-overflow
    )
endif()