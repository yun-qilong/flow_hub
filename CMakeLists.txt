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

# ---- 依赖：nlohmann/json（JSON 编解码）----------------------------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# ---- 依赖：GTest（仅当启用测试时）-------------------------------
if(FLOWHUB_BUILD_TESTS)
    enable_testing()
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

# ---- 日志配置代码生成 --------------------------------------------
add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/src/generated/LogConfig.hpp
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/gen_log_config.py
    DEPENDS
        ${CMAKE_SOURCE_DIR}/scripts/gen_log_config.py
        ${CMAKE_SOURCE_DIR}/config/log_config.conf
        ${CMAKE_SOURCE_DIR}/src/utils/LogTypes.hpp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating LogConfig.hpp from log_config.conf"
)
add_custom_target(gen_log_config DEPENDS ${CMAKE_SOURCE_DIR}/src/generated/LogConfig.hpp)

# ---- UT 编译 target（供 i but 使用）-----------------------------
# flowhub_ut 必须在 add_subdirectory 之前定义，因为子目录会 add_dependencies 到它
if(FLOWHUB_BUILD_TESTS)
    add_custom_target(flowhub_ut)

    # ---- 辅助函数：添加单元测试 ------------------------------------
    # 用法: flowhub_add_ut(TestXxx SOURCES TestXxx.cpp ../Xxx.cpp [EXTRA_LIBS GTest::gmock_main])
    function(flowhub_add_ut TEST_NAME)
        cmake_parse_arguments(UT "" "" "SOURCES;EXTRA_LIBS" ${ARGN})

        add_executable(${TEST_NAME} ${UT_SOURCES})
        target_compile_definitions(${TEST_NAME} PRIVATE FLOWHUB_TEST_BUILD)
        target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)
        target_link_libraries(${TEST_NAME} PRIVATE CAF::core GTest::gtest_main ${UT_EXTRA_LIBS})
        add_dependencies(${TEST_NAME} gen_code gen_log_config)
        add_dependencies(flowhub_ut ${TEST_NAME})

        include(GoogleTest)
        gtest_discover_tests(${TEST_NAME} PROPERTIES LABELS "flowhub")
    endfunction()
endif()

# ---- 进入子目录 -------------------------------------------------
add_subdirectory(src)

# ---- UT 基础编译 target（供 i but base 使用）--------------------
# ut_base 依赖 flowhub target（在 src/ 中定义），因此放在 add_subdirectory 之后
if(FLOWHUB_BUILD_TESTS)
    add_custom_target(ut_base)
    add_dependencies(ut_base
        flowhub
        gtest gtest_main
        gmock gmock_main
    )
endif()
