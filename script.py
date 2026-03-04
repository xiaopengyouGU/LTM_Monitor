import subprocess
import sys
import os
import shutil
import argparse

#使用Qt自带的gcc编译器，否则可能出现版本不一致导致的奇怪问题：
#如：Debug模式下，程序不运行，但是Release模式下程序正常运行
# 项目配置
PROJECT_NAME = "LTM_Monitor_V_0_1"                 #可执行文件名
QT_PREFIX_PATH = "D:/Qt/6.5.11/mingw_64"           #Qt文件路径
INSTALL_DIR = "D:/LTM_Monitor"                     #软件安装目录
QT_BIN_PATH = os.path.join(QT_PREFIX_PATH, "bin")
BUILD_DIR = "build"                                 

def run_cmake_configure(build_type="Debug"):
    compile_cmd = [
        "cmake",
        "-G", "MinGW Makefiles",
        "-B", BUILD_DIR,
        "-S", ".",
        f"-DCMAKE_PREFIX_PATH={QT_PREFIX_PATH}",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        f"-DCMAKE_INSTALL_PREFIX={INSTALL_DIR}"
    ]
    print(f">>> 配置 CMake (Build type: {build_type}) ...")
    result = subprocess.run(compile_cmd, check=False)
    return result.returncode == 0

def run_cmake_build():
    build_cmd = ["cmake", "--build", BUILD_DIR]
    print(">>> 构建项目 ...")
    result = subprocess.run(build_cmd, check=False)
    return result.returncode == 0

def run_executable(build_type):
    exe_path = os.path.join(BUILD_DIR, "output", build_type, f"{PROJECT_NAME}.exe")
    if not os.path.exists(exe_path):
        print(f"❌ 找不到可执行文件: {exe_path}")
        return False

    env = os.environ.copy()
    env["PATH"] = QT_BIN_PATH + os.pathsep + env.get("PATH", "")
    qt_plugins_path = os.path.join(QT_PREFIX_PATH, "plugins")
    if os.path.exists(qt_plugins_path):
        env["QT_PLUGIN_PATH"] = qt_plugins_path

    print(f">>> 运行 {exe_path} ...")
    subprocess.run([exe_path], env=env)
    return True

def run_install():
    install_cmd = ["cmake", "--install", BUILD_DIR]
    print(">>> 安装项目 ...")
    result = subprocess.run(install_cmd, check=False)
    return result.returncode == 0

def clean_build():
    if os.path.exists(BUILD_DIR):
        print(f">>> 删除构建目录 {BUILD_DIR} ...")
        shutil.rmtree(BUILD_DIR, ignore_errors=True)
    else:
        print(">>> 构建目录不存在，无需清理。")

def main():
    parser = argparse.ArgumentParser(description="LTM_Monitor 项目构建与运行脚本")
    group = parser.add_mutually_exclusive_group()   #使用了互斥组，所以最多只能有一个选项
    group.add_argument("-b", "--build-only", action="store_true", help="仅构建 Debug 版本（不运行）")
    group.add_argument("-D", "--debug-run", action="store_true", help="构建并运行 Debug 版本")
    group.add_argument("-R", "--release-run", action="store_true", help="构建并运行 Release 版本")
    group.add_argument("-i", "--install", action="store_true", help="构建并安装项目")
    group.add_argument("-c", "--clean", action="store_true", help="仅清理构建目录")
    args = parser.parse_args()

    # 无参数：显示帮助, -h时也显示帮助，系统自带的
    if not any(vars(args).values()):
        parser.print_help()
        sys.exit(0)

    if args.clean:
        clean_build()
        return

    # 确定构建类型
# 确定构建类型
    if args.release_run or args.install:
        build_type = "Release"
    else:  # 包括 -b, -D 以及默认（但默认无参数时会提前显示帮助，所以不会走到这里）
        build_type = "Debug"
    run_needed = args.debug_run or args.release_run
    install_needed = args.install
    build_only = args.build_only

    # 执行配置和构建（除非只是清理）
    if build_only or run_needed or install_needed:
        if not run_cmake_configure(build_type):
            print("❌ CMake 配置失败")
            sys.exit(1)
        if not run_cmake_build():
            print("❌ 构建失败")
            sys.exit(1)

    # 根据选项执行后续操作
    if run_needed:
        run_executable(build_type)
    elif install_needed:
        run_install()
    elif build_only:
        print("✅ 构建完成 (Debug)")
    # 其他情况已被互斥组覆盖

if __name__ == "__main__":
    main()