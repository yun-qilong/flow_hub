# ---- 进入各子目录，收集 OBJECT library --------------------------
add_subdirectory(fw)
add_subdirectory(common)
add_subdirectory(utils)
#add_subdirectory(access)
add_subdirectory(CPlane)
add_subdirectory(DPlane)

# ---- 可执行文件 ------------------------------------------------
add_executable(flowhub
    main.cpp
)

add_dependencies(flowhub gen_code)

target_include_directories(flowhub PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(flowhub PRIVATE
    CAF::core
    fwLib
    commonLib
    cplaneLib
    dplaneLib
    #accessLib
)
