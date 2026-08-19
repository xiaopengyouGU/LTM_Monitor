#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""统一 BootLoader 一键脚本
- 构建 BootLoader 固件（ARM，CMake + 交叉工具链）
- J-Link 一键烧录（R7KA8T2LF_CPU0）
- 平台无关测试（core 层 + mock：功能 + 压测）
- UART IAP 升级客户端（USB-TTL，LTM 协议）

用法:
  python script.py -b              # 仅构建
  python script.py -R              # 构建(Release) + 烧录
  python script.py -D              # 构建(Debug) + 烧录
  python script.py -o              # 仅烧录现有 elf
  python script.py -t              # 平台无关测试（功能 + 压测）
  python script.py -c              # 清理 build 目录
  python script.py -u --port COM3 --firmware app.bin   # UART IAP 升级
"""
import os
import shutil
import subprocess
import argparse

ROOT      = os.path.dirname(os.path.abspath(__file__))
BUILD_DIR = os.path.join(ROOT, "build")
ELF_PATH  = os.path.join(BUILD_DIR, "BootLoader.elf")
TOOLCHAIN = os.path.join(ROOT, "cmake", "toolchain.cmake")
TEST_DIR  = os.path.join(ROOT, "host", "test")
DEVICE    = "R7KA8T2LF_CPU0"

DEFAULT_FSP = r"D:/Develop/LtMotorLib/LTM_HAL/ltm_hal_ra8t2_CPU0"


def run(cmd):
    print(">>> " + " ".join(cmd))
    return subprocess.run(cmd).returncode == 0


def ensure_cache(dirpath, source):
    cache = os.path.join(dirpath, "CMakeCache.txt")
    if not os.path.isfile(cache):
        return
    home = None
    with open(cache, "r", errors="ignore") as f:
        for line in f:
            if line.startswith("CMAKE_HOME_DIRECTORY:INTERNAL"):
                home = line.split("=", 1)[1].strip()
                break
    if home and os.path.normcase(os.path.normpath(home)) != os.path.normcase(os.path.normpath(source)):
        print(f"[warn] CMake 缓存来源不匹配: {home}")
        shutil.rmtree(dirpath, ignore_errors=True)


def build(build_type, fsp_hal):
    ensure_cache(BUILD_DIR, ROOT)
    cfg = ["cmake", "-G", "MinGW Makefiles", "-B", BUILD_DIR, "-S", ROOT,
           "-DCMAKE_BUILD_TYPE=" + build_type,
           "-DFSP_HAL_DIR=" + fsp_hal]
    return run(cfg) and run(["cmake", "--build", BUILD_DIR])


def find_jlink():
    exe = os.environ.get("JLINK_PATH")
    if exe and os.path.isfile(exe):
        return exe
    exe = shutil.which("JLink.exe") or shutil.which("JLink")
    if exe:
        return exe
    for p in (r"C:\Program Files\SEGGER\JLink\JLink.exe",
              r"C:\Program Files (x86)\SEGGER\JLink\JLink.exe"):
        if os.path.isfile(p):
            return p
    return None


def flash(jlink, wipe_app=True):
    elf = os.path.abspath(ELF_PATH).replace("\\", "/")
    if not os.path.isfile(elf):
        print(f"[FAIL] 找不到 {elf}，请先构建")
        return False
    script = (f"device {DEVICE}\n"
              "si SWD\n"
              "speed 1000\n"
              "r\nh\n"
              f"loadfile {elf}\n")
    bad_vec = None
    if wipe_app:
        # 运行区起始写坏向量表（SP=0, PC=0）：
        # BootLoader 上电判定 App 无效 → 永久驻留等待升级，不跳转
        bad_vec = "bad_vectors.bin"
        with open(bad_vec, "wb") as f:
            f.write(b"\x00\x00\x00\x00" * 2)
        script += f"loadbin {os.path.abspath(bad_vec).replace(chr(92), '/')}, 0x02008000\n"
    script += "r\ng\nexit\n"
    sf = "flash.jlink"
    with open(sf, "w") as f:
        f.write(script)
    try:
        return run([jlink, sf])
    finally:
        if bad_vec and os.path.isfile(bad_vec):
            os.remove(bad_vec)
        if os.path.isfile(sf):
            os.remove(sf)


def run_tests():
    if not os.path.isdir(TEST_DIR):
        print(f"[FAIL] 找不到测试目录: {TEST_DIR}")
        return False
    core = os.path.join(ROOT, "core")
    ok = True
    for src, exe in (("test_main.c", "test_runner.exe"),
                     ("stress_test.c", "stress_runner.exe")):
        cmd = ["gcc", "-std=c11", "-Wall", "-Wextra",
               "-Wno-pointer-to-int-cast", "-Wno-int-to-pointer-cast",
               "-I" + core, "-I" + os.path.join(core, "protocol"),
               "-I" + os.path.join(ROOT, "port"),
               "-I" + TEST_DIR, "-I" + os.path.join(ROOT, "app"),
               os.path.join(core, "bootloader.c"),
               os.path.join(core, "uds_server.c"),
               os.path.join(core, "iso15765.c"),
               os.path.join(core, "protocol", "protocol.c"),
               os.path.join(core, "protocol", "ltm_commut.c"),
               os.path.join(TEST_DIR, "mock.c"),
               os.path.join(TEST_DIR, "port_mock.c"),
               os.path.join(TEST_DIR, src),
               "-o", os.path.join(TEST_DIR, exe)]
        if not run(cmd):
            ok = False
            continue
        if subprocess.run([os.path.join(TEST_DIR, exe)]).returncode != 0:
            ok = False
    return ok


def uart_upgrade(port, firmware, baud, app_base):
    """UART IAP 升级：调用 host/upgrade_client.py"""
    client = os.path.join(ROOT, "host", "upgrade_client.py")
    cmd = ["python", client, "--port", port, "--firmware", firmware,
           "--baud", str(baud), "--app-base", hex(app_base)]
    return run(cmd)


def clean():
    if os.path.isdir(BUILD_DIR):
        shutil.rmtree(BUILD_DIR, ignore_errors=True)
        print(f"[clean] removed {BUILD_DIR}")
    print("[OK] 清理完成")


def main():
    ap = argparse.ArgumentParser(description="统一 BootLoader 构建/烧录/测试/升级")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("-b", "--build", action="store_true", help="仅构建")
    g.add_argument("-R", "--release-run", action="store_true", help="构建(Release) + 烧录")
    g.add_argument("-D", "--debug-run", action="store_true", help="构建(Debug) + 烧录")
    g.add_argument("-o", "--only-flash", action="store_true", help="仅烧录现有 elf")
    g.add_argument("-t", "--test", action="store_true", help="平台无关测试（功能 + 压测）")
    g.add_argument("-u", "--uart-upgrade", action="store_true", help="UART IAP 升级")
    g.add_argument("-c", "--clean", action="store_true", help="清理 build 目录")
    ap.add_argument("--jlink", default=None, help="JLink.exe 路径")
    ap.add_argument("--fsp-hal", default=DEFAULT_FSP, help="FSP 芯片支持源码目录")
    ap.add_argument("--keep-app", action="store_true",
                    help="保留运行区旧 App（默认：烧 BootLoader 即擦除运行区，BootLoader 永久等待升级）")
    ap.add_argument("--port", default=None, help="UART 升级串口（如 COM3）")
    ap.add_argument("--baud", type=int, default=115200, help="UART 波特率")
    ap.add_argument("--firmware", default=None, help="固件 bin 文件")
    ap.add_argument("--app-base", type=lambda x: int(x, 0), default=0x02008000, help="App 起始地址")
    args = ap.parse_args()

    if args.clean:
        clean()
        return

    if args.uart_upgrade:
        if not args.port or not args.firmware:
            ap.print_help()
            return
        if not uart_upgrade(args.port, args.firmware, args.baud, args.app_base):
            sys.exit("[FAIL] UART 升级失败")
        print("[OK] UART 升级完成")
        return

    if args.test:
        print("[1/1] 平台无关测试 ...")
        if not run_tests():
            sys.exit("[FAIL] 测试未通过")
        print("[OK] 测试全部通过")
        return

    if not any([args.build, args.release_run, args.debug_run, args.only_flash]):
        ap.print_help()
        return

    build_type = "Release"
    if args.debug_run:
        build_type = "Debug"

    if args.build or args.release_run or args.debug_run:
        print(f"[1/2] 构建 BootLoader ({build_type}) ...")
        if not build(build_type, args.fsp_hal):
            sys.exit("[FAIL] 构建失败")
        print(f"[OK] 产物 -> {ELF_PATH}")

    if args.release_run or args.debug_run or args.only_flash:
        jlink = args.jlink or find_jlink()
        if not jlink:
            sys.exit("[FAIL] 找不到 JLink.exe，用 -jlink 指定或设 JLINK_PATH")
        print(f"[2/2] 烧录 {ELF_PATH} (JLink: {jlink})"
              + ("，擦除运行区（默认）" if not args.keep_app else "，保留运行区 App"))
        if not flash(jlink, not args.keep_app):
            sys.exit("[FAIL] 烧录失败")


if __name__ == "__main__":
    import sys
    main()
