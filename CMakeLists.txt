cmake_minimum_required(VERSION 3.20)
project(FlowHub VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- 选项 -------------------------------------------------------
option(FLOWHUB_BUILD_TESTS "构建单元测试" ON)

# ---- 依赖：CAF (C++ Actor Framework) ---------------------------
include(FetchContent)

FetchContent_Declare(
    caf
    GIT_REPOSITORY https://github.com/actor-framework/actor-framework.git
    GIT_TAG        1.1.0
)
FetchContent_MakeAvailable(caf)

# ---- 依赖：GTest（仅当启用测试时）-------------------------------
if(FLOWHUB_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.17.0
    )
    FetchContent_MakeAvailable(googletest)
endif()

# ---- 依赖：libcurl（HTTP 通信）----------------------------------
find_package(CURL REQUIRED)

# ---- 代码生成 ----------------------------------------------------
# 自动收集所有 .mt 文件，新增 .mt 无需手动注册
file(GLOB_RECURSE MT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/common/type/*.mt
    ${CMAKE_SOURCE_DIR}/src/common/message/*.mt
    ${CMAKE_SOURCE_DIR}/src/common/context/*.mt
)

add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/src/generated/message/Messages.hpp
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/gen_code.py
    DEPENDS
        ${CMAKE_SOURCE_DIR}/scripts/gen_code.py
        ${MT_SOURCES}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating C++ headers from .mt files"
)
add_custom_target(gen_code DEPENDS ${CMAKE_SOURCE_DIR}/src/generated/message/Messages.hpp)

# ---- 进入子目录 -------------------------------------------------
add_subdirectory(src)
