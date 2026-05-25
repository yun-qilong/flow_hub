add_library(cplaneLib STATIC
    BusinessMgr.cpp
)
target_include_directories(cplaneLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(cplaneLib PUBLIC CAF::core)
