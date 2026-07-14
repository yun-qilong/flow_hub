# ---- 进入各子目录，收集 OBJECT library --------------------------
add_subdirectory(fw)
add_subdirectory(common)
add_subdirectory(utils)
add_subdirectory(userAccess)
add_subdirectory(CPlane)
add_subdirectory(DPlane)

# 确保生成的头文件在编译任何库之前就绪
add_dependencies(commonLib gen_code)
add_dependencies(utilsLib gen_code gen_log_config)
add_dependencies(userAccessLib gen_code)
add_dependencies(cplaneLib gen_code)
add_dependencies(sessionLib gen_code)
add_dependencies(businessLib gen_code)
add_dependencies(serviceLib gen_code)

# ---- 可执行文件 ------------------------------------------------
add_executable(flowhub
    main.cpp
)

add_dependencies(flowhub gen_code gen_log_config)

target_include_directories(flowhub PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(flowhub PRIVATE
    CAF::core
    fwLib
    commonLib
    cplaneLib
    dplaneLib
    userAccessLib
)
