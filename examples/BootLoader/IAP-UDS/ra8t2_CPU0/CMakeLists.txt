#===========================================================
# 统一 BootLoader SDK - RA8T2 双通道（CAN-FD+ISO-TP / UART+LTM）
# 分层：core（硬件无关）/ port（自包含精简外设驱动）/ app（配置+入口）/ bsp（精简 FSP 支持）
# BootLoader 自身代码不依赖 LTM_HAL（不链接 libLTM_HAL.a、不调用 ltm_* API），
# LTM 协议组件（ltm_commut/protocol）以源码形式随 core 携带。
# FSP 芯片驱动源码（瑞萨官方）从 LTM_HAL 源码树引用，路径可配置：
#   -DFSP_HAL_DIR=D:/Develop/LtMotorLib/LTM_HAL/ltm_hal_ra8t2_CPU0
# 构建：cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain.cmake -DCMAKE_BUILD_TYPE=Release .
#       cmake --build build
#===========================================================
cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain.cmake)

project(BootLoader VERSION 1.0.0 LANGUAGES C ASM)

# FSP 芯片支持目录（瑞萨官方 SDK，默认指向 LTM_HAL 源码树）
if(NOT DEFINED FSP_HAL_DIR)
    set(FSP_HAL_DIR "D:/Develop/LtMotorLib/LTM_HAL/ltm_hal_ra8t2_CPU0")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)

add_compile_definitions(
    _RENESAS_RA_
    _RA_CORE=CPU0
    _RA_ORDINAL=1
)

# ========== 头文件路径：BootLoader 自带 bsp 优先，再引 FSP 芯片头 ==========
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/core/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/port
    ${CMAKE_CURRENT_SOURCE_DIR}/app
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp
    ${FSP_HAL_DIR}/src/drivers/hardware
    ${FSP_HAL_DIR}/src/drivers/hardware/ra_gen
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/fsp/inc
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/fsp/inc/api
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/fsp/inc/instances
    ${FSP_HAL_DIR}/src/drivers/hardware/ra_cfg/fsp_cfg
    ${FSP_HAL_DIR}/src/drivers/hardware/ra_cfg/fsp_cfg/bsp
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/arm/CMSIS_6/CMSIS/Core/Include
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/fsp/src/bsp/cmsis/Device/RENESAS/Include
)

# ========== BootLoader 自身源码 ==========
file(GLOB BL_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/core/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/core/protocol/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/port/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/app/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/*.c
)

# ========== FSP 芯片驱动源码（瑞萨官方，从 LTM_HAL 源码树引用） ==========
file(GLOB FSP_SOURCES
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/fsp/src/bsp/mcu/all/*.c
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/fsp/src/bsp/mcu/ra8t2/*.c
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/fsp/src/bsp/cmsis/Device/RENESAS/Source/*.c
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/fsp/src/r_canfd/*.c
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/fsp/src/r_sci_b_uart/*.c
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/fsp/src/r_mram/*.c
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/fsp/src/r_ioport/*.c
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/fsp/src/r_elc/*.c
    ${FSP_HAL_DIR}/src/drivers/hardware/ra/fsp/src/r_dmac/*.c
)

add_executable(${PROJECT_NAME}.elf ${BL_SOURCES} ${FSP_SOURCES})

# ========== 链接脚本 ==========
set(LINKER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/app/linker)
target_link_options(${PROJECT_NAME}.elf
    PRIVATE
        -L ${LINKER_DIR}
        -T ${LINKER_DIR}/fsp.ld
        -specs=nano.specs
        -specs=nosys.specs
        -Wl,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map
)

set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_C_FLAGS_DEBUG "-g -O2")

# ========== 产物 ==========
set(HEX_FILE ${CMAKE_CURRENT_SOURCE_DIR}/build/${PROJECT_NAME}.hex)
set(BIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/build/${PROJECT_NAME}.bin)
add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -Oihex $<TARGET_FILE:${PROJECT_NAME}.elf> ${HEX_FILE}
    COMMAND ${CMAKE_OBJCOPY} -Obinary --remove-section=__option_setting_* $<TARGET_FILE:${PROJECT_NAME}.elf> ${BIN_FILE}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${PROJECT_NAME}.elf> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/
    COMMAND ${SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
    COMMENT "IAP-UDS hex/bin -> build/"
)
