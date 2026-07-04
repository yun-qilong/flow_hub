add_library(accessLib STATIC
    AccessGateway.cpp
    CliAdapter.cpp
)
target_include_directories(accessLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(accessLib PUBLIC CAF::core)