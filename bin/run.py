import subprocess
import sys
import os
import ctypes
import argparse

def is_admin():
    """检查当前进程是否具有管理员权限"""
    try:
        return ctypes.windll.shell32.IsUserAnAdmin()
    except:
        return False

def run_vspd():
    """定位并启动 vspdconfig.exe"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    vspd_exe = os.path.join(script_dir, "Virtual Serial Port Driver 6.9", "vspdconfig.exe")

    if not os.path.exists(vspd_exe):
        print(f"❌ 找不到 VSPD 可执行文件: {vspd_exe}")
        return False

    print(f">>> 启动 VSPD 配置程序 ...")
    # 直接启动，不等待程序退出
    subprocess.Popen([vspd_exe], shell=True)
    return True

def main():
    parser = argparse.ArgumentParser(description="启动 Virtual Serial Port Driver 配置程序")
    group = parser.add_mutually_exclusive_group()
    group.add_argument("-v", "--vspd", action="store_true", help="启动 VSPD 配置程序")

    args = parser.parse_args()

    if not args.vspd:
        parser.print_help()
        sys.exit(0)

    # 检查管理员权限（VSPD 通常需要管理员权限才能创建/删除端口）
    if not is_admin():
        print("需要管理员权限，正在请求提升...")
        ctypes.windll.shell32.ShellExecuteW(
            None, "runas", sys.executable, " ".join(sys.argv), None, 1
        )
        return

    if not run_vspd():
        sys.exit(1)

if __name__ == "__main__":
    main()