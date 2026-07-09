add_library(userAccessLib STATIC
    AccessGateway.cpp
    CliAdapter.cpp
)
target_include_directories(userAccessLib PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(userAccessLib PUBLIC CAF::core)

if(FLOWHUB_BUILD_TESTS)
    add_subdirectory(ut)
endif()