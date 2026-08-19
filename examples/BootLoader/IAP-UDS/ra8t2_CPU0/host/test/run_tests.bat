@echo off
rem 平台无关测试：主机 gcc 编译 core 层 + mock，验证双通道协议逻辑
setlocal
set ROOT=%~dp0..\..
set TEST=%~dp0

set CORE=%ROOT%\core
set SRC=%CORE%\bootloader.c %CORE%\uds_server.c %CORE%\iso15765.c %CORE%\protocol\protocol.c %CORE%\protocol\ltm_commut.c

echo === 功能测试 ===
gcc -std=c11 -Wall -Wextra -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast ^
    -I%CORE% -I%CORE%\protocol -I%TEST% -I%ROOT%\app ^
    %SRC% %TEST%\mock.c %TEST%\port_mock.c %TEST%\test_main.c ^
    -o %TEST%\test_runner.exe
if errorlevel 1 exit /b 1
%TEST%\test_runner.exe
if errorlevel 1 exit /b 1

echo === 压力测试 ===
gcc -std=c11 -Wall -Wextra -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast ^
    -I%CORE% -I%CORE%\protocol -I%TEST% -I%ROOT%\app ^
    %SRC% %TEST%\mock.c %TEST%\port_mock.c %TEST%\stress_test.c ^
    -o %TEST%\stress_runner.exe
if errorlevel 1 exit /b 1
%TEST%\stress_runner.exe
exit /b %errorlevel%
