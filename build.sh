#!/bin/bash

# OPC UA服务器构建脚本
# 支持Linux和macOS

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 显示帮助信息
show_help() {
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -h, --help      显示帮助信息"
    echo "  -d, --debug     构建调试版本"
    echo "  -r, --release   构建发布版本 (默认)"
    echo "  -c, --clean     清理构建目录"
    echo "  -t, --test      运行测试"
    echo "  -i, --install   安装到系统"
    echo "  -p, --package   创建安装包"
    echo "  --run           构建后运行服务器"
    echo ""
    echo "示例:"
    echo "  $0                构建发布版本"
    echo "  $0 -d             构建调试版本"
    echo "  $0 -c             清理构建目录"
    echo "  $0 -t             构建并运行测试"
    echo "  $0 --run          构建并运行服务器"
}

# 检查依赖
check_dependencies() {
    print_status "检查构建依赖..."
    
    # 检查cmake
    if ! command -v cmake &> /dev/null; then
        print_error "CMake未安装，请先安装CMake 3.10或更高版本"
        exit 1
    fi
    
    # 检查编译器
    if ! command -v gcc &> /dev/null && ! command -v clang &> /dev/null; then
        print_error "未找到C编译器，请安装GCC或Clang"
        exit 1
    fi
    
    # 检查make
    if ! command -v make &> /dev/null; then
        print_error "Make未安装，请先安装make"
        exit 1
    fi
    
    print_success "所有依赖检查通过"
}

# 清理构建目录
clean_build() {
    print_status "清理构建目录..."
    if [ -d "build" ]; then
        rm -rf build
        print_success "构建目录已清理"
    else
        print_warning "构建目录不存在，无需清理"
    fi
}

# 构建项目
build_project() {
    local build_type=$1
    
    print_status "开始构建项目 (构建类型: $build_type)..."
    
    # 创建构建目录
    mkdir -p build
    cd build
    
    # 配置项目
    print_status "配置项目..."
    cmake -DCMAKE_BUILD_TYPE=$build_type ..
    
    # 编译
    print_status "编译项目..."
    make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    
    cd ..
    print_success "项目构建完成"
}

# 运行测试
run_tests() {
    print_status "运行测试..."
    cd build
    if ctest -V; then
        print_success "所有测试通过"
    else
        print_error "测试失败"
        exit 1
    fi
    cd ..
}

# 安装项目
install_project() {
    print_status "安装项目..."
    cd build
    if sudo make install; then
        print_success "项目安装成功"
    else
        print_error "项目安装失败"
        exit 1
    fi
    cd ..
}

# 创建安装包
create_package() {
    print_status "创建安装包..."
    cd build
    if make package; then
        print_success "安装包创建成功"
        print_status "生成的包文件："
        ls -la *.tar.gz *.zip *.deb *.rpm 2>/dev/null || true
    else
        print_error "创建安装包失败"
        exit 1
    fi
    cd ..
}

# 运行服务器
run_server() {
    print_status "启动OPC UA服务器..."
    cd build
    if [ -f "opcua_server" ]; then
        print_success "服务器已启动，按Ctrl+C停止"
        ./opcua_server
    else
        print_error "服务器可执行文件不存在，请先构建项目"
        exit 1
    fi
    cd ..
}

# 主程序
main() {
    local build_type="Release"
    local clean=false
    local test=false
    local install=false
    local package=false
    local run=false
    
    # 解析命令行参数
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_help
                exit 0
                ;;
            -d|--debug)
                build_type="Debug"
                shift
                ;;
            -r|--release)
                build_type="Release"
                shift
                ;;
            -c|--clean)
                clean=true
                shift
                ;;
            -t|--test)
                test=true
                shift
                ;;
            -i|--install)
                install=true
                shift
                ;;
            -p|--package)
                package=true
                shift
                ;;
            --run)
                run=true
                shift
                ;;
            *)
                print_error "未知选项: $1"
                show_help
                exit 1
                ;;
        esac
    done
    
    print_status "======================================"
    print_status "    OPC UA服务器构建脚本"
    print_status "======================================"
    
    # 检查依赖
    check_dependencies
    
    # 清理构建目录
    if [ "$clean" = true ]; then
        clean_build
        if [ "$test" = false ] && [ "$install" = false ] && [ "$package" = false ] && [ "$run" = false ]; then
            exit 0
        fi
    fi
    
    # 构建项目
    build_project "$build_type"
    
    # 运行测试
    if [ "$test" = true ]; then
        run_tests
    fi
    
    # 安装项目
    if [ "$install" = true ]; then
        install_project
    fi
    
    # 创建安装包
    if [ "$package" = true ]; then
        create_package
    fi
    
    # 运行服务器
    if [ "$run" = true ]; then
        run_server
    fi
    
    print_success "所有操作完成！"
    print_status "可执行文件位置: build/opcua_server"
    print_status "使用 './build/opcua_server --help' 获取运行帮助"
}

# 执行主程序
main "$@" 