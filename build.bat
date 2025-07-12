@echo off
setlocal enabledelayedexpansion

rem OPC UA服务器构建脚本 (Windows版本)
rem 支持Windows 10/11 和 Visual Studio

set "BUILD_TYPE=Release"
set "CLEAN=false"
set "TEST=false"
set "INSTALL=false"
set "PACKAGE=false"
set "RUN=false"

rem 颜色定义（使用PowerShell颜色输出）
set "RED=[91m"
set "GREEN=[92m"
set "YELLOW=[93m"
set "BLUE=[94m"
set "NC=[0m"

rem 显示帮助信息
:show_help
echo 用法: %0 [选项]
echo.
echo 选项:
echo   -h, --help      显示帮助信息
echo   -d, --debug     构建调试版本
echo   -r, --release   构建发布版本 (默认)
echo   -c, --clean     清理构建目录
echo   -t, --test      运行测试
echo   -i, --install   安装到系统
echo   -p, --package   创建安装包
echo   --run           构建后运行服务器
echo.
echo 示例:
echo   %0                构建发布版本
echo   %0 -d             构建调试版本
echo   %0 -c             清理构建目录
echo   %0 -t             构建并运行测试
echo   %0 --run          构建并运行服务器
echo.
echo 注意: 需要安装CMake和Visual Studio或MinGW
goto :eof

rem 打印消息函数
:print_status
echo %BLUE%[INFO]%NC% %~1
goto :eof

:print_success
echo %GREEN%[SUCCESS]%NC% %~1
goto :eof

:print_warning
echo %YELLOW%[WARNING]%NC% %~1
goto :eof

:print_error
echo %RED%[ERROR]%NC% %~1
goto :eof

rem 检查依赖
:check_dependencies
call :print_status "检查构建依赖..."

rem 检查cmake
cmake --version >nul 2>&1
if errorlevel 1 (
    call :print_error "CMake未安装，请先安装CMake 3.10或更高版本"
    exit /b 1
)

rem 检查编译器
where cl >nul 2>&1
if errorlevel 1 (
    where gcc >nul 2>&1
    if errorlevel 1 (
        call :print_error "未找到C编译器，请安装Visual Studio或MinGW"
        exit /b 1
    )
)

call :print_success "所有依赖检查通过"
goto :eof

rem 清理构建目录
:clean_build
call :print_status "清理构建目录..."
if exist build (
    rmdir /s /q build
    call :print_success "构建目录已清理"
) else (
    call :print_warning "构建目录不存在，无需清理"
)
goto :eof

rem 构建项目
:build_project
call :print_status "开始构建项目 (构建类型: %BUILD_TYPE%)..."

rem 创建构建目录
if not exist build mkdir build
cd build

rem 检查是否有Visual Studio
where cl >nul 2>&1
if errorlevel 1 (
    rem 使用MinGW
    call :print_status "使用MinGW构建..."
    cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ..
    mingw32-make -j%NUMBER_OF_PROCESSORS%
) else (
    rem 使用Visual Studio
    call :print_status "使用Visual Studio构建..."
    cmake -G "Visual Studio 16 2019" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ..
    cmake --build . --config %BUILD_TYPE% --parallel %NUMBER_OF_PROCESSORS%
)

cd ..
call :print_success "项目构建完成"
goto :eof

rem 运行测试
:run_tests
call :print_status "运行测试..."
cd build
ctest -V -C %BUILD_TYPE%
if errorlevel 1 (
    call :print_error "测试失败"
    cd ..
    exit /b 1
)
call :print_success "所有测试通过"
cd ..
goto :eof

rem 安装项目
:install_project
call :print_status "安装项目..."
cd build
cmake --install . --config %BUILD_TYPE%
if errorlevel 1 (
    call :print_error "项目安装失败"
    cd ..
    exit /b 1
)
call :print_success "项目安装成功"
cd ..
goto :eof

rem 创建安装包
:create_package
call :print_status "创建安装包..."
cd build
cmake --build . --target package --config %BUILD_TYPE%
if errorlevel 1 (
    call :print_error "创建安装包失败"
    cd ..
    exit /b 1
)
call :print_success "安装包创建成功"
call :print_status "生成的包文件："
dir *.zip *.exe 2>nul
cd ..
goto :eof

rem 运行服务器
:run_server
call :print_status "启动OPC UA服务器..."
cd build
if exist opcua_server.exe (
    call :print_success "服务器已启动，按Ctrl+C停止"
    opcua_server.exe
) else if exist %BUILD_TYPE%\opcua_server.exe (
    call :print_success "服务器已启动，按Ctrl+C停止"
    %BUILD_TYPE%\opcua_server.exe
) else (
    call :print_error "服务器可执行文件不存在，请先构建项目"
    cd ..
    exit /b 1
)
cd ..
goto :eof

rem 解析命令行参数
:parse_args
if "%~1"=="" goto :end_parse
if /i "%~1"=="-h" goto :help
if /i "%~1"=="--help" goto :help
if /i "%~1"=="-d" (
    set "BUILD_TYPE=Debug"
    shift
    goto :parse_args
)
if /i "%~1"=="--debug" (
    set "BUILD_TYPE=Debug"
    shift
    goto :parse_args
)
if /i "%~1"=="-r" (
    set "BUILD_TYPE=Release"
    shift
    goto :parse_args
)
if /i "%~1"=="--release" (
    set "BUILD_TYPE=Release"
    shift
    goto :parse_args
)
if /i "%~1"=="-c" (
    set "CLEAN=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--clean" (
    set "CLEAN=true"
    shift
    goto :parse_args
)
if /i "%~1"=="-t" (
    set "TEST=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--test" (
    set "TEST=true"
    shift
    goto :parse_args
)
if /i "%~1"=="-i" (
    set "INSTALL=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--install" (
    set "INSTALL=true"
    shift
    goto :parse_args
)
if /i "%~1"=="-p" (
    set "PACKAGE=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--package" (
    set "PACKAGE=true"
    shift
    goto :parse_args
)
if /i "%~1"=="--run" (
    set "RUN=true"
    shift
    goto :parse_args
)
call :print_error "未知选项: %~1"
call :show_help
exit /b 1

:help
call :show_help
goto :eof

:end_parse

rem 主程序
:main
call :print_status "======================================"
call :print_status "    OPC UA服务器构建脚本 (Windows)"
call :print_status "======================================"

rem 检查依赖
call :check_dependencies
if errorlevel 1 exit /b 1

rem 清理构建目录
if "%CLEAN%"=="true" (
    call :clean_build
    if "%TEST%%INSTALL%%PACKAGE%%RUN%"=="falsefalsefalsefalse" (
        exit /b 0
    )
)

rem 构建项目
call :build_project
if errorlevel 1 exit /b 1

rem 运行测试
if "%TEST%"=="true" (
    call :run_tests
    if errorlevel 1 exit /b 1
)

rem 安装项目
if "%INSTALL%"=="true" (
    call :install_project
    if errorlevel 1 exit /b 1
)

rem 创建安装包
if "%PACKAGE%"=="true" (
    call :create_package
    if errorlevel 1 exit /b 1
)

rem 运行服务器
if "%RUN%"=="true" (
    call :run_server
    if errorlevel 1 exit /b 1
)

call :print_success "所有操作完成！"
call :print_status "可执行文件位置: build\opcua_server.exe 或 build\%BUILD_TYPE%\opcua_server.exe"
call :print_status "使用 'build\opcua_server.exe --help' 获取运行帮助"
goto :eof

rem 程序入口
call :parse_args %*
call :main 