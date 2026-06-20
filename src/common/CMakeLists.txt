add_library(commonLib STATIC
    TaskPool.cpp
)
target_include_directories(commonLib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(commonLib PUBLIC CAF::core)
