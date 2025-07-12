# OPC UA 服务器模拟器

一个功能完整的 OPC UA 服务端模拟器，基于 open62541 库实现，支持多种数据类型、数据模拟、方法调用等企业级功能。

## 🚀 功能特性

### 核心功能

- **多种数据类型支持**：Int32, UInt32, Float, Double, Boolean, String, DateTime
- **智能数据模拟**：正弦波、随机数、计数器、方波模拟
- **方法调用**：支持输入输出参数的方法调用
- **层次化节点**：对象节点、变量节点的层次化组织
- **事件系统**：自定义事件和报警通知
- **实时诊断**：性能监控、日志系统、统计信息

### 高级特性

- **多线程架构**：独立的数据模拟和诊断线程
- **线程安全**：完整的互斥锁保护机制
- **内存管理**：零内存泄漏设计
- **优雅停止**：信号处理和资源清理
- **跨平台支持**：Windows、Linux、macOS

## 🛠️ 构建要求

### 系统要求

- CMake 3.10 或更高版本
- C99 兼容的编译器（GCC、Clang、MSVC）
- pthread 库（多线程支持）
- 数学库（libm）

### 平台特定要求

- **Windows**: Visual Studio 2017+ 或 MinGW
- **Linux**: GCC 4.9+ 或 Clang 3.8+
- **macOS**: Xcode 8.0+

## 📦 构建和安装

### 基本构建

```bash
# 创建构建目录
mkdir build && cd build

# 配置项目
cmake ..

# 编译
make

# 运行
./opcua_server
```

### 高级构建选项

```bash
# Debug模式构建
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# Release模式构建
cmake -DCMAKE_BUILD_TYPE=Release ..
make

# 指定安装路径
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
make install
```

### Windows 构建

```powershell
# 使用Visual Studio
mkdir build && cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build . --config Release

# 使用MinGW
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
make
```

## 🎯 使用方法

### 基本运行

```bash
# 默认运行（端口4840）
./opcua_server

# 显示帮助信息
./opcua_server --help

# 启用调试模式
./opcua_server --debug

# 禁用诊断信息
./opcua_server --no-diagnostics
```

### 连接测试

- **服务器端口**: 4840
- **连接 URL**: `opc.tcp://localhost:4840`
- **推荐客户端**:
  - UaExpert (Windows)
  - Prosys OPC UA Client (跨平台)
  - UAExpert (Linux)

### 节点结构

```
Root/
├── Objects/
│   ├── Motor/          (对象节点)
│   ├── Temperature/    (对象节点)
│   ├── Int32Variable   (基本整型变量)
│   ├── FloatVariable   (浮点型变量)
│   ├── BooleanVariable (布尔型变量)
│   ├── StringVariable  (字符串变量)
│   ├── SineWave        (正弦波模拟)
│   ├── RandomInteger   (随机整数)
│   ├── Counter         (计数器)
│   ├── HelloMethod     (方法调用)
│   └── CalculateMethod (计算方法)
```

## 🔧 CMake 目标

### 构建目标

```bash
# 构建可执行文件
make opcua_server

# 运行服务器
make run

# 调试模式运行
make debug

# 清理构建文件
make clean

# 完全清理
make clean-all
```

### 测试目标

```bash
# 运行所有测试
make test

# 或者使用ctest
ctest -V
```

### 打包目标

```bash
# 创建源码包
make package_source

# 创建二进制包
make package

# 创建安装包（Windows）
make package  # 生成NSIS安装包

# 创建DEB包（Ubuntu/Debian）
make package  # 生成.deb文件

# 创建RPM包（RedHat/CentOS）
make package  # 生成.rpm文件
```

## 🧪 开发和调试

### 调试模式

```bash
# 构建调试版本
cmake -DCMAKE_BUILD_TYPE=Debug ..
make

# 使用GDB调试
gdb ./opcua_server
```

### 代码分析

```bash
# 静态分析（需要安装clang-static-analyzer）
scan-build make

# 内存检查（需要安装valgrind）
valgrind --leak-check=full ./opcua_server
```

## 📁 项目结构

```
opcua_server/
├── CMakeLists.txt      # CMake配置文件
├── config.h.in         # 配置文件模板
├── README.md           # 项目说明
├── server.c            # 主服务器代码
├── open62541.c         # OPC UA库实现
├── open62541.h         # OPC UA库头文件
└── build/              # 构建目录（生成）
    ├── opcua_server    # 可执行文件
    ├── config.h        # 生成的配置文件
    └── ...
```

## 🔍 故障排除

### 常见问题

1. **编译错误：找不到 pthread**

   ```bash
   # Ubuntu/Debian
   sudo apt-get install libpthread-stubs0-dev

   # CentOS/RHEL
   sudo yum install glibc-devel
   ```

2. **编译错误：找不到数学库**

   ```bash
   # 确保链接了数学库
   cmake .. -DMATH_LIBRARY=/usr/lib/x86_64-linux-gnu/libm.so
   ```

3. **运行时错误：端口被占用**

   ```bash
   # 检查端口占用
   netstat -an | grep 4840

   # 或者修改端口（需要修改源码）
   ```

### 日志调试

```bash
# 启用详细日志
./opcua_server --debug

# 重定向日志到文件
./opcua_server --debug > server.log 2>&1
```

## 🤝 贡献

欢迎贡献代码！请遵循以下步骤：

1. Fork 项目
2. 创建功能分支
3. 提交更改
4. 创建 Pull Request

## 📄 许可证

本项目采用 MIT 许可证 - 详见 LICENSE 文件。

## 🔗 相关资源

- [OPC UA 规范](https://opcfoundation.org/about/opc-technologies/opc-ua/)
- [open62541 库](https://open62541.org/)
- [CMake 文档](https://cmake.org/documentation/)

## 📧 联系方式

如有问题或建议，请通过以下方式联系：

- 邮箱: opcua-server@example.com
- 项目 Issues: [GitHub Issues](https://github.com/your-repo/opcua_server/issues)

---

**注意**: 此项目仅用于教育和测试目的，在生产环境中使用前请进行充分的安全性测试。
