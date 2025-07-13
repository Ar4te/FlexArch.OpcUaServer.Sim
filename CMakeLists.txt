cmake_minimum_required(VERSION 3.10)

# 项目信息
project(OpcUaServer 
    VERSION 1.0.0
    DESCRIPTION "完整功能的OPC UA服务端模拟器"
    LANGUAGES C
)

# 设置C标准
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 编译类型设置
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 编译选项
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# 查找必要的库
find_package(Threads REQUIRED)
find_library(MATH_LIBRARY m)

# 包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# 源文件
set(SOURCES
    server.c
    ./includes/open62541.c
)

# 头文件
set(HEADERS
    ./includes/open62541.h
)

# 创建可执行文件
add_executable(opcua_server ${SOURCES} ${HEADERS})

# 链接库
target_link_libraries(opcua_server 
    PRIVATE 
    Threads::Threads
    ${MATH_LIBRARY}
)

# 设置编译器特定选项
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(opcua_server PRIVATE 
        -Wno-unused-parameter
        -Wno-unused-variable
        -fPIC
    )
elseif(CMAKE_C_COMPILER_ID STREQUAL "Clang")
    target_compile_options(opcua_server PRIVATE 
        -Wno-unused-parameter
        -Wno-unused-variable
        -fPIC
    )
elseif(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(opcua_server PRIVATE 
        /W3
        /wd4996  # 禁用不安全函数警告
    )
endif()

# 平台特定设置
if(WIN32)
    target_link_libraries(opcua_server PRIVATE ws2_32 wsock32)
    target_compile_definitions(opcua_server PRIVATE _WIN32_WINNT=0x0600)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(opcua_server PRIVATE rt)
endif()

# 安装设置
install(TARGETS opcua_server
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# 安装头文件
install(FILES ${HEADERS}
    DESTINATION include/opcua_server
)

# 创建配置文件
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/config.h"
    @ONLY
)

# 包含生成的配置文件
target_include_directories(opcua_server PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(opcua_server PRIVATE HAVE_CONFIG_H)

# 测试支持
enable_testing()

# 基本功能测试
add_test(NAME server_help_test
    COMMAND opcua_server --help
)

add_test(NAME server_version_test
    COMMAND opcua_server --version
)

# 自定义目标
add_custom_target(run
    COMMAND opcua_server
    DEPENDS opcua_server
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "运行OPC UA服务器"
)

add_custom_target(debug
    COMMAND opcua_server --debug
    DEPENDS opcua_server
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "以调试模式运行OPC UA服务器"
)

add_custom_target(clean-all
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/CMakeCache.txt
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/cmake_install.cmake
    COMMAND ${CMAKE_COMMAND} -E remove ${CMAKE_CURRENT_BINARY_DIR}/Makefile
    COMMENT "清理所有构建文件"
)

# 打包支持
set(CPACK_PACKAGE_NAME "OpcUaServer")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_CONTACT "opcua-server@example.com")
set(CPACK_GENERATOR "TGZ;ZIP")

if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
endif()

if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "TGZ;DEB;RPM")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.17)")
    set(CPACK_RPM_PACKAGE_REQUIRES "glibc >= 2.17")
endif()

include(CPack)

# 显示配置信息
message(STATUS "====================================")
message(STATUS "OPC UA服务器配置信息")
message(STATUS "====================================")
message(STATUS "项目名称: ${PROJECT_NAME}")
message(STATUS "版本: ${PROJECT_VERSION}")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "C编译器: ${CMAKE_C_COMPILER}")
message(STATUS "C标准: ${CMAKE_C_STANDARD}")
message(STATUS "安装前缀: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "====================================") 